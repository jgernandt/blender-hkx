#pragma once
#include <utility>
#include <vector>
#include "common.h"

namespace iohkx
{
	//Gather all the logic for sorting out animation tracks here, so the decoder
	//doesn't need to worry about that.
	class TrackPacker
	{
	public:
		//bones[i], floats[i] will be the data for output track i
		// (the pair is so that we can find the bone (to fill with ref pose)
		//  if there is no track for it)
		std::vector<std::pair<BoneTrack*, Bone*>> m_bones;
		std::vector<FloatTrack*> m_floats;
		int m_annotationTracks[2]{ -1, -1 };

	public:
		bool map(const AnimationData& data, 
			hkaAnimation* animation, hkaAnimationBinding* binding);

	private:
		bool paired(const AnimationData& data,
			hkaAnimation* animation, hkaAnimationBinding* binding);
		bool single(const AnimationData& data,
			hkaAnimation* animation, hkaAnimationBinding* binding);
	};

	class TrackUnpacker
	{
	public:
		std::vector<BoneTrack*> m_bones;
		std::vector<FloatTrack*> m_floats;
		int m_annotationClip{ -1 };
		int m_annotationTrack{ -1 };

	public:
		bool map(AnimationData& data, const std::vector<Skeleton*>& skeletons,
			const hkaAnimation* animation, const hkaAnimationBinding* binding);

	private:
		bool paired(AnimationData& data, const std::vector<Skeleton*>& skeletons,
			const hkaAnimation* animation, const hkaAnimationBinding* binding);
		bool single(AnimationData& data, const std::vector<Skeleton*>& skeletons,
			const hkaAnimation* animation, const hkaAnimationBinding* binding);
	};
}
