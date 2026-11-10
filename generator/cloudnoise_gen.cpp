// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anne Skydancer (Vulkanstorm)
//
// cloudnoise_gen: writes cloud_noise.vkcn, the volumetric clouds' noise textures.
//
// The base (128^3) and erosion (32^3) volumes are exactly the "unpacked" textures of
// TileableVolumeNoise's main.cpp (Sebastien Hillaire, MIT, https://github.com/sebh/TileableVolumeNoise,
// commit 1f879a26b89aaa4b5a7f112f14ec149d08057661), computed by the same Tileable3dNoise functions
// (vendor/TileableVolumeNoise) on glm 0.9.7. The loop bodies below are copied from main.cpp unchanged:
// the coordinate (s, t, r) / N, the unsigned char(255 * v) truncation, the channel meaning and the
// address r*N*N + t*N + s. Only the output differs (this container instead of TGA files), the "packed"
// variants main.cpp also writes are not needed, and the PPL parallel_for is a plain loop over s run on
// std::thread workers (every texel is independent of the others, so the order does not matter).
//
// The curl texture (128^2, RG) is NOT part of TileableVolumeNoise, which has no curl noise. It is a
// Vulkanstorm derivation from TileableVolumeNoise's own periodic Perlin noise; see make_curl().
//
// The bytes are only reproducible with the pinned toolchain (MSVC v145 14.51, Windows SDK, /MT /fp:precise,
// x64 SSE2): TileableVolumeNoise's hash calls sinf, and the UCRT's sinf is 1 ulp off the correctly rounded
// value on some of its arguments. The generator therefore checks a fingerprint of sinf over the whole hash
// domain before it generates, and refuses to run on a mismatch.
//
// Usage:
//   cloudnoise_gen <out.vkcn>        generate (after the sinf check)
//   cloudnoise_gen --verify <file>   parse a container and check every entry's SHA-256
//   cloudnoise_gen --fingerprint     print the sinf fingerprint and exit

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "TileableVolumeNoise.h"

#if !defined(_MSC_VER) || !defined(_M_X64) || _MSC_VER < 1950 || _MSC_VER >= 1960
#error "cloudnoise_gen must be built with MSVC v145 (14.5x) for x64; see README.md (pinned toolchain)."
#endif
#if defined(__AVX__) || defined(__AVX2__)
#error "cloudnoise_gen must be built for x64 SSE2 (no /arch:AVX*)."
#endif
#if !defined(_MT) || defined(_DLL)
#error "cloudnoise_gen must be built with /MT (static UCRT): the sinf in the exe is part of the pinned toolchain."
#endif

extern "C" int __cdecl _set_FMA3_enable(int);

namespace {

constexpr const char* kGeneratorId = "TVN 1f879a26 glm 0.9.7 gen 1.0.0"; // exactly 32 bytes
constexpr uint64_t kSinfFingerprint = 0xa930a79e891ebb5aull;                  // FNV-1a of sinf over TVN's hash arguments
constexpr uint32_t kBaseSize = 128, kErosionSize = 32, kCurlSize = 128;

// ---------------------------------------------------------------- SHA-256
struct Sha256 {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len = 0;
    size_t fill = 0;
    Sha256() {
        static const uint32_t i[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        std::memcpy(h, i, sizeof h);
    }
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        len += n;
        while (n) {
            const size_t take = std::min(n, 64 - fill);
            std::memcpy(buf + fill, p, take);
            fill += take; p += take; n -= take;
            if (fill == 64) { block(buf); fill = 0; }
        }
    }
    void finish(uint8_t out[32]) {
        const uint64_t bits = len * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (fill != 56) update(&zero, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; ++i) l[i] = uint8_t(bits >> (56 - 8 * i));
        update(l, 8);
        for (int i = 0; i < 8; ++i) {
            out[4 * i] = uint8_t(h[i] >> 24); out[4 * i + 1] = uint8_t(h[i] >> 16);
            out[4 * i + 2] = uint8_t(h[i] >> 8); out[4 * i + 3] = uint8_t(h[i]);
        }
    }
};

void sha256(const void* data, size_t n, uint8_t out[32]) { Sha256 s; s.update(data, n); s.finish(out); }

std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

// ---------------------------------------------------------------- the sinf guard
// FNV-1a over the bits of sinf(k + 1.951f), k = 0 .. 171*129: every argument TileableVolumeNoise's hash can get.
uint64_t sinf_fingerprint() {
    uint64_t h = 1469598103934665603ull;
    for (int k = 0; k <= 171 * 129; ++k) {
        volatile float x = (float)k + 1.951f;
        const float a = std::sin((float)x); // the float overload, as TileableVolumeNoise::hash calls it
        uint32_t u;
        std::memcpy(&u, &a, 4);
        h = (h ^ u) * 1099511628211ull;
    }
    return h;
}

// ---------------------------------------------------------------- parallel helper
template <class F> void parallel_for_s(int n, F&& body) {
    std::atomic<int> next{ 0 };
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < workers; ++i)
        pool.emplace_back([&] { for (int s; (s = next++) < n;) body(s); });
    for (auto& t : pool) t.join();
}

// the remap function of main.cpp
float remap(float originalValue, float originalMin, float originalMax, float newMin, float newMax)
{
    return newMin + (((originalValue - originalMin) / (originalMax - originalMin)) * (newMax - newMin));
}

// ---------------------------------------------------------------- base: main.cpp, cloudBaseShapeTexels (unpacked)
std::vector<uint8_t> make_base() {
    const float frequenceMul[6] = { 2.0f,8.0f,14.0f,20.0f,26.0f,32.0f };
    int cloudBaseShapeTextureSize = 128;
    std::vector<uint8_t> storage((size_t)cloudBaseShapeTextureSize * cloudBaseShapeTextureSize * cloudBaseShapeTextureSize * 4);
    uint8_t* cloudBaseShapeTexels = storage.data();
    parallel_for_s(cloudBaseShapeTextureSize, [&](int s)
    {
        const glm::vec3 normFact = glm::vec3(1.0f / float(cloudBaseShapeTextureSize));
        for (int t = 0; t<cloudBaseShapeTextureSize; t++)
        {
            for (int r = 0; r<cloudBaseShapeTextureSize; r++)
            {
                glm::vec3 coord = glm::vec3(s, t, r) * normFact;

                // Perlin FBM noise
                const int octaveCount = 3;
                const float frequency = 8.0f;
                float perlinNoise = Tileable3dNoise::PerlinNoise(coord, frequency, octaveCount);

                float PerlinWorleyNoise = 0.0f;
                {
                    const float cellCount = 4;
                    const float worleyNoise0 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * frequenceMul[0]));
                    const float worleyNoise1 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * frequenceMul[1]));
                    const float worleyNoise2 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * frequenceMul[2]));
                    const float worleyNoise3 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * frequenceMul[3]));
                    const float worleyNoise4 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * frequenceMul[4]));
                    const float worleyNoise5 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * frequenceMul[5]));
                    (void)worleyNoise3; (void)worleyNoise4; (void)worleyNoise5; // unused in main.cpp too

                    float worleyFBM = worleyNoise0*0.625f + worleyNoise1*0.25f + worleyNoise2*0.125f;

                    PerlinWorleyNoise = remap(perlinNoise, 0.0f, 1.0f, worleyFBM, 1.0f);
                }

                const float cellCount = 4;
                float worleyNoise0 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 1));
                float worleyNoise1 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 2));
                float worleyNoise2 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 4));
                float worleyNoise3 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 8));
                float worleyNoise4 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 16));
                (void)worleyNoise0;

                // Three frequency of Worley FBM noise
                float worleyFBM0 = worleyNoise1*0.625f + worleyNoise2*0.25f + worleyNoise3*0.125f;
                float worleyFBM1 = worleyNoise2*0.625f + worleyNoise3*0.25f + worleyNoise4*0.125f;
                float worleyFBM2 = worleyNoise3*0.75f + worleyNoise4*0.25f;

                int addr = r*cloudBaseShapeTextureSize*cloudBaseShapeTextureSize + t*cloudBaseShapeTextureSize + s;

                addr *= 4;
                cloudBaseShapeTexels[addr] = (unsigned char)(255.0f*PerlinWorleyNoise);
                cloudBaseShapeTexels[addr + 1] = (unsigned char)(255.0f*worleyFBM0);
                cloudBaseShapeTexels[addr + 2] = (unsigned char)(255.0f*worleyFBM1);
                cloudBaseShapeTexels[addr + 3] = (unsigned char)(255.0f*worleyFBM2);
            }
        }
    });
    return storage;
}

// ---------------------------------------------------------------- erosion: main.cpp, cloudErosionTexels (unpacked)
std::vector<uint8_t> make_erosion() {
    int cloudErosionTextureSize = 32;
    std::vector<uint8_t> storage((size_t)cloudErosionTextureSize * cloudErosionTextureSize * cloudErosionTextureSize * 4);
    uint8_t* cloudErosionTexels = storage.data();
    parallel_for_s(cloudErosionTextureSize, [&](int s)
    {
        const glm::vec3 normFact = glm::vec3(1.0f / float(cloudErosionTextureSize));
        for (int t = 0; t<cloudErosionTextureSize; t++)
        {
            for (int r = 0; r<cloudErosionTextureSize; r++)
            {
                glm::vec3 coord = glm::vec3(s, t, r) * normFact;

                // 3 octaves
                const float cellCount = 2;
                float worleyNoise0 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 1));
                float worleyNoise1 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 2));
                float worleyNoise2 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 4));
                float worleyNoise3 = (1.0f - Tileable3dNoise::WorleyNoise(coord, cellCount * 8));
                float worleyFBM0 = worleyNoise0*0.625f + worleyNoise1*0.25f + worleyNoise2*0.125f;
                float worleyFBM1 = worleyNoise1*0.625f + worleyNoise2*0.25f + worleyNoise3*0.125f;
                float worleyFBM2 = worleyNoise2*0.75f + worleyNoise3*0.25f;

                int addr = r*cloudErosionTextureSize*cloudErosionTextureSize + t*cloudErosionTextureSize + s;
                addr *= 4;
                cloudErosionTexels[addr] = (unsigned char)(255.0f*worleyFBM0);
                cloudErosionTexels[addr + 1] = (unsigned char)(255.0f*worleyFBM1);
                cloudErosionTexels[addr + 2] = (unsigned char)(255.0f*worleyFBM2);
                cloudErosionTexels[addr + 3] = (unsigned char)(255.0f);
            }
        }
    });
    return storage;
}

// ---------------------------------------------------------------- curl: a Vulkanstorm derivation, not TileableVolumeNoise
// Stream function psi(x, y) = Tileable3dNoise::PerlinNoise(vec3(x, y, 0), 8, 1), TileableVolumeNoise's own periodic
// Perlin noise (8 cells over the unit square, z = 0, one octave; the generator has no seed, TileableVolumeNoise's
// hash is fixed). The curl c = (dpsi/dy, -dpsi/dx), as a slope per noise cell, is taken by central differences
// with a step of 0.05 cell (0.05 / 8 in coordinate units), sampled at the texel centres, and stored as clamp(c * 0.5 + 0.5) rounded to
// the nearest 8-bit value in R and G, B = 0, A = 255 (the convention of Aurora's cloud noise file).
std::vector<uint8_t> make_curl() {
    const int n = (int)kCurlSize;
    const float eCell = 0.05f;        // the step, in cells of the noise (8 cells over the unit square)
    const float e = eCell / 8.0f;     // the same step in coordinate units
    std::vector<uint8_t> out((size_t)n * n * 4);
    auto psi = [](float x, float y) {
        x -= std::floor(x); y -= std::floor(y); // periodic: wrap into [0, 1)
        return Tileable3dNoise::PerlinNoise(glm::vec3(x, y, 0.0f), 8.0f, 1);
    };
    auto unorm = [](float v) { return (uint8_t)(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); };
    parallel_for_s(n, [&](int y) {
        for (int x = 0; x < n; ++x) {
            const float px = (x + 0.5f) / n, py = (y + 0.5f) / n;
            const float dx = psi(px + e, py) - psi(px - e, py);
            const float dy = psi(px, py + e) - psi(px, py - e);
            const float c0 = dy / (2.0f * eCell), c1 = -dx / (2.0f * eCell); // slope per cell
            uint8_t* t = &out[((size_t)y * n + x) * 4];
            t[0] = unorm(c0 * 0.5f + 0.5f); t[1] = unorm(c1 * 0.5f + 0.5f); t[2] = 0; t[3] = 255;
        }
    });
    return out;
}

// ---------------------------------------------------------------- container
// Little endian. Header (256 bytes): magic "VKCLDNZ1", u32 format_version, u32 header_bytes, u32 entry_count,
// 32-byte generator id; then per entry (68 bytes): u32 kind (1 base, 2 erosion, 3 curl), u32 format (1 = RGBA8
// UNORM), u32 w, h, d, u64 offset, u64 length, 32-byte SHA-256 of the entry's bytes. The payloads follow, in order.
constexpr size_t kHeaderBytes = 256, kEntryBytes = 68;
struct Entry { uint32_t kind, format, w, h, d; uint64_t offset, length; uint8_t sha[32]; };

void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void put64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | (uint64_t(get32(p + 4)) << 32); }

std::vector<uint8_t> make_container(const std::vector<uint8_t>& base, const std::vector<uint8_t>& erosion, const std::vector<uint8_t>& curl) {
    const std::vector<uint8_t>* payload[3] = { &base, &erosion, &curl };
    const uint32_t kind[3] = { 1, 2, 3 }, w[3] = { kBaseSize, kErosionSize, kCurlSize },
                   h[3] = { kBaseSize, kErosionSize, kCurlSize }, d[3] = { kBaseSize, kErosionSize, 1 };
    std::vector<uint8_t> out;
    out.insert(out.end(), { 'V', 'K', 'C', 'L', 'D', 'N', 'Z', '1' });
    put32(out, 1); put32(out, (uint32_t)kHeaderBytes); put32(out, 3);
    out.insert(out.end(), kGeneratorId, kGeneratorId + 32);
    uint64_t offset = kHeaderBytes;
    for (int i = 0; i < 3; ++i) {
        put32(out, kind[i]); put32(out, 1); put32(out, w[i]); put32(out, h[i]); put32(out, d[i]);
        put64(out, offset); put64(out, payload[i]->size());
        uint8_t sha[32];
        sha256(payload[i]->data(), payload[i]->size(), sha);
        out.insert(out.end(), sha, sha + 32);
        offset += payload[i]->size();
    }
    out.resize(kHeaderBytes, 0);
    for (int i = 0; i < 3; ++i) out.insert(out.end(), payload[i]->begin(), payload[i]->end());
    return out;
}

bool read_file(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n < 0 ? 0 : (size_t)n);
    const bool ok = n >= 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

int verify(const char* path) {
    std::vector<uint8_t> b;
    if (!read_file(path, b)) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
    if (b.size() < kHeaderBytes || std::memcmp(b.data(), "VKCLDNZ1", 8) != 0) { std::fprintf(stderr, "bad magic\n"); return 1; }
    if (get32(&b[8]) != 1 || get32(&b[12]) != kHeaderBytes || get32(&b[16]) != 3) { std::fprintf(stderr, "bad version/header\n"); return 1; }
    const uint32_t expect[3][4] = { { 1, kBaseSize, kBaseSize, kBaseSize }, { 2, kErosionSize, kErosionSize, kErosionSize }, { 3, kCurlSize, kCurlSize, 1 } };
    uint64_t end = kHeaderBytes;
    for (int i = 0; i < 3; ++i) {
        const uint8_t* e = &b[52 + i * kEntryBytes];
        const uint64_t off = get64(e + 20), len = get64(e + 28);
        const uint64_t want = (uint64_t)expect[i][1] * expect[i][2] * expect[i][3] * 4;
        if (get32(e) != expect[i][0] || get32(e + 4) != 1 || get32(e + 8) != expect[i][1] || get32(e + 12) != expect[i][2] ||
            get32(e + 16) != expect[i][3] || len != want || off != end || off + len > b.size()) {
            std::fprintf(stderr, "bad entry %d\n", i); return 1;
        }
        uint8_t sha[32];
        sha256(&b[(size_t)off], (size_t)len, sha);
        if (std::memcmp(sha, e + 36, 32) != 0) { std::fprintf(stderr, "entry %d: SHA-256 mismatch\n", i); return 1; }
        end = off + len;
    }
    if (end != b.size()) { std::fprintf(stderr, "trailing bytes\n"); return 1; }
    uint8_t sha[32];
    sha256(b.data(), b.size(), sha);
    std::printf("ok %s\n", hex(sha, 32).c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    _set_FMA3_enable(0); // measured to make no difference on the pinned toolchain; defensive

    // SHA-256 self-test (FIPS 180-2 "abc"), so that a bad hash implementation cannot bless a bad file.
    {
        uint8_t d[32];
        sha256("abc", 3, d);
        if (hex(d, 32) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") { std::fprintf(stderr, "SHA-256 self-test failed\n"); return 3; }
    }
    if (argc == 2 && std::strcmp(argv[1], "--fingerprint") == 0) {
        std::printf("%016llx\n", (unsigned long long)sinf_fingerprint());
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--verify") == 0) return verify(argv[2]);
    if (argc != 2 || argv[1][0] == '-') { std::fprintf(stderr, "usage: cloudnoise_gen <out.vkcn> | --verify <file> | --fingerprint\n"); return 64; }

    const uint64_t fp = sinf_fingerprint();
    if (fp != kSinfFingerprint) {
        std::fprintf(stderr, "REFUSING TO GENERATE: the C runtime's sinf differs from the pinned one (fingerprint %016llx, expected %016llx).\n"
                             "Build with the pinned toolchain (MSVC v145 14.51, /MT /fp:precise, x64 SSE2); see README.md.\n",
                     (unsigned long long)fp, (unsigned long long)kSinfFingerprint);
        return 2;
    }
    std::printf("sinf fingerprint %016llx: ok\n", (unsigned long long)fp);

    const std::vector<uint8_t> base = make_base(), erosion = make_erosion(), curl = make_curl();
    const std::vector<uint8_t> file = make_container(base, erosion, curl);
    FILE* f = std::fopen(argv[1], "wb");
    if (!f || std::fwrite(file.data(), 1, file.size(), f) != file.size() || std::fclose(f) != 0) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    uint8_t sha[32];
    sha256(file.data(), file.size(), sha);
    std::printf("wrote %s (%zu bytes) sha256 %s\n", argv[1], file.size(), hex(sha, 32).c_str());
    return 0;
}
