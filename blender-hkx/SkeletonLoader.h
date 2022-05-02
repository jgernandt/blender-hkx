#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{
	class SkeletonLoader
	{
	public:
		struct Options
		{
			bool loadHierarchies;
		};
	public:
		SkeletonLoader(const HavokEngine& engine);
		~SkeletonLoader();

		void load(int argc, char** argv);

		const Skeleton& operator[](int i) const { return m_skeletons[i]; }
		bool empty() const { return m_skeletons.empty(); }

	public:
		Options m_options;

	private:
		std::vector<Skeleton> m_skeletons;
	};
}
