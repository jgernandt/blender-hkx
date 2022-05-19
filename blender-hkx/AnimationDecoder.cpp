#include "pch.h"
#include "AnimationDecoder.h"
#include "TrackMapper.h"

constexpr int FRAME_RATE = 30;

using namespace iohkx;

//Transform the bone and its descendants to parent-space transform T
static void objToBone(Bone* bone, Clip& clip, int frame,
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
		//we want
		//inv ref pose * inv parent current pose * current pose
		hkQsTransform iRef;
		iRef.setInverse(track->target->refPose);
		track->keys[frame].setMul(iT, track->keys[frame]);
		track->keys[frame].setMul(iRef, track->keys[frame]);
	}
	else {
		next_T.setMul(T, bone->refPose);
	}

	hkQsTransform next_iT;
	next_iT.setInverse(next_T);

	//then recurse
	for (auto&& child : bone->children) {
		objToBone(child, clip, frame, next_T, next_iT);
	}
}

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
	TrackPacker map;
	map.map(m_data, raw, binding);

	//The mapping functions should have determined how many tracks we need
	int nBones = map.m_bones.size();
	int nFloats = map.m_floats.size();

	if (nBones == 0 && nFloats == 0)
		//Nothing to export
		return hkRefPtr<hkaAnimationContainer>();

	binding->m_blendHint = m_data.additive ? 
		hkaAnimationBinding::ADDITIVE : hkaAnimationBinding::NORMAL;
	raw->m_duration = static_cast<float>(m_data.frames - 1) / m_data.frameRate;
	raw->m_numberOfTransformTracks = nBones;
	raw->m_numberOfFloatTracks = nFloats;
	raw->m_transforms.setSize(nBones * m_data.frames);
	raw->m_floats.setSize(nFloats * m_data.frames);

	//Transfer data to raw anim

	//bone tracks
	for (int i = 0; i < nBones; i++) {
		BoneTrack* track = map.m_bones[i].first;
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
				if (map.m_bones[i].second) {
					raw->m_transforms[i + f * nBones] = map.m_bones[i].second->refPose;
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
		FloatTrack* track = map.m_floats[i];
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
		if (map.m_annotationTracks[i] != -1) {
			for (auto&& anno : m_data.clips[i].annotations) {
				hkaAnnotationTrack::Annotation a;
				a.m_time = static_cast<float>(anno.frame) / FRAME_RATE;
				a.m_text = anno.text.c_str();
				raw->m_annotationTracks[map.m_annotationTracks[i]].m_annotations.pushBack(a);
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
	TrackUnpacker map;
	if (!map.map(m_data, skeletons, anim, binding))
		//mapping failed
		return;

	//Set frame count, framerate, blend mode
	m_data.frames = static_cast<int>(std::round(anim->m_duration * FRAME_RATE)) + 1;
	m_data.frameRate = FRAME_RATE;
	m_data.additive = binding->m_blendHint == hkaAnimationBinding::ADDITIVE;

	//Init the key arrays
	for (int i = 0; i < anim->m_numberOfTransformTracks; i++) {
		if (map.m_bones[i])
			map.m_bones[i]->keys.setSize(m_data.frames);
	}
	for (int i = 0; i < anim->m_numberOfFloatTracks; i++) {
		if (map.m_floats[i])
			map.m_floats[i]->keys.setSize(m_data.frames);
	}

	//Sample animation and transfer keys
	hkArray<hkQsTransform> tmpT(anim->m_numberOfTransformTracks);
	hkArray<hkReal> tmpF(anim->m_numberOfFloatTracks);
	for (int f = 0; f < m_data.frames; f++) {
		anim->sampleTracks((float)f / FRAME_RATE, tmpT.begin(), tmpF.begin(), HK_NULL);

		//convert tmpT to bone space
		for (int i = 0; i < tmpT.getSize(); i++) {
			if (map.m_bones[i]) {
				assert(map.m_bones[i]->target);

				//if additive, the offset first needs to be applied in parent space
				if (m_data.additive) {
					tmpT[i].setMulEq(map.m_bones[i]->target->refPose);
				}

				//now transform back to the ref space of the bone
				//bone space = inv * tmpT
				tmpT[i].setMul(map.m_bones[i]->target->refPoseInv, tmpT[i]);
			}
		}

		hkaSkeletonUtils::normalizeRotations(tmpT.begin(), tmpT.getSize());

		for (int i = 0; i < anim->m_numberOfTransformTracks; i++) {
			if (map.m_bones[i])
				map.m_bones[i]->keys[f] = tmpT[i];
		}
		for (int i = 0; i < anim->m_numberOfFloatTracks; i++) {
			if (map.m_floats[i])
				map.m_floats[i]->keys[f] = tmpF[i];
		}
	}
	for (auto&& clip : m_data.clips) {
		clip.refFrame = REF_BONE;
	}

	removeDuplicateKeys();

	//Annotations
	//map should point us to the annotation track for each clip
	if (map.m_annotationClip != -1 && map.m_annotationTrack != -1) {
		//store each annotation in the source animation
		for (auto&& item : anim->m_annotationTracks[map.m_annotationTrack].m_annotations) {
			//(convert time to frame)
			m_data.clips[map.m_annotationClip].annotations.push_back({
				static_cast<int>(std::round(item.m_time * FRAME_RATE)),
				item.m_text.cString() });
		}
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
		//We expect transforms to be in object space now

		//if (m_data.additive) {
			//Transform to bone space
		//	for (int f = 0; f < m_data.frames; f++) {
		//		objToBone(clip.skeleton->rootBone, clip, f, I, I);
		//	}
		//}
		//else {
			//Transform to parent-bone space
			for (int f = 0; f < m_data.frames; f++) {
				objToParent(clip.skeleton->rootBone, clip, f, I, I);
			}
		//}
		if (m_data.additive) {
			//convert to offset (right-mult by inverse of ref pose)
			for (int t = 0; t < clip.nBoneTracks; t++) {
				for (auto&& key : clip.boneTracks[t].keys) {
					key.setMulEq(clip.boneTracks[t].target->refPoseInv);
				}
			}
		}

		//set all rotations to the shortest distance from previous key
		sanitiseQuats(clip);
	}
}
