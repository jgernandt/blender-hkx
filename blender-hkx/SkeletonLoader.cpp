#include "pch.h"
#include "SkeletonLoader.h"

iohkx::SkeletonLoader::SkeletonLoader()
{
}

iohkx::SkeletonLoader::~SkeletonLoader()
{
	for (unsigned int i = 0; i < m_skeletons.size(); i++) {
		delete[] m_skeletons[i]->bones;
		delete[] m_skeletons[i]->floats;
		delete m_skeletons[i];
	}
}

void iohkx::SkeletonLoader::load(hkaAnimationContainer* animCtnr)
{
	assert(animCtnr);

	if (animCtnr->m_skeletons.isEmpty())
		return;

	//First skeleton is the one we want (Skyrim will have a ragdoll after it)
	hkaSkeleton* src = animCtnr->m_skeletons[0];

	Skeleton* skeleton = new Skeleton;
	m_skeletons.push_back(skeleton);

	//Convenience vars
	int nBones = src->m_bones.getSize();
	int nFloats = src->m_floatSlots.getSize();

	//Set name
	//(turns out all Skyrim skeletons have the same name, so this isn't useful)
	skeleton->name = src->m_name.cString();
	//Use index as name instead
	//char buf[8];
	//sprintf_s(buf, sizeof(buf), "%d", m_skeletons.size() - 1);
	//skeleton->name = buf;
	//Actually, it *is* useful when packing. We can't rely on it as an id, though.

	//Create the bones
	//To simplify dealing with paired animations, we add an extra bone
	//and parent the whole skeleton to it.
	//This may be incorrect with the camera controls. I really don't know yet.
	skeleton->nBones = nBones;
	skeleton->bones = new Bone[nBones + 1];

	//Root bone is the last, hidden bone
	skeleton->rootBone = &skeleton->bones[nBones];
	skeleton->rootBone->index = -1;
	skeleton->rootBone->name = "NPC";
	skeleton->rootBone->refPose.setIdentity();
	skeleton->rootBone->refPoseObj.setIdentity();
	skeleton->rootBone->parent = NULL;

	//Process bone data
	for (int i = 0; i < nBones; i++) {
		Bone& bone = skeleton->bones[i];

		bone.index = i;
		bone.name = src->m_bones[i].m_name.cString();

		int iparent = src->m_parentIndices[i];
		Bone* parent = iparent == -1 ? skeleton->rootBone : &skeleton->bones[iparent];
		bone.parent = parent;
		parent->children.push_back(&bone);

		//The children always come after their parents, right?
		//Otherwise, we'll just have to do this in a separate loop.
		//(The only incorrect result now would be refPoseObj)
		assert(iparent < i);

		bone.refPose = src->m_referencePose[i];
		bone.refPoseObj.setMul(parent->refPoseObj, bone.refPose);
	}

	//Create the floats
	skeleton->nFloats = nFloats;
	skeleton->floats = new Float[nFloats];

	//Process float data
	for (int i = 0; i < nFloats; i++) {
		skeleton->floats[i].name = src->m_floatSlots[i].cString();
		skeleton->floats[i].refValue = src->m_referenceFloats[i];
	}
	
	//Map the bones
	for (int i = 0; i < nBones; i++) {
		skeleton->boneIndex[skeleton->bones[i].name] = &skeleton->bones[i];
	}

	//Map the floats
	for (int i = 0; i < nFloats; i++) {
		skeleton->floatIndex[skeleton->floats[i].name] = &skeleton->floats[i];
	}
}
