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
		void mapPairedComp(
			hkaAnimationBinding* binding, 
			std::vector<std::pair<BoneTrack*, Bone*>>& bones,
			std::vector<FloatTrack*>& floats);
		void mapSingleComp(
			hkaAnimationBinding* binding,
			std::vector<std::pair<BoneTrack*, Bone*>>& bones,
			std::vector<FloatTrack*>& floats);
		void mapPaired(
			hkaAnimationBinding* binding,
			const std::vector<Skeleton*>& skeletons,
			std::vector<BoneTrack*>& bones,
			std::vector<FloatTrack*>& floats);
		void mapSingle(
			hkaAnimationBinding* binding, 
			const std::vector<Skeleton*>& skeletons, 
			std::vector<BoneTrack*>& bones, 
			std::vector<FloatTrack*>& floats);

		void preProcess();

	private:
		AnimationData m_data;
	};
}
