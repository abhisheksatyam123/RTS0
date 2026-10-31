#[compute]
#version 450

// GPU separation -- mirrors GameSim.SeparationPass AccumulatePush using O(N) 64x64 spatial hash grid
// Input: positions (vec2 xz), radii (float), cell_head (4096 ints), cell_next (N ints)
// Output: forces (vec2)

layout(local_size_x = 64) in;

layout(set = 0, binding = 0, std430) readonly buffer Positions {
    vec2 pos[];
};

layout(set = 0, binding = 1, std430) writeonly buffer Forces {
    vec2 force[];
};

layout(set = 0, binding = 2, std430) readonly buffer Radii {
    float radius[];
};

layout(set = 0, binding = 3, std430) readonly buffer CellHeads {
    int cell_head[];
};

layout(set = 0, binding = 4, std430) readonly buffer CellNext {
    int cell_next[];
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
    float ri = radius[i];
    vec2 f = vec2(0.0);
    if (cellSize > 0.0001) {
        int cx = int(floor(a.x / cellSize));
        int cz = int(floor(a.y / cellSize));
        for (int dz = -1; dz <= 1; ++dz) {
            int bz = (cz + dz) & 63;
            for (int dx = -1; dx <= 1; ++dx) {
                int bucket = ((cx + dx) & 63) | (bz << 6);
                for (int j = cell_head[bucket]; j >= 0; j = cell_next[j]) {
                    if (uint(j) == i) continue;
                    float rj = radius[j];
                    float min_d = (ri > 0.0 && rj > 0.0) ? (ri + rj) : minDist;
                    vec2 d = a - pos[j];
                    float dist = length(d);
                    if (dist < 0.0001 || dist >= min_d) continue;
                    float s = (min_d - dist) * strength;
                    f += (d / dist) * s;
                }
            }
        }
    } else {
        for (uint j = 0u; j < count; ++j) {
            if (j == i) continue;
            float rj = radius[j];
            float min_d = (ri > 0.0 && rj > 0.0) ? (ri + rj) : minDist;
            vec2 d = a - pos[j];
            float dist = length(d);
            if (dist < 0.0001 || dist >= min_d) continue;
            float s = (min_d - dist) * strength;
            f += (d / dist) * s;
        }
    }
    force[i] = f;
}
