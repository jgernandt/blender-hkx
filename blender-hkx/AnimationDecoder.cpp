#include "pch.h"
#include "AnimationDecoder.h"

#define FRAME_RATE 30

using namespace iohkx;

//Do we really not have round? How old is this s***?
double round(double x)
{
    return x >= 0.0 ? ceil(x - 0.5) : floor(x + 0.5);
}
float round(float x)
{
    return x >= 0.0f ? ceilf(x - 0.5f) : floorf(x + 0.5f);
}

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

hkRefPtr<hkaAnimationContainer> iohkx::AnimationDecoder::compress(
	const AnimationData& data, const Skeleton& skeleton)
{
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
}

void iohkx::AnimationDecoder::decompress(
	hkaAnimationContainer* animCtnr, const Skeleton& skeleton, AnimationData& data)
{
	assert(animCtnr);
	if (animCtnr->m_animations.getSize() == 0 || animCtnr->m_bindings.getSize() == 0)
		throw Exception(ERR_INVALID_INPUT, "No animations");

	hkaAnimation* anim = animCtnr->m_animations[0];
	hkaAnimationBinding* binding = animCtnr->m_bindings[0];

	//Transfer the basics
	data.frames = static_cast<int>(round(anim->m_duration * FRAME_RATE)) + 1;
	data.frameRate = FRAME_RATE;
	bool additive = binding->m_blendHint == hkaAnimationBinding::ADDITIVE;
	data.blendMode = additive ? "ADDITIVE" : "NORMAL";
	data.bones.resize(anim->m_numberOfTransformTracks);

	//Set track-to-bone index
	for (int i = 0; (unsigned int)i < data.bones.size(); i++) {
		data.bones[i].index = binding->m_transformTrackToBoneIndices.isEmpty() ? 
			i : binding->m_transformTrackToBoneIndices[i];
		data.bones[i].keys.resize(data.frames);
	}
	//Set track-to-float index
	data.floats.resize(anim->m_numberOfFloatTracks);
	for (int i = 0; (unsigned int)i < data.floats.size(); i++) {
		data.floats[i].index = binding->m_floatTrackToFloatSlotIndices.isEmpty() ? 
			i : binding->m_floatTrackToFloatSlotIndices[i];
		data.floats[i].keys.resize(data.frames);
	}

	//Set bone-to-track index (if required)
	if (data.bones.size() < skeleton.bones.size()) {

		data.boneToTrack.resize(skeleton.bones.size(), -1);

		for (int i = 0; (unsigned int)i < data.bones.size(); i++)
			data.boneToTrack[data.bones[i].index] = i;
	}
	//Set float-to-track index (if required)
	if (data.floats.size() < skeleton.floats.size()) {

		data.floatToTrack.resize(skeleton.floats.size(), -1);

		for (int i = 0; (unsigned int)i < data.floats.size(); i++)
			data.floatToTrack[data.floats[i].index] = i;
	}

	//Check that we have the right skeleton
	if (strcmp(binding->m_originalSkeletonName.cString(), skeleton.name.c_str()) != 0)
		throw Exception(ERR_INVALID_INPUT, "Mismatched skeleton");

	data.skeleton = &skeleton;

	//Sample animation and transfer keys
	hkArray<hkQsTransform> tmpT(data.bones.size());
	hkArray<hkReal> tmpF(data.floats.size());
	for (int f = 0; f < data.frames; f++) {
		anim->sampleTracks((float)f / FRAME_RATE, tmpT.begin(), tmpF.begin(), HK_NULL);

		for (unsigned int i = 0; i < data.bones.size(); i++) {
			//convert to bone-space,
			//=inverse of parent-space ref pose * sample pose
			hkQsTransform inv;
			inv.setInverse(skeleton.bones[data.bones[i].index]->refPose);
			tmpT[i].setMul(inv, tmpT[i]);
		}

		hkaSkeletonUtils::normalizeRotations(tmpT.begin(), tmpT.getSize());

		for (unsigned int i = 0; i < data.bones.size(); i++) {
			vecToRaw(tmpT[i].getTranslation(), data.bones[i].keys[f].T);
			quatToRaw(tmpT[i].getRotation(), data.bones[i].keys[f].R);
			vecToRaw(tmpT[i].getScale(), data.bones[i].keys[f].S);
		}

		for (unsigned int i = 0; i < data.floats.size(); i++)
			data.floats[i].keys[f] = tmpF[i];
	}

	//Postprocess: if *every* key in a track is identical, we only need one.
	//Worth the trouble? This is not a performance-dependent application.
	for (unsigned int t = 0; t < data.bones.size(); t++) {
		bool clean = true;

		for (int f = 1; f < data.frames; f++) {
			if (data.bones[t].keys[f] != data.bones[t].keys[f - 1]) {
				clean = false;
				break;
			}
		}

		if (clean)
			data.bones[t].keys.resize(1);
	}
	for (unsigned int t = 0; t < data.floats.size(); t++) {
		bool clean = true;

		for (int f = 1; f < data.frames; f++) {
			if (data.floats[t].keys[f] != data.floats[t].keys[f - 1]) {
				clean = false;
				break;
			}
		}

		if (clean)
			data.floats[t].keys.resize(1);
	}
}
