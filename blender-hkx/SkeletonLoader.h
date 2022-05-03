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

		const Skeleton& operator[](int i) const { return m_skeletons[i]; }
		bool empty() const { return m_skeletons.empty(); }

	public:
		struct
		{
			bool loadHierarchies;
		} m_options;

	private:
		std::vector<Skeleton> m_skeletons;
	};
}
