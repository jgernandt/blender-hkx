#pragma once
#include "common.h"

namespace iohkx
{
	class XMLReader
	{
	public:
		XMLReader() {}

		void read(const std::string& file, const Skeleton& skeleton);

		AnimationData m_data;
	};
}
