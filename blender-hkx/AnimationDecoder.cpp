#include "pch.h"
#include "AnimationDecoder.h"

constexpr int FRAME_RATE = 30;

using namespace iohkx;

//Transform the bone and its descendants to parent-space transform T
static void objToParent(Bone* bone, Clip& clip, int frame, 
	const hkQsTransform& T, const hkQsTransform& iT)
{
	assert(bone && frame >= 0);

	//T is our parent's current pose (in object space) and iT its inverse

	//Calc our recursion transforms
	BoneTrack* track = bone->index >= 0 ? clip.boneMap[bone->index] : clip.rootTransform;
	//if we have no track, our transform is T * our parent-space ref
	//if we do have a track, we use our current key
	hkQsTransform next_T;
	if (track && !track->keys.isEmpty()) {
		//as long as we export in object space, we need keys on every frame
		assert(track->keys.getSize() > frame);
		//save current
		next_T = track->keys[frame];
		//and update
		track->keys[frame].setMul(iT, track->keys[frame]);
	}
	else {
		next_T.setMul(T, bone->refPose);
	}

	hkQsTransform next_iT;
	next_iT.setInverse(next_T);

	//then recurse
	for (auto&& child : bone->children) {
		objToParent(child, clip, frame, next_T, next_iT);
	}
}

//Set the sign of all quaternions so that they rotate the shortest path
static void sanitiseQuats(Clip& clip)
{
	for (int i = 0; i < clip.nBoneTracks; i++) {
		for (int j = 1; j < clip.boneTracks[i].keys.getSize(); j++) {
			auto&& thisR = clip.boneTracks[i].keys[j].m_rotation.m_vec;
			auto&& prevR = clip.boneTracks[i].keys[j - 1].m_rotation.m_vec;
			if (thisR.dot4(prevR) < 0.0f) {
				thisR.setNeg4(thisR);
			}
		}
	}
}

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

iohkx::AnimationDecoder::AnimationDecoder()
{}

iohkx::AnimationDecoder::~AnimationDecoder()
{
	for (unsigned int i = 0; i < m_data.clips.size(); i++) {
		delete m_data.clips[i].rootTransform;
		delete[] m_data.clips[i].boneTracks;
		delete[] m_data.clips[i].floatTracks;
	}
}

hkRefPtr<hkaAnimationContainer>
iohkx::AnimationDecoder::compress()
{
	if (m_data.frames < 1 || m_data.clips.empty())
		//Nothing to compress
		return hkRefPtr<hkaAnimationContainer>();

	if (m_data.frameRate != FRAME_RATE)
		throw Exception(ERR_INVALID_INPUT, "Unsupported frame rate");

	//We expect either one key per frame, or one single key
	for (auto&& clip : m_data.clips) {
		for (int i = 0; i < clip.nBoneTracks; i++) {
			int nKeys = clip.boneTracks[i].keys.getSize();
			if (nKeys != m_data.frames && nKeys != 1)
				throw Exception(ERR_INVALID_INPUT, "Missing keys");
		}
		for (int i = 0; i < clip.nFloatTracks; i++) {
			int nKeys = clip.floatTracks[i].keys.getSize();
			if (nKeys != m_data.frames && nKeys != 1)
				throw Exception(ERR_INVALID_INPUT, "Missing keys");
		}
	}

	preProcess();

	//Create binding (will be filled out by map*Comp)
	hkaAnimationBinding* binding = new hkaAnimationBinding;

	//Initialise raw animation object
	hkRefPtr<hkaInterleavedUncompressedAnimation> raw =
		new hkaInterleavedUncompressedAnimation;
	raw->removeReference();

	//bones[i], floats[i] will be the data for output track i
	// (the pair is so that we can find the bone (to fill with ref pose)
	//  if there is no track for it)
	std::vector<std::pair<BoneTrack*, Bone*>> bones;
	std::vector<FloatTrack*> floats;
	if (m_data.clips.size() == 2) {
		mapPairedComp(binding, raw, bones, floats);
	}
	else if (m_data.clips.size() == 1) {
		mapSingleComp(binding, raw, bones, floats);
	}

	int nBones = bones.size();
	int nFloats = floats.size();

	if (nBones == 0 && nFloats == 0)
		//Nothing to export
		return hkRefPtr<hkaAnimationContainer>();

	raw->m_duration = static_cast<float>(m_data.frames - 1) / m_data.frameRate;
	raw->m_numberOfTransformTracks = nBones;
	raw->m_numberOfFloatTracks = nFloats;
	raw->m_transforms.setSize(nBones * m_data.frames);
	raw->m_floats.setSize(nFloats * m_data.frames);

	//Transfer data to raw anim
	for (int i = 0; i < nBones; i++) {
		BoneTrack* track = bones[i].first;
		if (track) {
			//Use current key, or last if none

			auto&& keys = track->keys;

			//What would this mean? How would we deal with it?
			assert(!keys.isEmpty());

			for (int f = 0; f < m_data.frames; f++) {
				raw->m_transforms[i + f * nBones] = keys.getSize() > f ? keys[f] : keys.back();
			}
		}
		else if (!m_data.additive) {
			//fill with ref pose (or identity if we have no bone)

			for (int f = 0; f < m_data.frames; f++) {
				if (bones[i].second) {
					raw->m_transforms[i + f * nBones] = bones[i].second->refPose;
				}
				else {
					raw->m_transforms[i + f * nBones].setIdentity();
				}
			}
		}
		//else ignore
	}
	for (int i = 0; i < nFloats; i++) {
		FloatTrack* track = floats[i];
		if (track) {
			//Use current key, or last if none

			auto&& keys = track->keys;

			//What would this mean? How would we deal with it?
			assert(!keys.isEmpty());

			for (int f = 0; f < m_data.frames; f++) {
				raw->m_floats[i + f * nFloats] = keys.getSize() > f ? keys[f] : keys.back();
			}
		}
		//else ignore (never fill)
	}

	hkaSkeletonUtils::normalizeRotations(raw->m_transforms.begin(), raw->m_transforms.getSize());

	hkaSplineCompressedAnimation::AnimationCompressionParams acp;
	if (m_data.clips.size() == 2)
		//paired animations need this (crash otherwise)
		acp.m_enableSampleSingleTracks = true;

	hkaSplineCompressedAnimation::TrackCompressionParams tcp;
	tcp.m_translationTolerance = 0.001f;
	tcp.m_rotationTolerance = 0.0001f;
	tcp.m_scaleTolerance = 0.001f;
	tcp.m_floatingTolerance = 0.001f;

	//tcp.m_translationQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::BITS8;
	tcp.m_rotationQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::THREECOMP40;
	//tcp.m_scaleQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::BITS8;
	//tcp.m_floatQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::BITS8;

	assert(tcp.isOk());

	binding->m_animation = new hkaSplineCompressedAnimation( *raw, tcp, acp );

	hkRefPtr<hkaAnimationContainer> animCtnr = new hkaAnimationContainer;
	animCtnr->removeReference();
	animCtnr->m_bindings.pushBack(binding);
	animCtnr->m_animations.pushBack(binding->m_animation);

	return animCtnr;
}

void iohkx::AnimationDecoder::decompress(
	hkaAnimationContainer* animCtnr, const std::vector<Skeleton*>& skeletons)
{
	//Init to a meaningful state, whether we are being reused or not
	m_data.frames = 0;
	m_data.frameRate = 30;
	m_data.additive = false;
	m_data.clips.clear();

	//Abort if there is no data
	if (!animCtnr || animCtnr->m_animations.isEmpty() || animCtnr->m_bindings.isEmpty())
		return;

	hkaAnimation* anim = animCtnr->m_animations[0];
	hkaAnimationBinding* binding = animCtnr->m_bindings[0];

	//To make this work with paired animations:
	//Very generally, we need a mapping of animation track index to a Bone* or Float*.
	//Even better, a mapping from track index to a BoneTrack* or FloatTrack*.
	std::vector<BoneTrack*> bones(anim->m_numberOfTransformTracks, nullptr);
	std::vector<FloatTrack*> floats(anim->m_numberOfFloatTracks, nullptr);
	//This mapping will be done very differently depending on whether 
	//binding->m_originalSkeletonName is "PairedRoot" or not.
	if (binding->m_originalSkeletonName == "PairedRoot")
		mapPaired(binding, skeletons, bones, floats);
	else
		mapSingle(binding, skeletons, bones, floats);

	//Abort if the animation could not be mapped
	if (m_data.clips.empty())
		return;

	//Set frame count, framerate, blend mode
	m_data.frames = static_cast<int>(std::round(anim->m_duration * FRAME_RATE)) + 1;
	m_data.frameRate = FRAME_RATE;
	m_data.additive = binding->m_blendHint == hkaAnimationBinding::ADDITIVE;

	//Init the key arrays
	for (int i = 0; i < anim->m_numberOfTransformTracks; i++) {
		if (bones[i])
			bones[i]->keys.setSize(m_data.frames);
	}
	for (int i = 0; i < anim->m_numberOfFloatTracks; i++) {
		if (floats[i])
			floats[i]->keys.setSize(m_data.frames);
	}

	//Sample animation and transfer keys
	hkArray<hkQsTransform> tmpT(anim->m_numberOfTransformTracks);
	hkArray<hkReal> tmpF(anim->m_numberOfFloatTracks);
	for (int f = 0; f < m_data.frames; f++) {
		anim->sampleTracks((float)f / FRAME_RATE, tmpT.begin(), tmpF.begin(), HK_NULL);

		for (int i = 0; i < tmpT.getSize(); i++) {
			//convert to bone-space,
			//=inverse of parent-space ref pose * sample pose
			if (bones[i]) {
				assert(bones[i]->target);

				hkQsTransform inv;
				inv.setInverse(bones[i]->target->refPose);
				tmpT[i].setMul(inv, tmpT[i]);
			}
		}

		hkaSkeletonUtils::normalizeRotations(tmpT.begin(), tmpT.getSize());

		for (int i = 0; i < anim->m_numberOfTransformTracks; i++) {
			if (bones[i])
				bones[i]->keys[f] = tmpT[i];
		}
		for (int i = 0; i < anim->m_numberOfFloatTracks; i++) {
			if (floats[i])
				floats[i]->keys[f] = tmpF[i];
		}
	}

	//Postprocess: if *every* key in a track is identical, we only need one.
	//Worth the trouble? This is not a performance-dependent application.
	for (int t = 0; t < anim->m_numberOfTransformTracks; t++) {

		if (bones[t]) {
			bool clean = true;
			for (int f = 1; f < m_data.frames; f++) {
				if (!bones[t]->keys[f].isApproximatelyEqual(bones[t]->keys[f - 1], 0.0f)) {
					clean = false;
					break;
				}
			}
			if (clean)
				bones[t]->keys.setSize(1);
		}
	}
	for (int t = 0; t < anim->m_numberOfFloatTracks; t++) {

		if (floats[t]) {
			bool clean = true;
			for (int f = 1; f < m_data.frames; f++) {
				if (floats[t]->keys[f] != floats[t]->keys[f - 1]) {
					clean = false;
					break;
				}
			}
			if (clean)
				floats[t]->keys.setSize(1);
		}
	}
}

void iohkx::AnimationDecoder::mapPairedComp(
	hkaAnimationBinding* binding,
	hkaAnimation* animation,
	std::vector<std::pair<BoneTrack*, Bone*>>& bones,
	std::vector<FloatTrack*>& floats)
{
	assert(binding && m_data.clips.size() == 2);

	bones.clear();
	floats.clear();

	if (m_data.additive)
		//I've never seen this in paired, don't know how to deal with it
		return;

	binding->m_originalSkeletonName = "PairedRoot";

	Clip& primary = m_data.clips.front();
	Clip& secondary = m_data.clips.back();

	//Include all bones in both skeletons, their root bones and the paired root
	int nBones = primary.skeleton->nBones + secondary.skeleton->nBones + 3;
	//and any addenda
	//nBones += primary.addenda.size() + secondary.addenda.size();

	//Some paired anims have float tracks, but I don't know how to interpret that.
	//Leave them for now.
	int nFloats = 0;

	//Fill out the targets and annotation names
	bones.resize(nBones);
	animation->m_annotationTracks.setSize(nBones);
	auto&& annotations = animation->m_annotationTracks;

	//PairedRoot (has neither track nor bone)
	bones[0].first = nullptr;
	bones[0].second = nullptr;
	annotations[0].m_trackName = "PairedRoot";

	//I don't know if the order here matters. Let's assume not.

	//Primary actor
	// root
	bones[1].first = primary.rootTransform;
	bones[1].second = primary.skeleton->rootBone;
	annotations[1].m_trackName = ROOT_BONE;
	int current = 2;
	// bones
	for (int i = 0; i < primary.skeleton->nBones; i++) {
		bones[current].first = primary.boneMap[i];
		bones[current].second = &primary.skeleton->bones[i];
		annotations[current].m_trackName = bones[current].second->name.c_str();
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
	bones[current].first = secondary.rootTransform;
	bones[current].second = secondary.skeleton->rootBone;
	setSecondaryRootName(secondary.skeleton->rootBone->name,
		secondary.skeleton, annotations[current]);
	++current;
	// bones
	for (int i = 0; i < secondary.skeleton->nBones; i++) {
		bones[current].first = secondary.boneMap[i];
		bones[current].second = &secondary.skeleton->bones[i];
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
}

void iohkx::AnimationDecoder::mapSingleComp(
	hkaAnimationBinding* binding,
	hkaAnimation* animation,
	std::vector<std::pair<BoneTrack*, Bone*>>& bones,
	std::vector<FloatTrack*>& floats)
{
	assert(binding && !m_data.clips.empty());

	Clip& clip = m_data.clips.front();

	binding->m_originalSkeletonName = clip.skeleton->name.c_str();

	//In additive mode, we'll include whatever tracks the user exported.
	//In normal mode, we'll fill all missing tracks with the bind pose.
	int nBones = m_data.additive ? clip.nBoneTracks : clip.skeleton->nBones;

	//We'll include whatever float tracks the user exported.
	int nFloats = clip.nFloatTracks;

	//Unrelated tracks should have been filtered out earlier
	assert(nBones <= clip.skeleton->nBones && nFloats <= clip.skeleton->nFloats);

	//If bones are missing, we need the mapping from clip.boneTracks to bone index.
	if (nBones < clip.skeleton->nBones) {
		assert(nBones == clip.nBoneTracks);
		binding->m_transformTrackToBoneIndices.reserve(nBones);

		//sort by bone index
		for (int i = 0; i < clip.skeleton->nBones; i++) {
			if (clip.boneMap[i]) {
				assert(clip.boneMap[i]->target->index == i);//or we messed up the map
				binding->m_transformTrackToBoneIndices.pushBack(i);
			}
		}
		assert(binding->m_transformTrackToBoneIndices.getSize() == nBones);
	}
	//Same with floats
	if (nFloats < clip.skeleton->nFloats) {
		assert(nFloats == clip.nFloatTracks);
		binding->m_floatTrackToFloatSlotIndices.reserve(nFloats);

		//sort by bone index
		for (int i = 0; i < clip.skeleton->nFloats; i++) {
			if (clip.floatMap[i]) {
				assert(clip.floatMap[i]->target->index == i);//or we messed up the map
				binding->m_floatTrackToFloatSlotIndices.pushBack(i);
			}
		}
		assert(binding->m_floatTrackToFloatSlotIndices.getSize() == nFloats);
	}

	bones.resize(nBones, { nullptr, nullptr });
	floats.resize(nFloats, nullptr);

	//Fill out bones and floats in whatever order we want it in the final animation,
	//i.e. in skeleton order
	
	//only correct if nBones == skeleton->nBones! Fix later.
	assert(nBones == clip.skeleton->nBones);
	for (int i = 0; i < nBones; i++) {
		bones[i].first = clip.boneMap[i];//may be null
		bones[i].second = &clip.skeleton->bones[i];
	}
	for (int i = 0; i < nFloats; i++) {
		floats[i] = clip.floatMap[i];//may be null
	}

	//Initialise annotation tracks
	animation->m_annotationTracks.setSize(nBones);
	for (int i = 0; i < nBones; i++)
		//the strings are not initialised
		animation->m_annotationTracks[i].m_trackName = "";
}

void iohkx::AnimationDecoder::mapPaired(
	hkaAnimationBinding* binding,
	const std::vector<Skeleton*>& skeletons,
	std::vector<BoneTrack*>& bones,
	std::vector<FloatTrack*>& floats)
{
	assert(binding);

	hkaAnimation* animation = binding->m_animation;

	//We expect all paired animations to have transform tracks
	if (!animation || animation->m_numberOfTransformTracks == 0)
		return;

	//We need annotations to map the tracks. Nothing to do if they are missing.
	if (animation->m_numberOfTransformTracks != animation->m_annotationTracks.getSize())
		return;

	//There must be at least one skeleton, but we can't say anything about
	//bone counts at this time.
	if (skeletons.empty() || skeletons[0] == nullptr)
		return;

	m_data.clips.resize(2);

	//If there is only one skeleton, we use it for both clips
	if (skeletons.size() == 1) {
		m_data.clips[0].skeleton = skeletons[0];
		m_data.clips[1].skeleton = skeletons[0];
	}
	else {
		m_data.clips[0].skeleton = skeletons[0];
		m_data.clips[1].skeleton = skeletons[1];
	}

	//Identify target bone by annotations
	//We'll do bone name lookup to map track index to a bone
	std::vector<std::pair<int, Bone*>> maps[2];//(track, bone)
	maps[0].reserve(animation->m_numberOfTransformTracks);
	maps[1].reserve(animation->m_numberOfTransformTracks);

	std::pair<int, Bone*> roots[2]{ { -1, nullptr }, { -1, nullptr } };

	std::vector<std::pair<int, Bone*>> addenda[2];

	for (int i = 0; i < animation->m_numberOfTransformTracks; i++) {
		//get (clip index, bone) by name
		auto&& name = animation->m_annotationTracks[i].m_trackName;
		auto pair = findBone(name.cString(), m_data.clips);

		if (pair.first == 0 || pair.first == 1) {
			Clip& clip = m_data.clips[pair.first];
			if (pair.second) {
				//We found a bone
				if (pair.second == clip.skeleton->rootBone) {

					//store the root immediately
					assert(!clip.rootTransform);
					clip.rootTransform = new BoneTrack;
					clip.rootTransform->target = clip.skeleton->rootBone;
					bones[i] = clip.rootTransform;
				}
				else
					maps[pair.first].push_back({ i, pair.second });
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

	for (unsigned int i = 0; i < m_data.clips.size(); i++) {
		//Allocate the tracks
		int nTracks = maps[i].size();
		m_data.clips[i].nBoneTracks = nTracks;
		m_data.clips[i].boneTracks = new BoneTrack[nTracks];
		m_data.clips[i].boneMap.resize(m_data.clips[i].skeleton->nBones, nullptr);

		//Some paired anims have float tracks, but I don't know how to interpret that.
		//Leave them for now.
		m_data.clips[i].nFloatTracks = 0;
		m_data.clips[i].floatTracks = nullptr;
		m_data.clips[i].floatMap.resize(m_data.clips[i].skeleton->nFloats, nullptr);

		for (int j = 0; j < nTracks; j++) {
			//Set the track targets
			BoneTrack* track = &m_data.clips[i].boneTracks[j];
			track->target = maps[i][j].second;
			m_data.clips[i].boneMap[track->target->index] = track;

			//Final mapping
			bones[maps[i][j].first] = track;
		}
	}
}

void iohkx::AnimationDecoder::mapSingle(
	hkaAnimationBinding* binding,
	const std::vector<Skeleton*>& skeletons,
	std::vector<BoneTrack*>& bones,
	std::vector<FloatTrack*>& floats)
{
	assert(binding);

	//Check validity of the skeleton first
	//Skyrim has the same name for all skeletons, so we can't rely on that.
	//If it has fewer slots than we have tracks, it's definitely the wrong one.
	//If it has *more* slots, it might still be correct.
	if (skeletons.empty()
		|| skeletons[0] == nullptr
		|| skeletons[0]->nBones < (int)bones.size()
		|| skeletons[0]->nFloats < (int)floats.size())
		return;

	//There will be only one clip
	m_data.clips.resize(1);
	Clip& clip = m_data.clips.front();

	//Point it to its skeleton
	clip.skeleton = skeletons[0];

	//Allocate the tracks (also 1:1 for a single-skeleton animation)
	clip.nBoneTracks = bones.size();
	clip.boneTracks = new BoneTrack[clip.nBoneTracks];

	clip.nFloatTracks = floats.size();
	clip.floatTracks = new FloatTrack[clip.nFloatTracks];

	//Assign the track targets
	if (binding->m_transformTrackToBoneIndices.isEmpty()) {
		//The tracks should map 1:1 with the skeleton
		for (int i = 0; i < clip.nBoneTracks; i++) {
			clip.boneTracks[i].target = &skeletons[0]->bones[i];
		}
	}
	else {
		//Map from the binding
		for (int i = 0; i < clip.nBoneTracks; i++) {
			int index = binding->m_transformTrackToBoneIndices[i];
			clip.boneTracks[i].target = &skeletons[0]->bones[index];
		}
	}
	if (binding->m_floatTrackToFloatSlotIndices.isEmpty()) {
		//The tracks should map 1:1 with the skeleton
		for (int i = 0; i < clip.nFloatTracks; i++) {
			clip.floatTracks[i].target = &skeletons[0]->floats[i];
		}
	}
	else {
		//Map from the binding
		for (int i = 0; i < clip.nFloatTracks; i++) {
			int index = binding->m_floatTrackToFloatSlotIndices[i];
			clip.floatTracks[i].target = &skeletons[0]->floats[index];
		}
	}

	//Do the final mapping (in this case just a copy of the track list)
	for (int i = 0; i < clip.nBoneTracks; i++) {
		bones[i] = &clip.boneTracks[i];
	}
	for (int i = 0; i < clip.nFloatTracks; i++) {
		floats[i] = &clip.floatTracks[i];
	}
}

void iohkx::AnimationDecoder::preProcess()
{
	hkQsTransform I(hkQsTransform::IDENTITY);
	for (auto&& clip : m_data.clips) {

		//Transform all bone transforms to parent space
		//(we expect them to be in object space now)
		for (int f = 0; f < m_data.frames; f++) {
			objToParent(clip.skeleton->rootBone, clip, f, I, I);
		}

		//set all rotations to the shortest distance from previous key
		sanitiseQuats(clip);
	}
}
