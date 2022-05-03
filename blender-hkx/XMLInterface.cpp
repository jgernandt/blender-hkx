#include "pch.h"
#include "XMLInterface.h"

const char* NODE_FILE = "blender-hkx";
const char* NODE_ANIMATION = "clip";
const char* NODE_SKELETON = "skeleton";
const char* NODE_BONE = "bone";
const char* NODE_KEY = "key";

const char* TYPE_INT = "int";
const char* TYPE_FLOAT = "float";
const char* TYPE_STRING = "string";
const char* TYPE_VEC3 = "vec3";
const char* TYPE_VEC4 = "vec4";

const char* ATTR_FRAMES = "frames";
const char* ATTR_FRAMERATE = "frameRate";
const char* ATTR_BLENDMODE = "blendMode";
const char* ATTR_SKELETON = "skeleton";
const char* ATTR_T = "translation";
const char* ATTR_R = "rotation";
const char* ATTR_S = "scale";

using namespace iohkx;
using namespace pugi;

static void strToVec(const char* str, float* vec, int n)
{
	assert(str);

	if (*str == '(')
		str++;

	char* end;
	for (int i = 0; i < n; i++) {
		if (*str == ',')
			str++;
		vec[i] = static_cast<float>(strtod(str, &end));
		str = end;
	}
}

template<int N>
static void vecToStr(const float* vec, char**)
{
}

void iohkx::XMLInterface::read(const char* fileName, const Skeleton& skeleton, AnimationData& data)
{
	xml_document doc;
	xml_parse_result result = doc.load_file(fileName);
	if (result.status != status_ok) {
		throw Exception(ERR_INVALID_INPUT, "Failed to load XML");
	}
	
	xml_node root = doc.child(NODE_FILE);
	if (root) {
		if (root.attribute("version").as_int(-1) == 1) {
			//We should check for the *existence* of data, not its validity
			data.frames = root.child(ATTR_FRAMES).first_child().text().as_int(-1);
			if (data.frames < 0)
				throw Exception(ERR_INVALID_INPUT, "Missing frames value");

			data.frameRate = root.child(ATTR_FRAMERATE).first_child().text().as_int(-1);
			if (data.frames < 0)
				throw Exception(ERR_INVALID_INPUT, "Missing frame rate value");
			
			data.blendMode = root.child(ATTR_BLENDMODE).child_value();
			if (data.blendMode.empty())
				throw Exception(ERR_INVALID_INPUT, "Missing blend mode value");

			xml_node clip = root.child(NODE_ANIMATION);
			if (!clip)
				throw Exception(ERR_INVALID_INPUT, "Missing clip");

			for (xml_node bone = clip.child(NODE_BONE); bone; bone = bone.next_sibling(NODE_BONE)) {
				//look for this bone in the skeleton
				TrackMap::const_iterator it = 
					skeleton.boneIndex.find(bone.attribute("name").value());
				if (it != skeleton.boneIndex.end()) {
					//This is an actual bone
					data.bones.push_back(AnimationData::Track<Transform>());
					data.bones.back().index = it->second;

					//we expect one key per frame
					data.bones.back().keys.reserve(data.frames);
					for (xml_node key = bone.child(NODE_KEY); key; key = key.next_sibling(NODE_KEY)) {
						data.bones.back().keys.push_back(Transform());
						strToVec(key.child(ATTR_T).child_value(), data.bones.back().keys.back().T, 3);
						strToVec(key.child(ATTR_R).child_value(), data.bones.back().keys.back().R, 4);
						strToVec(key.child(ATTR_S).child_value(), data.bones.back().keys.back().S, 3);
					}
					if (data.bones.back().keys.size() != data.frames)
						throw Exception(ERR_INVALID_INPUT, "Missing keys");
				}
			}
		}
		else
			throw Exception(ERR_INVALID_INPUT, "Unknown version");
	}
}

void appendi(pugi::xml_node node, const char* name, int val)
{
	char buf[16];
	sprintf_s(buf, sizeof(buf), "%d", val);
	xml_node child = node.append_child(TYPE_INT);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(buf);
}

void appends(pugi::xml_node node, const char* name, const char* val)
{
	xml_node child = node.append_child(TYPE_STRING);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(val);
}

void appendv3(pugi::xml_node node, const char* name, const float* vals)
{
	char buf[64];
	sprintf_s(buf, sizeof(buf), "(%g, %g, %g)", vals[0], vals[1], vals[2]);
	xml_node child = node.append_child(TYPE_VEC3);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(buf);
}

void appendv4(pugi::xml_node node, const char* name, const float* vals)
{
	char buf[64];
	sprintf_s(buf, sizeof(buf), "(%g, %g, %g, %g)", vals[0], vals[1], vals[2], vals[3]);
	xml_node child = node.append_child(TYPE_VEC4);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(buf);
}

void appendTransform(pugi::xml_node node, const hkQsTransform& val)
{
	char buf[64];

	hkVector4 v = val.getTranslation();
	sprintf_s(buf, sizeof(buf), "(%g, %g, %g)", v(0), v(1), v(2));
	xml_node child = node.append_child(TYPE_VEC3);
	child.append_attribute("name").set_value(ATTR_T);
	child.append_child(node_pcdata).set_value(buf);

	hkQuaternion q = val.getRotation();
	sprintf_s(buf, sizeof(buf), "(%g, %g, %g, %g)", q(0), q(1), q(2), q(3));
	child = node.append_child(TYPE_VEC4);
	child.append_attribute("name").set_value(ATTR_R);
	child.append_child(node_pcdata).set_value(buf);

	v = val.getScale();
	sprintf_s(buf, sizeof(buf), "(%g, %g, %g)", v(0), v(1), v(2));
	child = node.append_child(TYPE_VEC3);
	child.append_attribute("name").set_value(ATTR_S);
	child.append_child(node_pcdata).set_value(buf);
}

void appendBone(pugi::xml_node node, Bone* bone)
{
	assert(bone);
	xml_node child = node.append_child(NODE_BONE);
	child.append_attribute("name").set_value(bone->name.c_str());

	appendTransform(child, bone->refPose);

	for (unsigned int i = 0; i < bone->children.size(); i++)
		appendBone(child, bone->children[i]);
}

void iohkx::XMLInterface::write(const AnimationData& data, const char* fileName)
{
	xml_document doc;
	xml_node decl = doc.append_child(node_declaration);
	decl.append_attribute("version").set_value("1.0");
	decl.append_attribute("encoding").set_value("UTF-8");

	xml_node root = doc.append_child(NODE_FILE);
	root.append_attribute("version").set_value(1);

	appendi(root, ATTR_FRAMES, data.frames);
	appendi(root, ATTR_FRAMERATE, data.frameRate);
	appends(root, ATTR_BLENDMODE, data.blendMode.c_str());

	assert(data.skeleton);

	//Skeleton
	xml_node skeleton = root.append_child(NODE_SKELETON);
	skeleton.append_attribute("name").set_value(data.skeleton->name.c_str());
	
	for (unsigned int i = 0; i < data.skeleton->rootBones.size(); i++)
		appendBone(skeleton, data.skeleton->rootBones[i]);

	xml_node anim = root.append_child(NODE_ANIMATION);
	appends(anim, ATTR_SKELETON, data.skeleton->name.c_str());

	//Bone tracks
	for (unsigned int t = 0; t < data.bones.size(); t++) {
		Bone* bone = data.skeleton->bones[data.bones[t].index];
		assert(bone);

		xml_node node = anim.append_child(NODE_BONE);
		node.append_attribute("name").set_value(bone->name.c_str());

		for (int f = 0; f < data.frames; f++) {
			assert(f < data.bones[t].keys.size());

			xml_node key = node.append_child(NODE_KEY);
			appendv3(key, ATTR_T, data.bones[t].keys[f].T);
			appendv4(key, ATTR_R, data.bones[t].keys[f].R);
			appendv3(key, ATTR_S, data.bones[t].keys[f].S);
		}
	}

	doc.save_file(fileName);
}
