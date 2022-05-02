#pragma once
#include <string>

#include "Common/Base/hkBase.h"

namespace iohkx
{
	enum ErrorCode
	{
		ERR_NONE,
		ERR_RUNTIME_ERROR,
		ERR_INVALID_ARGS,
		ERR_INVALID_INPUT,
		ERR_INVALID_SKELETON,
		ERR_WRITE_FAIL
	};

	struct Exception
	{
		Exception(int c, const char* s) : code(c), msg(s) {}
		int code;
		std::string msg;
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

	struct AnimationData
	{
		template<typename T>
		struct Track
		{
			int index;
			std::vector<T> keys;

			Track(int i = -1) : index(i) {}
		};

		int frames;
		int frameRate;
		std::string blendMode;
		std::vector<Track<Transform>> bones;
		std::vector<Track<float>> floats;
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
		TrackMap boneIndex;

		std::vector<Float> floats;
		TrackMap floatIndex;

		std::vector<Bone*> rootBones;
	};

}
