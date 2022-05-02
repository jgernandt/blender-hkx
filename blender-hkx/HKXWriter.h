#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{
	class HKXWriter
	{
	public:
		struct Options
		{
			bool textFormat;//{ false }
			FileLayout layout;//{ LAYOUT_AMD64 }
		};

	public:
		HKXWriter(const HavokEngine& engine);
		
		void write(hkRefPtr<hkaAnimationBinding> binding, const std::string& file);
		
	public:
		Options m_options;
	};
}
