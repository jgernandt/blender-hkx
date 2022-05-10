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

	//Don't need to be on the heap, but do it for consistency
	struct Float
	{
		int index;
		std::string name;
		float refValue;
	};

	struct Skeleton
	{
		std::string name;

		int nBones;
		Bone* bones;
		std::map<std::string, Bone*> boneIndex;

		int nFloats;
		Float* floats;
		std::map<std::string, Float*> floatIndex;

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
		const Skeleton* skeleton{ nullptr };

		int nBoneTracks{ 0 };
		BoneTrack* boneTracks{ nullptr };

		int nFloatTracks{ 0 };
		FloatTrack* floatTracks{ nullptr };

		//maps bone index to track
		std::vector<BoneTrack*> boneMap;
		std::vector<FloatTrack*> floatMap;
	};

	struct AnimationData
	{
		int frames;
		int frameRate;
		std::string blendMode;

		std::vector<Clip> clips;
	};
}
