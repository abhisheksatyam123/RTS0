#[compute]
#version 450

// GPU separation -- mirrors GameSim.SeparationPass AccumulatePush
// Input: positions (vec2 xz), Output: forces (vec2)

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly buffer Positions {
    vec2 pos[];
};

layout(set = 0, binding = 1, std430) writeonly buffer Forces {
    vec2 force[];
};

layout(push_constant, std430) uniform Params {
    uint count;
    float cellSize;
    float minDist;
    float strength;
};

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= count) return;
    vec2 a = pos[i];
    vec2 f = vec2(0.0);
    for (uint j = 0u; j < count; ++j) {
        if (j == i) continue;
        vec2 d = a - pos[j];
        float dist = length(d);
        if (dist < 0.0001 || dist >= minDist) continue;
        float s = (minDist - dist) * strength;
        f += normalize(d) * s;
    }
    force[i] = f;
}
