#pragma once
#include "common.h"

namespace iohkx
{
	class XMLInterface
	{
	public:
		XMLInterface() {}

		void read(const char* fileName, const std::vector<Skeleton*>& skeletons, AnimationData& data);
		void write(const AnimationData& data, const char* fileName);
	};
}
