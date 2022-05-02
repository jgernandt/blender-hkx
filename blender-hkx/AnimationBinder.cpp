#include "pch.h"
#include "AnimationBinder.h"

void iohkx::AnimationBinder::bind(const AnimationData& data, const Skeleton& skeleton)
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
	m_binding = new hkaAnimationBinding;
	m_binding->removeReference();
	m_binding->m_originalSkeletonName = skeleton.name.c_str();
	m_binding->m_blendHint = 
		additive ? hkaAnimationBinding::ADDITIVE : hkaAnimationBinding::NORMAL;
	
	std::vector<int> dataToBoneTrack;
	if (nBones && (unsigned int)nBones < skeleton.bones.size()) {
		m_binding->m_transformTrackToBoneIndices.setSize(nBones);
		//The pedant in me wants the entries in m_transformTrackToBoneIndices 
		//to be sorted in order (probably doesn't matter at all).
		//This means we must provide a mapping from the indices in
		//data.bones to track indices.
		dataToBoneTrack.resize(nBones);
		
		//Sorting with good worst-case, poor best-case performance
		std::vector<int> temp(skeleton.bones.size(), -1);
		for (int i = 0; i < (int)data.bones.size(); i++)
			//temp is the mapping from bone index to data index
			temp[data.bones[i].index] = i;

		int at = 0;
		for (int i = 0; i < (int)temp.size(); i++) {
			//i is the bone index
			if (temp[i] != -1) {
				m_binding->m_transformTrackToBoneIndices[at] = i;
				dataToBoneTrack[temp[i]] = at++;
			}
		}
	}

	std::vector<int> dataToFloatTrack;
	if (nFloats && (unsigned int)nFloats < skeleton.floats.size()) {
		m_binding->m_floatTrackToFloatSlotIndices.setSize(nFloats);
		//dito
		dataToFloatTrack.resize(nFloats);

		std::vector<int> temp(skeleton.floats.size(), -1);
		for (int i = 0; i < (int)data.floats.size(); i++)
			temp[data.floats[i].index] = i;

		int at = 0;
		for (int i = 0; i < (int)temp.size(); i++) {
			if (temp[i] != -1) {
				m_binding->m_floatTrackToFloatSlotIndices[at] = i;
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

	m_binding->m_animation = new hkaSplineCompressedAnimation( *raw, tcp, acp );
}
