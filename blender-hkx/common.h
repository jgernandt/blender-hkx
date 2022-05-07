#pragma once
#include <string>

#include "Common/Base/hkBase.h"

namespace iohkx
{
	enum ErrorCode
	{
		ERR_NONE,
		ERR_INVALID_ARGS,
		ERR_INVALID_INPUT,
		ERR_READ_FAIL,
		ERR_WRITE_FAIL
	};

	struct Exception
	{
		Exception(int c, const char* s) : code(c), msg(s) {}
		int code;
		const char* msg;
	};

	enum FileLayout
	{
		LAYOUT_WIN32,
		LAYOUT_AMD64
	};

	//Need to be on the heap for refs to work
	struct Bone
	{
		int index;
		std::string name;
		//parent-space rest pose
		hkQsTransform refPose;
		//object-space rest pose
		hkQsTransform refPoseObj;

		Bone* parent;
		std::vector<Bone*> children;
	};
	static_assert(offsetof(Bone, refPose) % 16 == 0, "Misaligned hkQsTransform");
	static_assert(offsetof(Bone, refPoseObj) % 16 == 0, "Misaligned hkQsTransform");

	//Don't need to be on the heap, but do it for consistency
	struct Float
	{
		int index;
		std::string name;
		float refValue;
	};

	typedef std::map<std::string, int> NameMap;

	struct Skeleton
	{
		std::string name;

		int nBones;
		Bone* bones;
		//maps bone name to bone index
		NameMap boneIndex;

		int nFloats;
		Float* floats;
		//maps float name to float index
		NameMap floatIndex;

		Bone* rootBone;
	};

	struct BoneTrack
	{
		const Bone* target;
		hkArray<hkQsTransform> keys;
	};

	struct FloatTrack
	{
		const Float* target;
		hkArray<hkReal> keys;
	};

	struct Clip
	{
		const Skeleton* skeleton;

		int nBoneTracks;
		BoneTrack* boneTracks;

		int nFloatTracks;
		FloatTrack* floatTracks;
	};

	struct AnimationData
	{
		int frames;
		int frameRate;
		std::string blendMode;

		std::vector<Clip> clips;
	};

	//Unused
	struct Transform
	{
		float T[3];
		float R[4];
		float S[3];
	};

	inline bool operator==(const Transform& lhs, const Transform& rhs)
	{
		for (int i = 0; i < 3; i++) {
			if (lhs.T[i] != rhs.T[i])
				return false;
		}
		for (int i = 0; i < 4; i++) {
			if (lhs.R[i] != rhs.R[i])
				return false;
		}
		for (int i = 0; i < 3; i++) {
			if (lhs.S[i] != rhs.S[i])
				return false;
		}
		return true;
	}
	inline bool operator!=(const Transform& lhs, const Transform& rhs)
	{
		return !(lhs == rhs);
	}

}
