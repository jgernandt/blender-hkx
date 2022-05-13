#include "pch.h"
#include "AnimationDecoder.h"
#include "TrackMapper.h"

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

static bool mapBonesIfMissing(int n, Clip& clip, hkArray<hkInt16>& target)
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

static bool mapFloatsIfMissing(int n, Clip& clip, hkArray<hkInt16>& target)
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

struct iohkx::AnimationDecoder::CompressionMap
{
	//bones[i], floats[i] will be the data for output track i
	// (the pair is so that we can find the bone (to fill with ref pose)
	//  if there is no track for it)
	std::vector<std::pair<BoneTrack*, Bone*>> bones;
	std::vector<FloatTrack*> floats;
	int annotationTracks[2]{ -1, -1 };
};

struct iohkx::AnimationDecoder::DecompressionMap
{
	std::vector<BoneTrack*> bones;
	std::vector<FloatTrack*> floats;
	int annotationClip{ -1 };
	int annotationTrack{ -1 };
};

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

hkRefPtr<hkaAnimationContainer> iohkx::AnimationDecoder::compress()
{
	if (m_data.frames < 1 || m_data.clips.empty())
		//Nothing to compress
		return hkRefPtr<hkaAnimationContainer>();

	if (m_data.frameRate != FRAME_RATE)
		throw Exception(ERR_INVALID_INPUT, "Unsupported frame rate");

	preProcess();

	//Create binding (will be filled out by map*Comp)
	hkaAnimationBinding* binding = new hkaAnimationBinding;

	//Initialise raw animation object
	hkRefPtr<hkaInterleavedUncompressedAnimation> raw =
		new hkaInterleavedUncompressedAnimation;
	raw->removeReference();

	//Map the source data to the hkaAnimation in a way that works for both 
	//single and paired animations
	CompressionMap map;
	if (m_data.clips.size() == 2) {
		mapPairedComp(binding, raw, map);
	}
	else if (m_data.clips.size() == 1) {
		mapSingleComp(binding, raw, map);
	}

	//The mapping functions should have determined how many tracks we need
	int nBones = map.bones.size();
	int nFloats = map.floats.size();

	if (nBones == 0 && nFloats == 0)
		//Nothing to export
		return hkRefPtr<hkaAnimationContainer>();

	raw->m_duration = static_cast<float>(m_data.frames - 1) / m_data.frameRate;
	raw->m_numberOfTransformTracks = nBones;
	raw->m_numberOfFloatTracks = nFloats;
	raw->m_transforms.setSize(nBones * m_data.frames);
	raw->m_floats.setSize(nFloats * m_data.frames);

	//Transfer data to raw anim

	//bone tracks
	for (int i = 0; i < nBones; i++) {
		BoneTrack* track = map.bones[i].first;
		if (track) {
			auto&& keys = track->keys;

			//What if there are no keys? How would we deal with it?
			if (keys.isEmpty())
				throw Exception(ERR_INVALID_INPUT, "Track with no keys");

			for (int f = 0; f < m_data.frames; f++) {
				//Use current key, or last if none
				raw->m_transforms[i + f * nBones] = keys.getSize() > f ? keys[f] : keys.back();
			}
		}
		else if (!m_data.additive) {
			//fill with ref pose (or identity if we have no bone)

			for (int f = 0; f < m_data.frames; f++) {
				if (map.bones[i].second) {
					raw->m_transforms[i + f * nBones] = map.bones[i].second->refPose;
				}
				else {
					raw->m_transforms[i + f * nBones].setIdentity();
				}
			}
		}
		//else ignore
	}
	//float tracks
	for (int i = 0; i < nFloats; i++) {
		FloatTrack* track = map.floats[i];
		if (track) {
			auto&& keys = track->keys;

			//What if there are no keys? How would we deal with it?
			if (keys.isEmpty())
				throw Exception(ERR_INVALID_INPUT, "Track with no keys");

			for (int f = 0; f < m_data.frames; f++) {
				//Use current key, or last if none
				raw->m_floats[i + f * nFloats] = keys.getSize() > f ? keys[f] : keys.back();
			}
		}
		//else ignore (never fill)
	}
	//annotations
	for (int i : { 0, 1 }) {
		if (map.annotationTracks[i] != -1) {
			for (auto&& anno : m_data.clips[i].annotations) {
				hkaAnnotationTrack::Annotation a;
				a.m_time = static_cast<float>(anno.frame) / FRAME_RATE;
				a.m_text = anno.text.c_str();
				raw->m_annotationTracks[map.annotationTracks[i]].m_annotations.pushBack(a);
			}
		}
	}

	hkaSkeletonUtils::normalizeRotations(raw->m_transforms.begin(), raw->m_transforms.getSize());

	hkaSplineCompressedAnimation::AnimationCompressionParams acp;
	if (m_data.clips.size() == 2)
		//paired animations need this (crash otherwise)
		acp.m_enableSampleSingleTracks = true;

	hkaSplineCompressedAnimation::TrackCompressionParams tcp;
	tcp.m_translationTolerance = 0.004f;
	tcp.m_rotationTolerance = 0.001f;
	tcp.m_scaleTolerance = 0.004f;
	tcp.m_floatingTolerance = 0.004f;

	tcp.m_translationQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::BITS8;
	tcp.m_rotationQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::THREECOMP40;
	tcp.m_scaleQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::BITS8;
	tcp.m_floatQuantizationType = hkaSplineCompressedAnimation::TrackCompressionParams::BITS8;

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

	//Map the source data to our AnimationData in a way that works for both 
	//single and paired animations
	DecompressionMap map;
	map.bones.resize(anim->m_numberOfTransformTracks, nullptr);
	map.floats.resize(anim->m_numberOfFloatTracks, nullptr);

	if (binding->m_originalSkeletonName == "PairedRoot")
		mapPaired(binding, skeletons, map);
	else
		mapSingle(binding, skeletons, map);

	//Abort if the animation could not be mapped
	if (m_data.clips.empty())
		return;

	//Set frame count, framerate, blend mode
	m_data.frames = static_cast<int>(std::round(anim->m_duration * FRAME_RATE)) + 1;
	m_data.frameRate = FRAME_RATE;
	m_data.additive = binding->m_blendHint == hkaAnimationBinding::ADDITIVE;

	//Init the key arrays
	for (int i = 0; i < anim->m_numberOfTransformTracks; i++) {
		if (map.bones[i])
			map.bones[i]->keys.setSize(m_data.frames);
	}
	for (int i = 0; i < anim->m_numberOfFloatTracks; i++) {
		if (map.floats[i])
			map.floats[i]->keys.setSize(m_data.frames);
	}

	//Sample animation and transfer keys
	hkArray<hkQsTransform> tmpT(anim->m_numberOfTransformTracks);
	hkArray<hkReal> tmpF(anim->m_numberOfFloatTracks);
	for (int f = 0; f < m_data.frames; f++) {
		anim->sampleTracks((float)f / FRAME_RATE, tmpT.begin(), tmpF.begin(), HK_NULL);

		for (int i = 0; i < tmpT.getSize(); i++) {
			//convert to bone-space,
			//=inverse of parent-space ref pose * sample pose
			if (map.bones[i]) {
				assert(map.bones[i]->target);

				hkQsTransform inv;
				inv.setInverse(map.bones[i]->target->refPose);
				tmpT[i].setMul(inv, tmpT[i]);
			}
		}

		hkaSkeletonUtils::normalizeRotations(tmpT.begin(), tmpT.getSize());

		for (int i = 0; i < anim->m_numberOfTransformTracks; i++) {
			if (map.bones[i])
				map.bones[i]->keys[f] = tmpT[i];
		}
		for (int i = 0; i < anim->m_numberOfFloatTracks; i++) {
			if (map.floats[i])
				map.floats[i]->keys[f] = tmpF[i];
		}
	}

	removeDuplicateKeys();

	//Annotations
	if (map.annotationClip != -1 && map.annotationTrack != -1) {
		for (auto&& item : anim->m_annotationTracks[map.annotationTrack].m_annotations) {
			m_data.clips[map.annotationClip].annotations.push_back({ 
				static_cast<int>(std::round(item.m_time * FRAME_RATE)),
				item.m_text.cString() });
		}
	}
}

void iohkx::AnimationDecoder::mapPairedComp(
	hkaAnimationBinding* binding,
	hkaAnimation* animation,
	CompressionMap& map)
{
	assert(binding && m_data.clips.size() == 2);

	map.bones.clear();
	map.floats.clear();

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

	//Some paired anims have float track. Let's assume they belong to the primary actor.
	int nFloats = primary.nFloatTracks;

	//Fill out the targets and annotation names
	map.bones.resize(nBones);
	map.floats.resize(nFloats);
	animation->m_annotationTracks.setSize(nBones);
	auto&& annotations = animation->m_annotationTracks;

	//Bones and annotations

	//PairedRoot (has neither track nor bone)
	map.bones[0].first = nullptr;
	map.bones[0].second = nullptr;
	annotations[0].m_trackName = "PairedRoot";

	//I don't know if the order here matters. Let's assume not.

	//Primary actor
	// root
	map.bones[1].first = primary.rootTransform;
	map.bones[1].second = primary.skeleton->rootBone;
	annotations[1].m_trackName = ROOT_BONE;
	int current = 2;
	//annotations go to bone 0
	map.annotationTracks[0] = current;
	// bones
	for (int i = 0; i < primary.skeleton->nBones; i++) {
		map.bones[current].first = primary.boneMap[i];
		map.bones[current].second = &primary.skeleton->bones[i];
		annotations[current].m_trackName = map.bones[current].second->name.c_str();
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
	map.bones[current].first = secondary.rootTransform;
	map.bones[current].second = secondary.skeleton->rootBone;
	setSecondaryRootName(secondary.skeleton->rootBone->name,
		secondary.skeleton, annotations[current]);
	++current;
	//annotations go to bone 0
	map.annotationTracks[1] = current;
	// bones
	for (int i = 0; i < secondary.skeleton->nBones; i++) {
		map.bones[current].first = secondary.boneMap[i];
		map.bones[current].second = &secondary.skeleton->bones[i];
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
		map.floats[i] = primary.floatMap[index];
	}
}

void iohkx::AnimationDecoder::mapSingleComp(
	hkaAnimationBinding* binding,
	hkaAnimation* animation,
	CompressionMap& map)
{
	assert(binding && !m_data.clips.empty());

	Clip& clip = m_data.clips.front();

	//Find the final track that represents bone 0!

	binding->m_originalSkeletonName = clip.skeleton->name.c_str();

	//In additive mode, we'll include whatever tracks the user exported.
	//In normal mode, we'll fill all missing tracks with the bind pose.
	int nBones = m_data.additive ? clip.nBoneTracks : clip.skeleton->nBones;

	//We'll include whatever float tracks the user exported.
	int nFloats = clip.nFloatTracks;

	//Unrelated tracks should have been filtered out earlier
	assert(nBones <= clip.skeleton->nBones && nFloats <= clip.skeleton->nFloats);

	//If bones are missing, we need the mapping from clip.boneTracks to bone index.
	bool missingBones = mapBonesIfMissing(nBones, clip, binding->m_transformTrackToBoneIndices);
	bool missingFloats = mapFloatsIfMissing(nFloats, clip, binding->m_floatTrackToFloatSlotIndices);

	/*if (missingBones) {
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
	if (missingFloats) {
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
	}*/

	map.bones.resize(nBones, { nullptr, nullptr });
	map.floats.resize(nFloats, nullptr);

	//Fill out bones and floats in whatever order we want it in the final animation,
	//i.e. in skeleton order
	for (int i = 0; i < nBones; i++) {
		int index = missingBones ? binding->m_transformTrackToBoneIndices[i] : i;
		map.bones[i].first = clip.boneMap[index];//may be null
		map.bones[i].second = &clip.skeleton->bones[index];

		if (index == 0) {
			//This track points to the annotation bone
			map.annotationTracks[0] = i;
		}
	}
	for (int i = 0; i < nFloats; i++) {
		int index = missingFloats ? binding->m_floatTrackToFloatSlotIndices[i] : i;
		map.floats[i] = clip.floatMap[index];//may be null
	}

	//Initialise annotation tracks
	animation->m_annotationTracks.setSize(nBones);
	for (int i = 0; i < nBones; i++)
		//(the strings are not initialised)
		animation->m_annotationTracks[i].m_trackName = "";
}

void iohkx::AnimationDecoder::mapPaired(
	hkaAnimationBinding* binding,
	const std::vector<Skeleton*>& skeletons,
	DecompressionMap& map)
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
	Clip& primary = m_data.clips[0];
	Clip& secondary = m_data.clips[1];

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
					map.bones[i] = clip.rootTransform;
				}
				else {
					maps[pair.first].push_back({ i, pair.second });

					//the annotations are in bone 0 of the primary skeleton (?)
					if (pair.second == &primary.skeleton->bones[0]) {
						map.annotationClip = 0;
						map.annotationTrack = i;
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

	for (unsigned int i = 0; i < m_data.clips.size(); i++) {
		//Allocate the tracks
		int nTracks = maps[i].size();
		m_data.clips[i].nBoneTracks = nTracks;
		m_data.clips[i].boneTracks = new BoneTrack[nTracks];
		m_data.clips[i].boneMap.resize(m_data.clips[i].skeleton->nBones, nullptr);

		//Some paired anims have float tracks, but I don't know how to interpret that.
		//Leave them for now.
		//m_data.clips[i].nFloatTracks = 0;
		//m_data.clips[i].floatTracks = nullptr;
		//m_data.clips[i].floatMap.resize(m_data.clips[i].skeleton->nFloats, nullptr);

		for (int j = 0; j < nTracks; j++) {
			//Set the track targets
			BoneTrack* track = &m_data.clips[i].boneTracks[j];
			track->target = maps[i][j].second;
			m_data.clips[i].boneMap[track->target->index] = track;

			//Final mapping
			map.bones[maps[i][j].first] = track;
		}
	}

	//Some paired anims have float tracks. Let's assume they belong to the primary actor,
	//if there are slots for them.

	int nFloats = map.floats.size();

	//Does the number of tracks match the number of slots?
	bool validFloats = nFloats == primary.skeleton->nFloats;

	//If not, do we have a valid mapping?
	if (!validFloats && binding->m_floatTrackToFloatSlotIndices.getSize() == nFloats) {

		validFloats = true;

		//make sure no index exceeds the number of slots
		for (int i = 0; i < nFloats; i++) {
			if (binding->m_floatTrackToFloatSlotIndices[i] >= primary.skeleton->nFloats) {
				validFloats = false;
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

			map.floats[i] = &primary.floatTracks[i];
		}
	}
}

void iohkx::AnimationDecoder::mapSingle(
	hkaAnimationBinding* binding,
	const std::vector<Skeleton*>& skeletons,
	DecompressionMap& map)
{
	assert(binding);

	//Check validity of the skeleton first
	//Skyrim has the same name for all skeletons, so we can't rely on that.
	//If it has fewer slots than we have tracks, it's definitely the wrong one.
	//If it has *more* slots, it might still be correct.
	if (skeletons.empty()
		|| skeletons[0] == nullptr
		|| skeletons[0]->nBones < (int)map.bones.size()
		|| skeletons[0]->nFloats < (int)map.floats.size())
		return;

	//There will be only one clip
	m_data.clips.resize(1);
	Clip& clip = m_data.clips.front();
	map.annotationClip = 0;

	//Point it to its skeleton
	clip.skeleton = skeletons[0];

	//Allocate the tracks
	clip.nBoneTracks = map.bones.size();
	clip.boneTracks = new BoneTrack[clip.nBoneTracks];
	//we don't need this map here
	//clip.boneMap.resize(clip.skeleton->nBones, nullptr);

	clip.nFloatTracks = map.floats.size();
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
		map.annotationTrack = 0;
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
				map.annotationTrack = i;
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
		map.bones[i] = &clip.boneTracks[i];
	}
	for (int i = 0; i < clip.nFloatTracks; i++) {
		map.floats[i] = &clip.floatTracks[i];
	}
}

void iohkx::AnimationDecoder::removeDuplicateKeys()
{
	//If all keys are equal, keep only one
	for (auto&& clip : m_data.clips) {
		//bones
		for (int i = 0; i < clip.nBoneTracks; i++) {
			auto&& keys = clip.boneTracks[i].keys;
			bool clean = true;
			for (int f = 1; f < m_data.frames; f++) {
				if (!keys[f].isApproximatelyEqual(keys[f - 1], 0.0f)) {
					clean = false;
					break;
				}
			}
			if (clean)
				keys.setSize(1);
		}
		//floats
		for (int i = 0; i < clip.nFloatTracks; i++) {
			auto&& keys = clip.floatTracks[i].keys;
			bool clean = true;
			for (int f = 1; f < m_data.frames; f++) {
				if (keys[f] != keys[f - 1]) {
					clean = false;
					break;
				}
			}
			if (clean)
				keys.setSize(1);
		}
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
