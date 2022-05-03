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

	struct Transform
	{
		float T[3];
		float R[4];
		float S[3];
	};

	struct Bone
	{
		std::string name;
		//parent-space rest pose
		hkQsTransform refPose;

		Bone* parent;
		std::vector<Bone*> children;
	};

	struct Float
	{
		std::string name;
		float refValue;
	};

	typedef std::map<std::string, int> TrackMap;

	struct Skeleton
	{
		std::string name;

		std::vector<Bone*> bones;
		//maps bone name to bone index
		TrackMap boneIndex;

		std::vector<Float> floats;
		//maps float name to float index
		TrackMap floatIndex;

		std::vector<Bone*> rootBones;
	};

	struct AnimationData
	{
		template<typename T>
		struct Track
		{
			//track index in skeleton
			int index;
			std::vector<T> keys;

			Track(int i = -1) : index(i) {}
		};

		int frames;
		int frameRate;
		std::string blendMode;
		std::vector<Track<Transform>> bones;
		std::vector<Track<float>> floats;

		const Skeleton* skeleton;
	};
}
