#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{
	class AnimationDecoder
	{
	public:
		AnimationDecoder() {}

		hkRefPtr<hkaAnimationContainer> compress(const AnimationData& data, const Skeleton& skeleton);
		void decompress(hkaAnimationContainer* animCtnr, const Skeleton& skeleton, AnimationData& data);
	};
}
