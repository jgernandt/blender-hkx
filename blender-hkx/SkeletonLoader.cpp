#include "pch.h"
#include "SkeletonLoader.h"

iohkx::SkeletonLoader::SkeletonLoader()
{
	m_options.loadHierarchies = true;
}

iohkx::SkeletonLoader::~SkeletonLoader()
{
	//Not the best memory management in the world, but I can't be bothered
	//with this pre-C++11 crap...
	for (unsigned int i = 0; i < m_skeletons.size(); i++) {
		delete[] m_skeletons[i].bones[0];
	}
}

void iohkx::SkeletonLoader::load(hkaAnimationContainer* animCtnr)
{
	assert(animCtnr);

	if (animCtnr->m_skeletons.getSize() == 0)
		throw Exception(ERR_INVALID_INPUT, "No skeleton");
	//First skeleton is the one we want (Skyrim will have a ragdoll after it)
	hkaSkeleton* src = animCtnr->m_skeletons[0];

	Skeleton skeleton;
	skeleton.name = src->m_name.cString();
	skeleton.bones.resize(src->m_bones.getSize());
	skeleton.floats.resize(src->m_floatSlots.getSize());

	Bone* bones = new Bone[skeleton.bones.size()];

	for (int i = 0; i < src->m_bones.getSize(); i++) {
		Bone* bone = bones + i;
		skeleton.bones[i] = bone;
		bone->name = src->m_bones[i].m_name.cString();
		bone->refPose = src->m_referencePose[i];

		if (m_options.loadHierarchies) {
			int parent = src->m_parentIndices[i];
			//The children always come after their parents, right?
			//Otherwise, we'll just have to do this in a separate loop. No big deal.
			assert(parent < i);

			if (parent == -1) {
				bone->parent = NULL;
				skeleton.rootBones.push_back(bone);
			}
			else {
				bone->parent = skeleton.bones[parent];
				bone->parent->children.push_back(bone);
			}
		}
	}
	
	//Map the bones
	for (int i = 0; i < src->m_bones.getSize(); i++) {
		std::string name = src->m_bones[i].m_name;
		skeleton.boneIndex[name] = i;
	}

	//Map the floats
	for (int i = 0; i < src->m_floatSlots.getSize(); i++) {
		std::string name = src->m_floatSlots[i];
		skeleton.floatIndex[name] = i;
	}

	m_skeletons.push_back(skeleton);
}
