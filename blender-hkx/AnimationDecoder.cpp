#include "pch.h"
#include "AnimationDecoder.h"

#define FRAME_RATE 30

using namespace iohkx;

static inline hkVector4 rawToVec(const float* raw)
{
	return hkVector4(raw[0], raw[1], raw[2]);
}

static inline hkQuaternion rawToQuat(const float* raw)
{
	return hkQuaternion(raw[1], raw[2], raw[3], raw[0]);
}

static inline void vecToRaw(const hkVector4& vec, float* raw)
{
	raw[0] = vec(0);
	raw[1] = vec(1);
	raw[2] = vec(2);
}

static inline void quatToRaw(const hkQuaternion& q, float* raw)
{
	raw[0] = q(3);
	raw[1] = q(0);
	raw[2] = q(1);
	raw[3] = q(2);
}

iohkx::AnimationDecoder::AnimationDecoder()
{}

iohkx::AnimationDecoder::~AnimationDecoder()
{
	for (unsigned int i = 0; i < m_data.clips.size(); i++) {
		delete[] m_data.clips[i].boneTracks;
		delete[] m_data.clips[i].floatTracks;
	}
}

void iohkx::AnimationDecoder::mapPaired(
			hkaAnimationBinding* binding, 
			const std::vector<Skeleton*>& skeletons, 
			std::vector<BoneTrack*>& bones, 
			std::vector<FloatTrack*>& floats)
{
	assert(binding);

	//There must be at least one skeleton, but we can't say anything about
	//bone counts at this time.
	//Well, we can say that the number of tracks must not exceed the number
	//of bones in both skeletons + 3 (two extra roots and the paired root)
	if (skeletons.empty() || skeletons[0] == NULL)
		return;

	//If there is only one skeleton, it's for both clips
	if (skeletons.size() == 1) {
		//The number of tracks should not exceed 2 * nBones + 3.
		//It should equal that, but that may not always be true.
		//Let's assume it will, for now.
		if (bones.size() != 2 * skeletons[0]->nBones + 3)
			return;

		m_data.clips.resize(2);
		m_data.clips[0].skeleton = skeletons[0];
		m_data.clips[1].skeleton = skeletons[0];
	}
	//If there are two skeletons, do we assume that they are in order?
	//Or do we verify that by looking through the annotations?
	//Let's not bother verifying, for now.
	else {
		if (bones.size() != skeletons[0]->nBones + skeletons[1]->nBones + 3)
			return;

		m_data.clips.resize(2);
		m_data.clips[0].skeleton = skeletons[0];
		m_data.clips[1].skeleton = skeletons[1];
	}

	//Allocate the tracks
	for (unsigned int i = 0; i < m_data.clips.size(); i++) {
		//Assume that all bones are included in the animation
		assert(bones.size() == m_data.clips[0].skeleton->nBones 
				+ m_data.clips[1].skeleton->nBones + 3);

		//number of tracks is number of bones + 1 (the root)
		m_data.clips[i].nBoneTracks = m_data.clips[i].skeleton->nBones + 1;
		m_data.clips[i].boneTracks = new BoneTrack[m_data.clips[i].nBoneTracks];

		//Some paired anims have float tracks, but I don't know how to interpret that.
		//Leave them for now.
		m_data.clips[i].nFloatTracks = 0;
		m_data.clips[i].floatTracks = NULL;
	}

	//Set the track targets
	//track 0 is unused
	//clip 1 comes first
	//track 1 is the root bone of skeleton 1
	m_data.clips[1].boneTracks[0].target = m_data.clips[1].skeleton->rootBone;
	//then follows the bones of skeleton 1 in order
	for (int i = 1; i < m_data.clips[1].nBoneTracks; i++) {
		m_data.clips[1].boneTracks[i].target = &m_data.clips[1].skeleton->bones[i - 1];
	}
	//clip 0 starts here
	//first the root bone of skeleton 0
	m_data.clips[0].boneTracks[0].target = m_data.clips[0].skeleton->rootBone;
	//then the bones of skeleton 0 in order
	for (int i = 1; i < m_data.clips[0].nBoneTracks; i++) {
		m_data.clips[0].boneTracks[i].target = &m_data.clips[0].skeleton->bones[i - 1];
	}

	//Finally the mapping
	//track 0 is unused
	bones[0] = NULL;
	int outer = 1;
	//first all the tracks in clip 1
	for (int i = 0; i < m_data.clips[1].nBoneTracks; i++, outer++) {
		bones[outer] = &m_data.clips[1].boneTracks[i];
	}
	//then all the tracks in clip 0
	for (int i = 0; i < m_data.clips[0].nBoneTracks; i++, outer++) {
		bones[outer] = &m_data.clips[0].boneTracks[i];
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
		|| skeletons[0] == NULL
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

void iohkx::AnimationDecoder::decompress(
	hkaAnimationContainer* animCtnr, const std::vector<Skeleton*>& skeletons)
{
	//Init to a meaningful state, whether we are being reused or not
	m_data.frames = 0;
	m_data.frameRate = 30;
	m_data.blendMode.clear();
	m_data.clips.clear();

	//Abort if there is no data
	if (!animCtnr || animCtnr->m_animations.isEmpty() || animCtnr->m_bindings.isEmpty())
		return;

	hkaAnimation* anim = animCtnr->m_animations[0];
	hkaAnimationBinding* binding = animCtnr->m_bindings[0];

	//To make this work with paired animations:
	//Very generally, we need a mapping of animation track index to a Bone* or Float*.
	//Even better, a mapping from track index to a BoneTrack* or FloatTrack*.
	std::vector<BoneTrack*> bones(anim->m_numberOfTransformTracks, NULL);
	std::vector<FloatTrack*> floats(anim->m_numberOfFloatTracks, NULL);
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
	bool additive = binding->m_blendHint == hkaAnimationBinding::ADDITIVE;
	m_data.blendMode = additive ? "ADDITIVE" : "NORMAL";

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


hkRefPtr<hkaAnimationContainer> iohkx::AnimationDecoder::compress(
	const AnimationData& data, const Skeleton& skeleton)
{
	return hkRefPtr<hkaAnimationContainer>();
	/*Let's move this out of the way for now (it's broken anyway)
	//Validate animation data
	if (data.frames < 1)
		throw Exception(ERR_INVALID_INPUT, "No frames");
	if (data.frameRate != 30)
		throw Exception(ERR_INVALID_INPUT, "Invalid frame rate");
	for (unsigned int i = 0; i < data.bones.size(); i++) {
		if (data.bones[i].keys.size() != data.frames)
			throw Exception(ERR_INVALID_INPUT, "Missing keys");
	}

	bool additive = data.blendMode == "ADDITIVE";
	
	//In additive mode, we expect the XML to only contain
	//bones with non-identity transforms on at least one frame.
	//In normal mode, we fill all missing tracks with the bind pose.
	int nBones = additive ? data.bones.size() : skeleton.bones.size();
	
	//We only ever include float tracks with exported data in them
	int nFloats = data.floats.size();

	//Initialise raw animation object
	hkRefPtr<hkaInterleavedUncompressedAnimation> raw = 
		new hkaInterleavedUncompressedAnimation;
	raw->removeReference();

	raw->m_duration = static_cast<float>(data.frames - 1) / data.frameRate;
	raw->m_numberOfTransformTracks = nBones;
	raw->m_numberOfFloatTracks = nFloats;
	raw->m_transforms.setSize(nBones * data.frames);
	raw->m_floats.setSize(nFloats * data.frames);
	raw->m_annotationTracks.setSize(nBones);
	for (int i = 0; i < nBones; i++)
		//the strings are not initialised
		raw->m_annotationTracks[i].m_trackName = "";

	//Initialise binding
	hkaAnimationBinding* binding = new hkaAnimationBinding;
	binding->m_originalSkeletonName = skeleton.name.c_str();
	binding->m_blendHint = 
		additive ? hkaAnimationBinding::ADDITIVE : hkaAnimationBinding::NORMAL;
	
	std::vector<int> dataToBoneTrack;
	if (nBones && (unsigned int)nBones < skeleton.bones.size()) {
		binding->m_transformTrackToBoneIndices.setSize(nBones);
		//The pedant in me wants the entries in m_transformTrackToBoneIndices 
		//to be sorted in order (probably doesn't matter at all).
		//This means we must provide a mapping from the indices in
		//data.bones to track indices.
		dataToBoneTrack.resize(nBones);
		
		std::vector<int> temp(skeleton.bones.size(), -1);
		for (int i = 0; i < (int)data.bones.size(); i++)
			//temp is the mapping from bone index to data index
			temp[data.bones[i].index] = i;

		int at = 0;
		for (int i = 0; i < (int)temp.size(); i++) {
			//i is the bone index
			if (temp[i] != -1) {
				binding->m_transformTrackToBoneIndices[at] = i;
				dataToBoneTrack[temp[i]] = at++;
			}
		}
	}

	std::vector<int> dataToFloatTrack;
	if (nFloats && (unsigned int)nFloats < skeleton.floats.size()) {
		binding->m_floatTrackToFloatSlotIndices.setSize(nFloats);
		//dito
		dataToFloatTrack.resize(nFloats);

		std::vector<int> temp(skeleton.floats.size(), -1);
		for (int i = 0; i < (int)data.floats.size(); i++)
			temp[data.floats[i].index] = i;

		int at = 0;
		for (int i = 0; i < (int)temp.size(); i++) {
			if (temp[i] != -1) {
				binding->m_floatTrackToFloatSlotIndices[at] = i;
				dataToFloatTrack[temp[i]] = at++;
			}
		}
	}

	if (!additive && (int)data.bones.size() < nBones) {
		//Some tracks are missing. Probably only a few, so let's find them.
		std::vector<bool> fill(nBones, true);
		for (unsigned int i = 0; i < data.bones.size(); i++)
			fill[data.bones[i].index] = false;

		for (int bone = 0; bone < nBones; bone++) {
			if (fill[bone]) {
				for (int frame = 0; frame < data.frames; frame++)
					raw->m_transforms[bone + frame * nBones] = skeleton.bones[bone]->refPose;
			}
		}
	}

	//Transfer data to raw anim
	for (unsigned int i = 0; i < data.bones.size(); i++) {

		int track = dataToBoneTrack.empty() ? data.bones[i].index : dataToBoneTrack[i];

		for (int frame = 0; frame < data.frames; frame++) {
			const Transform& src = data.bones[i].keys[frame];
			hkQsTransform& dst = raw->m_transforms[track + frame * nBones];
			dst.setTranslation(hkVector4(src.T[0], src.T[1], src.T[2]));
			dst.setRotation(hkQuaternion(src.R[0], src.R[1], src.R[2], src.R[3]));
			dst.setScale(hkVector4(src.S[0], src.S[1], src.S[2]));
		}
	}
	
	for (unsigned int i = 0; i < data.floats.size(); i++) {
		int track = dataToFloatTrack.empty() ? data.floats[i].index : dataToFloatTrack[i];
		for (int frame = 0; frame < data.frames; frame++)
			raw->m_floats[track + frame * nFloats] = data.floats[i].keys[frame];
	}

	hkaSkeletonUtils::normalizeRotations(raw->m_transforms.begin(), raw->m_transforms.getSize());

	hkaSplineCompressedAnimation::AnimationCompressionParams acp;
	//acp.m_enableSampleSingleTracks = true;//enable for paired anims!
	
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
	*/
}

