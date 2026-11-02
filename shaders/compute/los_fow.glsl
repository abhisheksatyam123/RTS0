#[compute]
#version 450

// TODO-GPU-02: GPU Compute Line-of-Sight (LOS) & Fog-of-War 2x Bilinear Upsample + 7-Tap Binomial Blur
// Mirrors LosShading::generate_bitmap (CLOSTexture::GenerateBitmap) on GPU.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer FowInput {
    uint fow_packed[]; // 1 byte per cell packed into uint32
};

layout(set = 0, binding = 1, std430) writeonly buffer LosOutput {
    uint out_packed[]; // 1 byte per upsampled pixel (fr x fr) packed into uint32
};

layout(push_constant, std430) uniform Params {
    uint res; // base FOW grid resolution (e.g. 64 or 128)
    uint fr;  // upsampled resolution (res * 2)
};

uint sample_fow(int cx, int cz) {
    if (cx < 0 || cz < 0 || cx >= int(res) || cz >= int(res)) return 0u;
    uint idx = uint(cz) * res + uint(cx);
    uint word = fow_packed[idx >> 2u];
    uint st = (word >> ((idx & 3u) * 8u)) & 0xFFu;
    return (st == 2u) ? 255u : ((st == 1u) ? 127u : 0u);
}

uint sample_upsampled(int ux, int uz) {
    if (ux < 0 || uz < 0 || ux >= int(fr) || uz >= int(fr)) return 0u;
    int cx = ux >> 1;
    int cz = uz >> 1;
    int dx = (ux & 1) == 0 ? -1 : 1;
    int dz = (uz & 1) == 0 ? -1 : 1;
    uint c00 = sample_fow(cx, cz);
    uint c10 = sample_fow(cx + dx, cz);
    uint c01 = sample_fow(cx, cz + dz);
    uint c11 = sample_fow(cx + dx, cz + dz);
    uint h0 = (3u * c00 + c10 + 2u) >> 2u;
    uint h1 = (3u * c01 + c11 + 2u) >> 2u;
    return (3u * h0 + h1 + 2u) >> 2u;
}

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= int(fr) || y >= int(fr)) return;
    const uint w[7] = uint[7](1u, 6u, 15u, 20u, 15u, 6u, 1u);
    uint acc_y = 0u;
    for (int ky = -3; ky <= 3; ++ky) {
        uint acc_x = 0u;
        for (int kx = -3; kx <= 3; ++kx) {
            acc_x += w[kx + 3] * sample_upsampled(x + kx, y + ky);
        }
        acc_y += w[ky + 3] * (acc_x >> 6u);
    }
    uint val = (acc_y >> 6u) & 0xFFu;
    uint out_idx = uint(y) * fr + uint(x);
    atomicOr(out_packed[out_idx >> 2u], val << ((out_idx & 3u) * 8u));
}
