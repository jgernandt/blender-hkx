#include "pch.h"
#include "XMLInterface.h"

#define DATA_VERSION 1

const char* NODE_FILE = "blender-hkx";

const char* NODE_SKELETON = "skeleton";
const char* NODE_BONE = "bone";

const char* NODE_ANIMATION = "animation";
const char* NODE_TRACK = "track";

const char* TYPE_INT = "int";
const char* TYPE_FLOAT = "float";
const char* TYPE_STRING = "string";
const char* TYPE_VEC3 = "vec3";
const char* TYPE_VEC4 = "vec4";
const char* TYPE_TRANSFORM = "transform";

const char* ATTR_FRAMES = "frames";
const char* ATTR_FRAMERATE = "frameRate";
const char* ATTR_BLENDMODE = "blendMode";
const char* ATTR_SKELETON = "skeleton";
const char* ATTR_REFPOSE = "refPose";
const char* ATTR_FRAME = "frame";


using namespace iohkx;
using namespace pugi;

template<int N>
static void strToVec(const char* str, float* vec)
{
	assert(str);

	if (*str == '(')
		str++;

	char* end;
	for (int i = 0; i < N; i++) {
		if (*str == ',')
			str++;
		vec[i] = static_cast<float>(strtod(str, &end));
		assert(str != end);
		str = end;
	}
}

static void readFloatTrack(pugi::xml_node node, iohkx::AnimationData& data)
{
}

static void readTransformTrack(pugi::xml_node node, iohkx::AnimationData& data)
{
	Clip& clip = data.clips.back();

	assert(clip.skeleton);

	//look for this bone in the skeleton
	auto it = clip.skeleton->boneIndex.find(node.attribute("name").value());
	if (it != clip.skeleton->boneIndex.end()) {
		//This is an actual bone

		//Fill out the next track
		clip.boneTracks[clip.nBoneTracks].target = it->second;
		clip.boneTracks[clip.nBoneTracks].keys.reserve(data.frames);
		clip.boneMap[it->second->index] = &clip.boneTracks[clip.nBoneTracks];

		auto&& keys = clip.boneTracks[clip.nBoneTracks].keys;

		clip.nBoneTracks++;

		//Add keys
		for (xml_node key = node.child(TYPE_TRANSFORM); key; key = key.next_sibling(TYPE_TRANSFORM)) {

			float raw[10];
			strToVec<10>(key.child_value(), raw);

			//This transform is in object space
			hkVector4 loc(raw[0], raw[1], raw[2]);
			hkQuaternion rot(raw[4], raw[5], raw[6], raw[3]);
			hkVector4 scl(raw[7], raw[8], raw[9]);

			//Convert to parent space:
			//left multiply by the inverse of the parent bone's current pose
			
			//Which means that we must either do this in Blender, or have Blender
			//ensure that the bones are exported in descending order (which we don't!).

			//Or do it in a separate loop after this one. <- this

			keys.pushBack(hkQsTransform(loc, rot, scl));
		}
	}
}

static void readAnimation(
	pugi::xml_node node,
	const std::vector<Skeleton*>& skeletons,
	iohkx::AnimationData& data)
{
	//We don't have any real policy for skeleton names. 
	//Just do: first clip->first skeleton, second clip->last skeleton
	data.clips.push_back(Clip());
	data.clips.back().skeleton = data.clips.size() == 1 ? skeletons.front() : skeletons.back();

	//Reserve memory for tracks
	data.clips.front().boneTracks = new BoneTrack[data.clips.front().skeleton->nBones];
	data.clips.front().floatTracks = new FloatTrack[data.clips.front().skeleton->nFloats];
	data.clips.front().boneMap.resize(data.clips.front().skeleton->nBones, nullptr);
	data.clips.front().floatMap.resize(data.clips.front().skeleton->nFloats, nullptr);

	//for each track
	for (xml_node track = node.child(NODE_TRACK); track; track = track.next_sibling(NODE_TRACK)) {
		xml_attribute type = track.attribute("type");
		if (strcmp(type.value(), TYPE_TRANSFORM) == 0) {
			readTransformTrack(track, data);
		}
		else if (strcmp(type.value(), TYPE_FLOAT) == 0) {
			readFloatTrack(track, data);
		}
	}
}

void iohkx::XMLInterface::read(
	const char* fileName, 
	const std::vector<Skeleton*>& skeletons, 
	AnimationData& data)
{
	xml_document doc;
	xml_parse_result result = doc.load_file(fileName);
	if (result.status != status_ok) {
		throw Exception(ERR_INVALID_INPUT, "Failed to load XML");
	}
	
	xml_node root = doc.child(NODE_FILE);
	if (root) {
		if (root.attribute("version").as_int(-1) == 1) {
			for (xml_node node = root.child(TYPE_INT); node; node = node.next_sibling(TYPE_INT)) {
				xml_attribute name = node.attribute("name");
				if (strcmp(name.value(), ATTR_FRAMES) == 0) {
					data.frames = node.first_child().text().as_int(-1);
				}
				else if (strcmp(name.value(), ATTR_FRAMERATE) == 0) {
					data.frameRate = node.first_child().text().as_int(-1);
				}
			}
			for (xml_node node = root.child(TYPE_STRING); node; node = node.next_sibling(TYPE_STRING)) {
				if (strcmp(node.attribute("name").value(), ATTR_BLENDMODE) == 0) {
					data.blendMode = node.child_value();
					break;
				}
			}

			if (data.frames < 0)
				throw Exception(ERR_INVALID_INPUT, "Missing frames value");

			if (data.frames < 0)
				throw Exception(ERR_INVALID_INPUT, "Missing frame rate value");

			if (data.blendMode.empty())
				throw Exception(ERR_INVALID_INPUT, "Missing blend mode value");

			for (xml_node clip = root.child(NODE_ANIMATION); 
					clip; 
					clip = clip.next_sibling(NODE_ANIMATION)) {
				readAnimation(clip, skeletons, data);
			}
		}
		else
			throw Exception(ERR_INVALID_INPUT, "Unknown version");
	}
}

static void appendf(pugi::xml_node node, const char* name, float val)
{
	char buf[16];
	sprintf_s(buf, sizeof(buf), "%g", val);
	xml_node child = node.append_child(TYPE_FLOAT);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(buf);
}

static void appendi(pugi::xml_node node, const char* name, int val)
{
	char buf[16];
	sprintf_s(buf, sizeof(buf), "%d", val);
	xml_node child = node.append_child(TYPE_INT);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(buf);
}

static void appends(pugi::xml_node node, const char* name, const char* val)
{
	xml_node child = node.append_child(TYPE_STRING);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(val);
}

static void appendTransform(pugi::xml_node node, const char* name, const hkQsTransform& val)
{
	char buf[256];
	int i = 0;

	hkVector4 v = val.getTranslation();
	i += sprintf_s(buf, sizeof(buf), "%g %g %g", v(0), v(1), v(2));

	hkQuaternion q = val.getRotation();
	//print Blender format
	i += sprintf_s(buf + i, sizeof(buf) - i, " %g %g %g %g", q(3), q(0), q(1), q(2));

	v = val.getScale();
	sprintf_s(buf + i, sizeof(buf) - i, " %g %g %g", v(0), v(1), v(2));

	xml_node child = node.append_child(TYPE_TRANSFORM);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(buf);
}

static void appendBone(pugi::xml_node node, Bone* bone)
{
	assert(bone);
	xml_node child = node.append_child(NODE_BONE);
	child.append_attribute("name").set_value(bone->name.c_str());

	appendTransform(child, ATTR_REFPOSE, bone->refPoseObj);

	for (unsigned int i = 0; i < bone->children.size(); i++)
		appendBone(child, bone->children[i]);
}

void iohkx::XMLInterface::write(
	const AnimationData& data, const char* fileName)
{
	xml_document doc;
	//Add declaration
	xml_node decl = doc.append_child(node_declaration);
	decl.append_attribute("version").set_value("1.0");
	decl.append_attribute("encoding").set_value("UTF-8");

	//Add root element
	xml_node root = doc.append_child(NODE_FILE);
	root.append_attribute("version").set_value(DATA_VERSION);

	//Add shared attributes
	appendi(root, ATTR_FRAMES, data.frames);
	appendi(root, ATTR_FRAMERATE, data.frameRate);
	appends(root, ATTR_BLENDMODE, data.blendMode.c_str());

	//Add skeleton elements
	std::vector<const Skeleton*> addedSkeletons;
	for (unsigned int i = 0; i < data.clips.size(); i++) {
		//reject duplicates
		if (std::find(
			addedSkeletons.begin(), 
			addedSkeletons.end(), 
			data.clips[i].skeleton) != addedSkeletons.end())
			continue;
		addedSkeletons.push_back(data.clips[i].skeleton);

		xml_node skeleton = root.append_child(NODE_SKELETON);
		//Use index as name instead
		//skeleton.append_attribute("name").set_value(data.clips[i].skeleton->name.c_str());
		char buf[8];
		sprintf_s(buf, sizeof(buf), "%d", i);
		skeleton.append_attribute("name").set_value(buf);
		
		appendBone(skeleton, data.clips[i].skeleton->rootBone);
	}

	//Animations
	for (unsigned int i = 0; i < data.clips.size(); i++) {
		const Clip& clip = data.clips[i];

		//Insert animation element
		xml_node anim = root.append_child(NODE_ANIMATION);
		//Set name attribute to animation index
		char buf[8];
		sprintf_s(buf, sizeof(buf), "%d", i);
		anim.append_attribute("name").set_value(buf);

		appends(anim, ATTR_SKELETON, clip.skeleton->name.c_str());

		//Bone tracks
		for (int t = 0; t < clip.nBoneTracks; t++) {
			BoneTrack* track = &clip.boneTracks[t];
			assert(track->target);

			xml_node node = anim.append_child(NODE_TRACK);
			node.append_attribute("name").set_value(track->target->name.c_str());
			node.append_attribute("type").set_value(TYPE_TRANSFORM);

			for (int f = 0; f < track->keys.getSize(); f++) {
				//Set key name to frame index
				sprintf_s(buf, sizeof(buf), "%d", f);
				appendTransform(node, buf, track->keys[f]);
			}
		}

		//Float tracks
		for (int t = 0; t < clip.nFloatTracks; t++) {
			FloatTrack* track = &clip.floatTracks[t];
			assert(track->target);

			xml_node node = anim.append_child(NODE_TRACK);
			node.append_attribute("name").set_value(track->target->name.c_str());
			node.append_attribute("type").set_value(TYPE_FLOAT);

			for (int f = 0; f < track->keys.getSize(); f++) {
				sprintf_s(buf, sizeof(buf), "%d", f);
				appendf(node, buf, track->keys[f]);
			}
		}
	}

	doc.save_file(fileName);
}
