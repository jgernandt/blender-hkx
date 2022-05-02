#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{

	class AnimationBinder
	{
	public:
		AnimationBinder(const HavokEngine& engine) {}

		void bind(const AnimationData& data, const Skeleton& skeleton);

		hkRefPtr<hkaAnimationBinding> m_binding;
	};
}
