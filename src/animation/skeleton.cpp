#include "skeleton.h"
#include <godot_cpp/classes/xml_parser.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

using namespace rtso;

static bool read_xml_string(const std::string& path, std::string& out_xml) {
	godot::String gpath = godot::String(path.c_str());
	if (godot::FileAccess::file_exists(gpath)) {
		godot::Ref<godot::FileAccess> f = godot::FileAccess::open(gpath, godot::FileAccess::READ);
		if (f.is_valid()) {
			out_xml = f->get_as_text().utf8().get_data();
			return !out_xml.empty();
		}
	}
	std::ifstream is(path);
	if (is.is_open()) {
		std::stringstream ss;
		ss << is.rdbuf();
		out_xml = ss.str();
		return !out_xml.empty();
	}
	return false;
}

bool SkeletonDef::load_xml(const std::string& path) {
	bones.clear();
	std::vector<std::string> candidates = {
		path,
		"res://" + path,
		"assets/skeletons/" + path,
		"res://assets/skeletons/" + path,
		path + ".xml",
		"res://" + path + ".xml",
		"assets/skeletons/" + path + ".xml",
		"res://assets/skeletons/" + path + ".xml",
		"/home/abhi/project/RTS0/assets/skeletons/" + path + ".xml"
	};

	std::string xml_data;
	bool found = false;
	for (const auto& c : candidates) {
		if (read_xml_string(c, xml_data)) {
			found = true;
			break;
		}
	}

	if (!found || xml_data.empty()) return false;

	godot::Ref<godot::XMLParser> parser;
	parser.instantiate();
	godot::PackedByteArray pba;
	pba.resize((int64_t)xml_data.size());
	memcpy(pba.ptrw(), xml_data.data(), xml_data.size());

	if (parser->open_buffer(pba) != godot::OK) {
		return false;
	}

	std::vector<int> parent_stack;
	while (parser->read() == godot::OK) {
		godot::XMLParser::NodeType type = parser->get_node_type();
		if (type == godot::XMLParser::NODE_ELEMENT) {
			godot::String node_name = parser->get_node_name();
			if (node_name == "bone") {
				godot::String bname = parser->has_attribute("name") ? parser->get_named_attribute_value("name") : "";
				int parent_idx = parent_stack.empty() ? -1 : parent_stack.back();
				int cur_idx = (int)bones.size();
				BoneDef bd;
				bd.name = bname.utf8().get_data();
				bd.parent = parent_idx;
				bones.push_back(bd);

				if (!parser->is_empty()) {
					parent_stack.push_back(cur_idx);
				}
			}
		} else if (type == godot::XMLParser::NODE_ELEMENT_END) {
			godot::String node_name = parser->get_node_name();
			if (node_name == "bone" && !parent_stack.empty()) {
				parent_stack.pop_back();
			}
		}
	}
	return !bones.empty();
}

int SkeletonDef::find(const std::string& n) const {
	for (size_t i = 0; i < bones.size(); ++i) {
		if (bones[i].name == n) return (int)i;
	}
	return -1;
}

void SkeletonDef::compute_global_transforms(const std::vector<godot::Transform3D>& local_xfs, std::vector<godot::Transform3D>& out_globals) const {
	size_t count = std::min(bones.size(), local_xfs.size());
	out_globals.resize(count);
	for (size_t i = 0; i < count; ++i) {
		int p = bones[i].parent;
		if (p >= 0 && p < (int)out_globals.size()) {
			out_globals[i] = out_globals[p] * local_xfs[i];
		} else {
			out_globals[i] = local_xfs[i];
		}
	}
}
