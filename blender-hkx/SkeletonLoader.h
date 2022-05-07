#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{
	class SkeletonLoader
	{
	public:
		SkeletonLoader();
		~SkeletonLoader();

		void load(hkaAnimationContainer* animCtnr);

		const Skeleton* operator[](int i) const { return m_skeletons[i]; }
		bool empty() const { return m_skeletons.empty(); }
		const std::vector<Skeleton*>& get() const { return m_skeletons; }

	private:
		std::vector<Skeleton*> m_skeletons;
	};
}
