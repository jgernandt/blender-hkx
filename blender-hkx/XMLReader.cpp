#include "pch.h"
#include "XMLReader.h"

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

void iohkx::XMLReader::read(const std::string& file, const Skeleton& skeleton)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(file.c_str());
	if (result.status != pugi::status_ok) {
		throw Exception(ERR_INVALID_INPUT, "Failed to load XML");
	}
	
	pugi::xml_node root = doc.child("io_hkx_file");
	if (root) {
		if (root.attribute("version").as_int(-1) == 1) {
			//We should check for the *existence* of data, not its validity
			m_data.frames = root.child("nFrames").first_child().text().as_int(-1);
			if (m_data.frames < 0)
				throw Exception(ERR_INVALID_INPUT, "Missing nFrames value");

			m_data.frameRate = root.child("frameRate").first_child().text().as_int(-1);
			if (m_data.frames < 0)
				throw Exception(ERR_INVALID_INPUT, "Missing frameRate value");
			
			m_data.blendMode = root.child("blendMode").child_value();
			if (m_data.blendMode.empty())
				throw Exception(ERR_INVALID_INPUT, "Missing blendMode value");

			pugi::xml_node clip = root.child("clip");
			if (!clip)
				throw Exception(ERR_INVALID_INPUT, "Missing clip");

			for (pugi::xml_node bone = clip.child("bone"); bone; bone = bone.next_sibling("bone")) {
				//look for this bone in the skeleton
				TrackMap::const_iterator it = 
					skeleton.boneIndex.find(bone.attribute("name").value());
				if (it != skeleton.boneIndex.end()) {
					//This is an actual bone
					m_data.bones.push_back(AnimationData::Track<Transform>());
					m_data.bones.back().index = it->second;

					//we expect one key per frame
					m_data.bones.back().keys.reserve(m_data.frames);
					for (pugi::xml_node key = bone.child("key"); key; key = key.next_sibling("key")) {
						m_data.bones.back().keys.push_back(Transform());
						strToVec(key.child("translation").child_value(), m_data.bones.back().keys.back().T, 3);
						strToVec(key.child("rotation").child_value(), m_data.bones.back().keys.back().R, 4);
						strToVec(key.child("scale").child_value(), m_data.bones.back().keys.back().S, 3);
					}
					if (m_data.bones.back().keys.size() != m_data.frames)
						throw Exception(ERR_INVALID_INPUT, "Missing keys");
				}
			}

			//std::cout << "nFrames: " << m_data.frames << '\n'
			//	<< "frameRate: " << m_data.frameRate << '\n'
			//	<< "blendMode: " << m_data.blendMode << '\n';
			//for (int i = 0; i < m_data.bones.size(); i++)
			//	std::cout << m_data.bones[i].index << std::endl;

		}
		else
			throw Exception(ERR_INVALID_INPUT, "Unknown version");
	}
}
