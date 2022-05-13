#include "pch.h"
#include "TrackMapper.h"

using namespace iohkx;

//Is this skeleton a HORSE?
static bool isHorse(const Skeleton* skeleton)
{
	//How can we tell if this is a HORSE?
	//Horse bones are named beginning with Horse, except for "NPC Root [Root]"
	//and "SaddleBone". If we look at 3 bones we'll know.
	for (int i = 0; i < 3 && i < skeleton->nBones; i++) {
		if (std::strncmp(skeleton->bones[i].name.c_str(), "Horse", 5) == 0) {
			return true;
		}
	}
	return false;
}

//Find the (clip index, bone) from an annotation name
static std::pair<int, Bone*> findBone(const char* name, const std::vector<Clip>& clips)
{
	//This is made a lot harder than it should be, because of horses. Thanks, horses.

	if (std::strcmp(name, "PairedRoot") == 0) {
		//This is not an actual track (?)
		return { -1, nullptr };
	}

	//Drop the "2_" prefix, if any, and pick out special cases
	int clip;//clip index

	if (std::strncmp(name, "2_", 2) == 0) {
		//Look in secondary skeleton, without the prefix
		name += 2;
		clip = 1;

		//If "2_" is the full name, this is a special case
		if (*name == '\0') {
			//this name seems to have multiple possible meanings
			//if this is a HORSE, this is "NPC Root [Root]"
			//else it is the root bone

			if (isHorse(clips[clip].skeleton)) {
				//find "NPC Root [Root]". Should be the first bone.
				for (int i = 0; i < clips[clip].skeleton->nBones; i++) {
					if (clips[clip].skeleton->bones[i].name == "NPC Root [Root]") {
						return { clip, &clips[clip].skeleton->bones[i] };
					}
				}
				//unexpected
				return { -1, nullptr };
			}
			else {
				return { clip, clips[clip].skeleton->rootBone };
			}
		}
		//Also, the name "2_Horse" is unusual
		else if (std::strcmp(name, "Horse") == 0) {
			//Horses are stupid?
			//This is the root bone
			return { clip, clips[clip].skeleton->rootBone };
		}
	}
	else if (std::strcmp(name, "SaddleBone") == 0) {
		//Another special case? This belongs to secondary actor (a HORSE)
		clip = 1;
	}
	else {
		//Look in primary skeleton
		clip = 0;

		if (std::strcmp(name, "NPC") == 0) {
			//Root bone
			return { clip, clips[clip].skeleton->rootBone };
		}
	}
	//Regular case, search skeleton
	auto it = clips[clip].skeleton->boneIndex.find(name);
	if (it != clips[clip].skeleton->boneIndex.end()) {
		return { clip, it->second };
	}
	else {
		//This bone is not in the skeleton.
		//I would have considered this evidence of a mismatched skeleton,
		//but it seems horses are weird.
		return { clip, nullptr };
	}

	return { -1, nullptr };
}

static bool mapBonesIfMissing(int n, const Clip& clip, hkArray<hkInt16>& target)
{
	target.clear();
	bool result = n < clip.skeleton->nBones;
	if (result) {
		assert(n == clip.nBoneTracks);
		target.reserve(n);

		//sort by bone index
		for (int i = 0; i < clip.skeleton->nBones; i++) {
			if (clip.boneMap[i]) {
				assert(clip.boneMap[i]->target->index == i);//or we messed up the map
				target.pushBack(i);
			}
		}
		assert(target.getSize() == n);
	}
	return result;
}

static bool mapFloatsIfMissing(int n, const Clip& clip, hkArray<hkInt16>& target)
{
	target.clear();
	bool result = n < clip.skeleton->nFloats;
	if (result) {
		assert(n == clip.nFloatTracks);
		target.reserve(n);

		//sort by skeleton order
		for (int i = 0; i < clip.skeleton->nFloats; i++) {
			if (clip.floatMap[i]) {
				assert(clip.floatMap[i]->target->index == i);//or we messed up the map
				target.pushBack(i);
			}
		}
		assert(target.getSize() == n);
	}
	return result;
}

//Set the appropriate annotation name, depending on the type of actor
static void setSecondaryName(
	const std::string& name,
	const Skeleton* skeleton,
	hkaAnnotationTrack& annotation)
{
	if (name == "NPC Root [Root]" && isHorse(skeleton)) {
		annotation.m_trackName = "2_";
	}
	else {
		annotation.m_trackName.printf("2_%s", name.c_str());
	}
}

//Set the appropriate annotation name, depending on the type of actor
static void setSecondaryRootName(
	const std::string& name,
	const Skeleton* skeleton,
	hkaAnnotationTrack& annotation)
{
	if (isHorse(skeleton)) {
		annotation.m_trackName = "2_Horse";
	}
	else {
		annotation.m_trackName = "2_";
	}
}

bool iohkx::TrackPacker::map(const AnimationData& data, 
	hkaAnimation* animation, hkaAnimationBinding* binding)
{
	if (data.clips.size() == 2) {
		return paired(data, animation, binding);
	}
	else if (data.clips.size() == 1) {
		return single(data, animation, binding);
	}
	else {
		return false;
	}
}

bool iohkx::TrackPacker::paired(const AnimationData& data, 
	hkaAnimation* animation, hkaAnimationBinding* binding)
{
	assert(animation && binding && data.clips.size() == 2);

	m_bones.clear();
	m_floats.clear();

	if (data.additive)
		//I've never seen this in paired, don't know how to deal with it
		return false;

	binding->m_originalSkeletonName = "PairedRoot";

	auto&& primary = data.clips.front();
	auto&& secondary = data.clips.back();

	//Include all bones in both skeletons, their root bones and the paired root
	int nBones = primary.skeleton->nBones + secondary.skeleton->nBones + 3;
	//and any addenda
	//nBones += primary.addenda.size() + secondary.addenda.size();

	//Some paired anims have float track. Let's assume they belong to the primary actor.
	int nFloats = primary.nFloatTracks;

	//Fill out the targets and annotation names
	m_bones.resize(nBones);
	m_floats.resize(nFloats);
	animation->m_annotationTracks.setSize(nBones);
	auto&& annotations = animation->m_annotationTracks;

	//Bones and annotations

	//PairedRoot (has neither track nor bone)
	m_bones[0].first = nullptr;
	m_bones[0].second = nullptr;
	annotations[0].m_trackName = "PairedRoot";

	//I don't know if the order here matters. Let's assume not.

	//Primary actor
	// root
	m_bones[1].first = primary.rootTransform;
	m_bones[1].second = primary.skeleton->rootBone;
	annotations[1].m_trackName = ROOT_BONE;
	int current = 2;
	//annotations go to bone 0
	m_annotationTracks[0] = current;
	// bones
	for (int i = 0; i < primary.skeleton->nBones; i++) {
		m_bones[current].first = primary.boneMap[i];
		m_bones[current].second = &primary.skeleton->bones[i];
		annotations[current].m_trackName = m_bones[current].second->name.c_str();
		++current;
	}
	// addenda
	//for (unsigned int i = 0; i < primary.addenda.size(); i++) {
	//	bones[current].first = primary.addenda[i].track.get();
	//	bones[current].second = primary.addenda[i].bone.get();
	//	annotations[current].m_trackName = bones[current].second->name.c_str();
	//	++current;
	//}

	//Secondary actor
	// root
	m_bones[current].first = secondary.rootTransform;
	m_bones[current].second = secondary.skeleton->rootBone;
	setSecondaryRootName(secondary.skeleton->rootBone->name,
		secondary.skeleton, annotations[current]);
	++current;
	//annotations go to bone 0
	m_annotationTracks[1] = current;
	// bones
	for (int i = 0; i < secondary.skeleton->nBones; i++) {
		m_bones[current].first = secondary.boneMap[i];
		m_bones[current].second = &secondary.skeleton->bones[i];
		setSecondaryName(secondary.skeleton->bones[i].name,
			secondary.skeleton, annotations[current]);
		++current;
	}
	// addenda
	//for (unsigned int i = 0; i < secondary.addenda.size(); i++) {
	//	bones[current].first = secondary.addenda[i].track.get();
	//	bones[current].second = secondary.addenda[i].bone.get();
	//	annotations[current].m_trackName = bones[current].second->name.c_str();
	//	++current;
	//}

	assert(current == nBones);

	//Floats (ignore secondary actor)
	bool missingFloats = mapFloatsIfMissing(nFloats, primary,
		binding->m_floatTrackToFloatSlotIndices);

	for (int i = 0; i < nFloats; i++) {
		int index = missingFloats ? binding->m_floatTrackToFloatSlotIndices[i] : i;
		m_floats[i] = primary.floatMap[index];
	}

	return true;
}

bool iohkx::TrackPacker::single(const AnimationData& data, 
	hkaAnimation* animation, hkaAnimationBinding* binding)
{
	assert(animation && binding && !data.clips.empty());

	auto&& clip = data.clips.front();

	binding->m_originalSkeletonName = clip.skeleton->name.c_str();

	//In additive mode, we'll include whatever tracks the user exported.
	//In normal mode, we'll fill all missing tracks with the bind pose.
	int nBones = data.additive ? clip.nBoneTracks : clip.skeleton->nBones;

	//We'll include whatever float tracks the user exported.
	int nFloats = clip.nFloatTracks;

	//Unrelated tracks should have been filtered out earlier
	assert(nBones <= clip.skeleton->nBones && nFloats <= clip.skeleton->nFloats);

	//If bones are missing, we need the mapping from clip.boneTracks to bone index.
	bool missingBones = mapBonesIfMissing(nBones, clip, binding->m_transformTrackToBoneIndices);
	bool missingFloats = mapFloatsIfMissing(nFloats, clip, binding->m_floatTrackToFloatSlotIndices);

	m_bones.resize(nBones, { nullptr, nullptr });
	m_floats.resize(nFloats, nullptr);

	//Fill out bones and floats in whatever order we want it in the final animation,
	//i.e. in skeleton order
	for (int i = 0; i < nBones; i++) {
		int index = missingBones ? binding->m_transformTrackToBoneIndices[i] : i;
		m_bones[i].first = clip.boneMap[index];//may be null
		m_bones[i].second = &clip.skeleton->bones[index];

		if (index == 0) {
			//This track points to the annotation bone
			m_annotationTracks[0] = i;
		}
	}
	for (int i = 0; i < nFloats; i++) {
		int index = missingFloats ? binding->m_floatTrackToFloatSlotIndices[i] : i;
		m_floats[i] = clip.floatMap[index];//may be null
	}

	//Initialise annotation tracks
	animation->m_annotationTracks.setSize(nBones);
	for (int i = 0; i < nBones; i++)
		//(the strings are not initialised)
		animation->m_annotationTracks[i].m_trackName = "";

	return true;
}

bool iohkx::TrackUnpacker::map(AnimationData& data, const std::vector<Skeleton*>& skeletons, 
	const hkaAnimation* animation, const hkaAnimationBinding* binding)
{
	m_bones.resize(animation->m_numberOfTransformTracks, nullptr);
	m_floats.resize(animation->m_numberOfFloatTracks, nullptr);

	if (binding->m_originalSkeletonName == "PairedRoot")
		return paired(data, skeletons, animation, binding);
	else
		return single(data, skeletons, animation, binding);
}

bool iohkx::TrackUnpacker::paired(AnimationData& data, const std::vector<Skeleton*>& skeletons, 
	const hkaAnimation* animation, const hkaAnimationBinding* binding)
{
	assert(animation && binding);

	//We expect all paired animations to have transform tracks
	if (!animation || animation->m_numberOfTransformTracks == 0)
		return false;

	//We need annotations to map the tracks. Nothing to do if they are missing.
	if (animation->m_numberOfTransformTracks != animation->m_annotationTracks.getSize())
		return false;

	//There must be at least one skeleton, but we can't say anything about
	//bone counts at this time.
	if (skeletons.empty() || skeletons[0] == nullptr)
		return false;

	data.clips.resize(2);
	auto&& primary = data.clips[0];
	auto&& secondary = data.clips[1];

	//If there is only one skeleton, we use it for both clips
	if (skeletons.size() == 1) {
		primary.skeleton = skeletons[0];
		secondary.skeleton = skeletons[0];
	}
	else {
		primary.skeleton = skeletons[0];
		secondary.skeleton = skeletons[1];
	}

	//Identify target bone by annotations
	//We'll do bone name lookup to map track index to a bone
	std::vector<std::pair<int, Bone*>> maps[2];//(track, bone)
	maps[0].reserve(animation->m_numberOfTransformTracks);
	maps[1].reserve(animation->m_numberOfTransformTracks);

	std::pair<int, Bone*> roots[2]{ { -1, nullptr }, { -1, nullptr } };

	//std::vector<std::pair<int, Bone*>> addenda[2];

	for (int i = 0; i < animation->m_numberOfTransformTracks; i++) {
		//get (clip index, bone) by name
		auto&& name = animation->m_annotationTracks[i].m_trackName;
		auto pair = findBone(name.cString(), data.clips);

		if (pair.first == 0 || pair.first == 1) {
			Clip& clip = data.clips[pair.first];
			if (pair.second) {
				//We found a bone
				if (pair.second == clip.skeleton->rootBone) {

					//store the root immediately
					assert(!clip.rootTransform);
					clip.rootTransform = new BoneTrack;
					clip.rootTransform->target = clip.skeleton->rootBone;
					m_bones[i] = clip.rootTransform;
				}
				else {
					maps[pair.first].push_back({ i, pair.second });

					//the annotations are in bone 0 of the primary skeleton (?)
					if (pair.second == &primary.skeleton->bones[0]) {
						m_annotationClip = 0;
						m_annotationTrack = i;
					}
				}
			}
			else {
				//This has been assigned to one of the clips, but it has no bone.
				//Horse anims will get here. What to do? Do we create this bone?
				//Ignore it?

				//Create an addendum to the skeleton
				//auto&& addenda = m_data.clips[pair.first].addenda;
				//addenda.push_back(Addendum());
				//addenda.back().bone = std::make_unique<Bone>();
				//get rid of the prefix
				//const char* p = name.cString();
				//if (std::strncmp(name, "2_", 2) == 0) {
				//	p += 2;
				//}
				//addenda.back().bone->name = p;
				//addenda.back().bone->refPose.setIdentity();
				//addenda.back().bone->refPoseObj.setIdentity();

				//addenda.back().track = std::make_unique<BoneTrack>();
				//addenda.back().track->target = addenda.back().bone.get();

				//bones[i] = addenda.back().track.get();
			}
		}
	}

	for (unsigned int i = 0; i < data.clips.size(); i++) {
		//Allocate the tracks
		int nTracks = maps[i].size();
		data.clips[i].nBoneTracks = nTracks;
		data.clips[i].boneTracks = new BoneTrack[nTracks];
		data.clips[i].boneMap.resize(data.clips[i].skeleton->nBones, nullptr);

		//Some paired anims have float tracks, but I don't know how to interpret that.
		//Leave them for now.
		//m_data.clips[i].nFloatTracks = 0;
		//m_data.clips[i].floatTracks = nullptr;
		//m_data.clips[i].floatMap.resize(m_data.clips[i].skeleton->nFloats, nullptr);

		for (int j = 0; j < nTracks; j++) {
			//Set the track targets
			BoneTrack* track = &data.clips[i].boneTracks[j];
			track->target = maps[i][j].second;
			data.clips[i].boneMap[track->target->index] = track;

			//Final mapping
			m_bones[maps[i][j].first] = track;
		}
	}

	//Some paired anims have float tracks. Let's assume they belong to the primary actor,
	//if there are slots for them.

	int nFloats = m_floats.size();

	//Does the number of tracks match the number of slots?
	bool validFloats = nFloats == primary.skeleton->nFloats;

	//If not, do we have a valid mapping?
	if (!validFloats && binding->m_floatTrackToFloatSlotIndices.getSize() == nFloats) {

		validFloats = true;

		//make sure no index exceeds the number of slots
		for (int i = 0; i < nFloats; i++) {
			if (binding->m_floatTrackToFloatSlotIndices[i] >= primary.skeleton->nFloats) {
				validFloats = false;
				nFloats = 0;
				break;
			}
		}
	}

	if (validFloats) {
		primary.nFloatTracks = nFloats;
		primary.floatTracks = new FloatTrack[nFloats];
		primary.floatMap.resize(primary.skeleton->nFloats, nullptr);

		bool missingFloats = nFloats != primary.skeleton->nFloats;
		for (int i = 0; i < nFloats; i++) {
			int index = missingFloats ? binding->m_floatTrackToFloatSlotIndices[i] : i;

			primary.floatTracks[i].target = &primary.skeleton->floats[index];

			m_floats[i] = &primary.floatTracks[i];
		}
	}

	return true;
}

bool iohkx::TrackUnpacker::single(AnimationData& data, const std::vector<Skeleton*>& skeletons, 
	const hkaAnimation* animation, const hkaAnimationBinding* binding)
{
	assert(animation && binding);

	//Check validity of the skeleton first
	//Skyrim has the same name for all skeletons, so we can't rely on that.
	//If it has fewer slots than we have tracks, it's definitely the wrong one.
	//If it has *more* slots, it might still be correct.
	if (skeletons.empty()
		|| skeletons[0] == nullptr
		|| skeletons[0]->nBones < (int)m_bones.size()
		|| skeletons[0]->nFloats < (int)m_floats.size())
		return false;

	//There will be only one clip
	data.clips.resize(1);
	auto&& clip = data.clips.front();
	m_annotationClip = 0;

	//Point it to its skeleton
	clip.skeleton = skeletons[0];

	//Allocate the tracks
	clip.nBoneTracks = m_bones.size();
	clip.boneTracks = new BoneTrack[clip.nBoneTracks];
	//we don't need this map here
	//clip.boneMap.resize(clip.skeleton->nBones, nullptr);

	clip.nFloatTracks = m_floats.size();
	clip.floatTracks = new FloatTrack[clip.nFloatTracks];
	//clip.floatMap.resize(clip.skeleton->nFloats, nullptr);

	//Assign the track targets
	if (binding->m_transformTrackToBoneIndices.isEmpty()) {
		//The tracks should map 1:1 with the skeleton
		for (int i = 0; i < clip.nBoneTracks; i++) {
			clip.boneTracks[i].target = &skeletons[0]->bones[i];
			//clip.boneMap[i] = &clip.boneTracks[i];
		}
		//The annotations should be in track 0
		m_annotationTrack = 0;
	}
	else {
		//Map from the binding
		for (int i = 0; i < clip.nBoneTracks; i++) {
			int index = binding->m_transformTrackToBoneIndices[i];
			Bone* bone = &skeletons[0]->bones[index];
			clip.boneTracks[i].target = bone;
			//clip.boneMap[bone->index] = &clip.boneTracks[i];

			//if we run into bone 0, we know where the annotations are
			if (bone->index == 0) {
				m_annotationTrack = i;
			}
		}
	}
	if (binding->m_floatTrackToFloatSlotIndices.isEmpty()) {
		//The tracks should map 1:1 with the skeleton
		for (int i = 0; i < clip.nFloatTracks; i++) {
			clip.floatTracks[i].target = &skeletons[0]->floats[i];
			//clip.floatMap[i] = &clip.floatTracks[i];
		}
	}
	else {
		//Map from the binding
		for (int i = 0; i < clip.nFloatTracks; i++) {
			int index = binding->m_floatTrackToFloatSlotIndices[i];
			Float* flt = &skeletons[0]->floats[index];
			clip.floatTracks[i].target = &skeletons[0]->floats[index];
			//clip.floatMap[flt->index] = &clip.floatTracks[i];
		}
	}

	//Do the final mapping (in this case just a copy of the track list)
	for (int i = 0; i < clip.nBoneTracks; i++) {
		m_bones[i] = &clip.boneTracks[i];
	}
	for (int i = 0; i < clip.nFloatTracks; i++) {
		m_floats[i] = &clip.floatTracks[i];
	}

	return true;
}
