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

	enum ReferenceFrame
	{
		REF_UNDEFINED,
		REF_OBJECT,
		REF_BONE,
		REF_PARENT_BONE,
	};

	constexpr const char* REF_INDEX[]{
		"",
		"OBJECT",
		"BONE",
		"PARENT_BONE",
	};

	//Name of the dummy bone used to represent skeleton transforms
	constexpr const char* ROOT_BONE = "NPC";

	//Need to be on the heap for refs to work
	struct Bone
	{
		int index{ -1 };
		std::string name;
		//parent-space rest pose
		hkQsTransform refPose;
		//inverse of parent-space ref pose
		hkQsTransform refPoseInv;
		//object-space rest pose
		hkQsTransform refPoseObj;

		Bone* parent{ nullptr };
		std::vector<Bone*> children;
	};

	//Don't need to be on the heap, but do it for consistency
	struct Float
	{
		int index{ -1 };
		std::string name;
		float refValue{ 0.0f };
	};

	struct Skeleton
	{
		std::string name;

		int nBones{ 0 };
		Bone* bones{ nullptr };
		std::map<std::string, Bone*> boneIndex;

		int nFloats{ 0 };
		Float* floats{ nullptr };
		std::map<std::string, Float*> floatIndex;

		Bone* rootBone{ nullptr };
	};

	struct BoneTrack
	{
		const Bone* target{ nullptr };
		hkArray<hkQsTransform> keys;
	};

	struct FloatTrack
	{
		const Float* target{ nullptr };
		hkArray<hkReal> keys;
	};

	struct Annotation
	{
		int frame{ 0 };
		std::string text;
	};

	//A way of adding bones to the skeleton,
	// but I don't think that's a good idea.
	//struct Addendum
	//{
	//	std::unique_ptr<Bone> bone;
	//	std::unique_ptr<BoneTrack> track;
	//};

	struct Clip
	{
		const Skeleton* skeleton{ nullptr };

		ReferenceFrame refFrame{ REF_UNDEFINED };

		BoneTrack* rootTransform{ nullptr };

		int nBoneTracks{ 0 };
		BoneTrack* boneTracks{ nullptr };

		int nFloatTracks{ 0 };
		FloatTrack* floatTracks{ nullptr };

		//maps bone index to track
		std::vector<BoneTrack*> boneMap;
		std::vector<FloatTrack*> floatMap;

		//std::vector<Addendum> addenda;

		//I've only ever seen annotation keys on the root bone 
		//(NPC Root [Root]) of the primary actor.
		std::vector<Annotation> annotations;
	};

	struct AnimationData
	{
		int frames{ 0 };
		int frameRate{ 30 };
		bool additive{ false };

		std::vector<Clip> clips;
	};
}
