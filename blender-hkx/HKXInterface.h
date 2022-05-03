#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{
	class HKXInterface
	{
	public:
		HKXInterface();
		
		hkRefPtr<hkaAnimationContainer> load(const char* fileName);
		void save(hkaAnimationContainer* animCtnr, const char* fileName);
		
	public:
		struct
		{
			bool textFormat;//{ false }
			FileLayout layout;//{ LAYOUT_AMD64 }
		} m_options;
	};
}
