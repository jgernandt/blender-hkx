#pragma once
#include "common.h"
#include "HavokEngine.h"

namespace iohkx
{

	class AnimationDecoder
	{
	public:
		AnimationDecoder();
		~AnimationDecoder();

		hkRefPtr<hkaAnimationContainer> compress();
		void decompress(hkaAnimationContainer* animCtnr, 
			const std::vector<Skeleton*>& skeletons);

		AnimationData& get() { return m_data; }
		const AnimationData& get() const { return m_data; }

	private:
		struct CompressionMap;
		struct DecompressionMap;

		void annotate(hkaAnimation* animation);
		void mapPairedComp(
			hkaAnimationBinding* binding,
			hkaAnimation* animation,
			CompressionMap& map);
		void mapSingleComp(
			hkaAnimationBinding* binding,
			hkaAnimation* animation,
			CompressionMap& map);
		void mapPaired(
			hkaAnimationBinding* binding,
			const std::vector<Skeleton*>& skeletons,
			DecompressionMap& map);
		void mapSingle(
			hkaAnimationBinding* binding, 
			const std::vector<Skeleton*>& skeletons, 
			DecompressionMap& map);

		void removeDuplicateKeys();
		void preProcess();

	private:
		AnimationData m_data;
	};
}
