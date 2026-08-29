#include "skeleton.h"
#include <godot_cpp/classes/file_access.hpp>
using namespace rtso;
bool SkeletonDef::load_xml(const std::string& path) { (void)path; return true; }
int SkeletonDef::find(const std::string& n) const {
    for (size_t i=0;i<bones.size();++i) if (bones[i].name==n) return (int)i;
    return -1;
}
