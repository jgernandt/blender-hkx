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

		hkRefPtr<hkaAnimationContainer> compress(
			const AnimationData& data, const Skeleton& skeleton);
		void decompress(
			hkaAnimationContainer* animCtnr, 
			const std::vector<Skeleton*>& skeletons);

		const AnimationData& get() const { return m_data; }

	private:
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

	private:
		AnimationData m_data;
		//std::vector<AnimationData*> m_data;
	};
}
