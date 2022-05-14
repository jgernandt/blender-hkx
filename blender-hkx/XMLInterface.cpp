#include "pch.h"
#include "XMLInterface.h"

#define DATA_VERSION 1

constexpr const char* NODE_FILE = "blender-hkx";

constexpr const char* NODE_SKELETON = "skeleton";
constexpr const char* NODE_BONE = "bone";
constexpr const char* NODE_FLOATSLOT = "slot";

constexpr const char* NODE_ANIMATION = "animation";
constexpr const char* NODE_ANNOTATION = "annotation";
constexpr const char* NODE_TRACK = "track";

constexpr const char* TYPE_BOOL = "bool";
constexpr const char* TYPE_INT = "int";
constexpr const char* TYPE_FLOAT = "float";
constexpr const char* TYPE_STRING = "string";
constexpr const char* TYPE_VEC3 = "vec3";
constexpr const char* TYPE_VEC4 = "vec4";
constexpr const char* TYPE_TRANSFORM = "transform";

constexpr const char* ATTR_FRAMES = "frames";
constexpr const char* ATTR_FRAMERATE = "frameRate";
constexpr const char* ATTR_ADDITIVE = "additive";
constexpr const char* ATTR_SKELETON = "skeleton";
constexpr const char* ATTR_REFERENCE = "ref";
constexpr const char* ATTR_REFERENCE_FRAME = "referenceFrame";
constexpr const char* ATTR_FRAME = "frame";
constexpr const char* ATTR_TEXT = "text";

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

static bool readb(pugi::xml_node node, const char* name)
{
	for (xml_node n = node.child(TYPE_BOOL); n; n = n.next_sibling(TYPE_BOOL)) {
		if (strcmp(n.attribute("name").value(), name) == 0) {
			return _stricmp(n.child_value(), "true") == 0;
		}
	}
	return false;
}

static int readi(pugi::xml_node node, const char* name)
{
	for (xml_node n = node.child(TYPE_INT); n; n = n.next_sibling(TYPE_INT)) {
		if (strcmp(n.attribute("name").value(), name) == 0) {
			return n.first_child().text().as_int(-1);
		}
	}
	return -1;
}

static const char* reads(pugi::xml_node node, const char* name)
{
	for (xml_node n = node.child(TYPE_STRING); n; n = n.next_sibling(TYPE_STRING)) {
		if (strcmp(n.attribute("name").value(), name) == 0) {
			return n.child_value();
		}
	}
	return "";
}

static void readFloatTrack(pugi::xml_node node, iohkx::AnimationData& data)
{
	Clip& clip = data.clips.back();

	assert(clip.skeleton);

	FloatTrack* track = nullptr;

	//look for this float in the skeleton
	auto it = clip.skeleton->floatIndex.find(node.attribute("name").value());
	if (it != clip.skeleton->floatIndex.end()) {
		track = &clip.floatTracks[clip.nFloatTracks];
		track->target = it->second;
		clip.floatMap[it->second->index] = track;

		clip.nFloatTracks++;
	}
	//else ignore

	if (track) {
		//Add keys
		track->keys.reserve(data.frames);
		for (xml_node key = node.child(TYPE_FLOAT); key; key = key.next_sibling(TYPE_FLOAT)) {
			track->keys.pushBack(key.first_child().text().as_float(track->target->refValue));
		}
	}
}

static void readTransformTrack(pugi::xml_node node, iohkx::AnimationData& data)
{
	Clip& clip = data.clips.back();

	assert(clip.skeleton);

	BoneTrack* track = nullptr;
	if (strcmp(node.attribute("name").value(), ROOT_BONE) == 0) {
		//this is the root bone
		track = clip.rootTransform;
		track->target = clip.skeleton->rootBone;
	}
	else {
		//look for this bone in the skeleton
		auto it = clip.skeleton->boneIndex.find(node.attribute("name").value());
		if (it != clip.skeleton->boneIndex.end()) {
			//This bone is driven by Havok. Use the next available track for it.
			track = &clip.boneTracks[clip.nBoneTracks];
			track->target = it->second;
			clip.boneMap[it->second->index] = track;

			clip.nBoneTracks++;
		}
		//else ignore
	}

	if (track) {
		//Add keys
		track->keys.reserve(data.frames);
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

			track->keys.pushBack(hkQsTransform(loc, rot, scl));
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
	Clip& clip = data.clips.back();
	clip.skeleton = data.clips.size() == 1 ? skeletons.front() : skeletons.back();

	//Reserve memory for tracks
	clip.rootTransform = new BoneTrack;
	clip.boneTracks = new BoneTrack[clip.skeleton->nBones];
	clip.floatTracks = new FloatTrack[clip.skeleton->nFloats];
	clip.boneMap.resize(clip.skeleton->nBones, nullptr);
	clip.floatMap.resize(clip.skeleton->nFloats, nullptr);

	//read tracks
	for (xml_node t = node.child(NODE_TRACK); t; t = t.next_sibling(NODE_TRACK)) {
		xml_attribute type = t.attribute("type");
		if (strcmp(type.value(), TYPE_TRANSFORM) == 0) {
			readTransformTrack(t, data);
		}
		else if (strcmp(type.value(), TYPE_FLOAT) == 0) {
			readFloatTrack(t, data);
		}
	}

	//read annotations
	for (xml_node a = node.child(NODE_ANNOTATION); a; a = a.next_sibling(NODE_ANNOTATION)) {
		clip.annotations.push_back({ readi(a, ATTR_FRAME), reads(a, ATTR_TEXT) });
	}
}

static void appendb(pugi::xml_node node, const char* name, bool val)
{
	xml_node child = node.append_child(TYPE_BOOL);
	child.append_attribute("name").set_value(name);
	child.append_child(node_pcdata).set_value(val ? "true" : "false");
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

static void appendAnnotation(pugi::xml_node node, const Annotation& annotation)
{
	xml_node child = node.append_child(NODE_ANNOTATION);
	appendi(child, ATTR_FRAME, annotation.frame);
	appends(child, ATTR_TEXT, annotation.text.c_str());
}

static void appendBone(pugi::xml_node node, Bone* bone)
{
	assert(bone);
	xml_node child = node.append_child(NODE_BONE);
	child.append_attribute("name").set_value(bone->name.c_str());

	appendTransform(child, ATTR_REFERENCE, bone->refPoseObj);

	for (unsigned int i = 0; i < bone->children.size(); i++)
		appendBone(child, bone->children[i]);
}

static void appendBoneTrack(pugi::xml_node node, BoneTrack* track)
{
	assert(track->target);

	xml_node child = node.append_child(NODE_TRACK);
	child.append_attribute("name").set_value(track->target->name.c_str());
	child.append_attribute("type").set_value(TYPE_TRANSFORM);

	for (int f = 0; f < track->keys.getSize(); f++) {
		//Set key name to frame index
		char buf[8];
		sprintf_s(buf, sizeof(buf), "%d", f);
		appendTransform(child, buf, track->keys[f]);
	}
}

static void appendFloatSlot(pugi::xml_node node, Float* slot)
{
	assert(slot);
	xml_node child = node.append_child(NODE_FLOATSLOT);
	child.append_attribute("name").set_value(slot->name.c_str());

	appendf(child, ATTR_REFERENCE, slot->refValue);
}

static void appendFloatTrack(pugi::xml_node node, FloatTrack* track)
{
	assert(track->target);

	xml_node child = node.append_child(NODE_TRACK);
	child.append_attribute("name").set_value(track->target->name.c_str());
	child.append_attribute("type").set_value(TYPE_FLOAT);

	for (int f = 0; f < track->keys.getSize(); f++) {
		char buf[8];
		sprintf_s(buf, sizeof(buf), "%d", f);
		appendf(child, buf, track->keys[f]);
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
			data.frames = readi(root, ATTR_FRAMES);
			data.frameRate = readi(root, ATTR_FRAMERATE);
			data.additive = readb(root, ATTR_ADDITIVE);

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
	appendb(root, ATTR_ADDITIVE, data.additive);

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

		appends(skeleton, ATTR_REFERENCE_FRAME, REF_INDEX[REF_OBJECT]);
		
		appendBone(skeleton, data.clips[i].skeleton->rootBone);

		//for (auto&& add : data.clips[i].addenda) {
		//	appendBone(skeleton, add.bone.get());
		//}

		for (int slot = 0; slot < data.clips[i].skeleton->nFloats; slot++) {
			appendFloatSlot(skeleton, &data.clips[i].skeleton->floats[slot]);
		}
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
		appends(anim, ATTR_REFERENCE_FRAME, REF_INDEX[clip.refFrame]);

		//Bone tracks
		if (clip.rootTransform) {
			appendBoneTrack(anim, clip.rootTransform);
		}
		for (int t = 0; t < clip.nBoneTracks; t++) {
			appendBoneTrack(anim, &clip.boneTracks[t]);
		}
		//for (auto&& add : data.clips[i].addenda) {
		//	appendTrack(anim, add.track.get());
		//}

		//Float tracks
		for (int t = 0; t < clip.nFloatTracks; t++) {
			appendFloatTrack(anim, &clip.floatTracks[t]);
		}

		//Annotations
		for (auto&& anno : clip.annotations) {
			appendAnnotation(anim, anno);
		}
	}

	doc.save_file(fileName);
}
