# 3p-cloudnoise

The noise textures of the Vulkanstorm volumetric clouds, as one data file, `cloud_noise.vkcn`, made by a
deterministic generator and shipped as an [Autobuild][] package (`cloudnoise`, platform `common`).
Aurora uses the same release asset.

## What is in the file

| Entry | Size | Format | Source |
|---|---|---|---|
| base | 128 x 128 x 128 | RGBA8 UNORM | **TileableVolumeNoise**, the "unpacked" shape texture of its `main.cpp`: R = Perlin-Worley, G/B/A = three Worley FBMs |
| erosion | 32 x 32 x 32 | RGBA8 UNORM | **TileableVolumeNoise**, the "unpacked" erosion texture of its `main.cpp`: R/G/B = three Worley FBMs, A = 255 |
| curl | 128 x 128 | RGBA8 UNORM | **not TileableVolumeNoise** (it has no curl noise): a derivation by this project, see below |

Base and erosion are exactly TileableVolumeNoise (Sébastien Hillaire, MIT,
<https://github.com/sebh/TileableVolumeNoise>, commit `1f879a26b89aaa4b5a7f112f14ec149d08057661`), built on GLM 0.9.7
(MIT), both vendored unchanged in `vendor/TileableVolumeNoise/`. `generator/cloudnoise_gen.cpp` keeps the loop bodies
of `main.cpp` verbatim (the coordinate `(s, t, r) / N`, the `unsigned char(255 * v)` truncation, the channels, the
address `r*N*N + t*N + s`, i.e. x = s, y = t, z = r of the 3D texture). Only the output differs (this container instead
of TGA files); the "packed" variants of `main.cpp` are not made; PPL's `parallel_for` is a loop over `s` on
`std::thread` workers (every texel is independent, so the order does not matter).

**Curl (Vulkanstorm derivation).** psi(x, y) = `Tileable3dNoise::PerlinNoise(vec3(x, y, 0), 8, 1)`, TileableVolumeNoise's
own periodic Perlin noise (8 cells over the unit square, one octave). TileableVolumeNoise's hash is fixed, so there is
no seed: "seed: none, curl z = 0". The curl c = (dpsi/dy, -dpsi/dx) is the slope per noise cell, by central differences
with a step of 0.05 cell, sampled at the texel centres, and stored as `clamp(c * 0.5 + 0.5)` rounded to 8 bits in R and
G, B = 0, A = 255 (the convention of Aurora's cloud noise file).

## Container `cloud_noise.vkcn`

Little endian, uncompressed (installers and archives compress it), 8,585,472 bytes.

Header, 256 bytes: magic `VKCLDNZ1`; u32 format_version (1); u32 header_bytes (256); u32 entry_count (3); 32 bytes of
generator id (`TVN 1f879a26 glm 0.9.7 gen 1.0.0`); then three entries of 68 bytes: u32 kind (1 base, 2 erosion, 3
curl), u32 format (1 = RGBA8 UNORM), u32 width, height, depth (curl: depth 1), u64 offset, u64 length, 32 bytes SHA-256
of the entry's bytes. The header is exactly 256 bytes (52 + 3 x 68, no padding); the payloads follow in order (base, erosion, curl).
`cloudnoise_gen --verify <file>` parses a container and checks the structure and every entry's hash.

The whole-file SHA-256 is pinned in [`EXPECTED_SHA256`](EXPECTED_SHA256). The viewer compiles it in, so a new noise
version needs a viewer change: they move in lockstep.

## Why the toolchain is pinned

TileableVolumeNoise's hash is `fract(sin(n + 1.951f) * 43758.5453f)`. The Universal C Runtime's `sinf` is 1 ulp off
the correctly rounded value on 30 of the 22,060 arguments the hash can get, and MinGW's `sinf` is not, so other
toolchains give different bytes (184 of 655,360 differed in the survey). Byte identity therefore holds only with:

- MSVC v145 14.51 (`cl` 19.51), x64, `/MT /O2 /fp:precise`, SSE2 (no `/arch:AVX*`), `_set_FMA3_enable(0)`;
- the Windows SDK 10.0.26100.0 (its static UCRT is the `sinf` in the exe), see [`scripts/pinned.cmd`](scripts/pinned.cmd).

Before it generates anything, `cloudnoise_gen` computes an FNV-1a fingerprint of `sinf` over all 22,060 hash arguments
and **refuses to run** unless it is `a930a79e891ebb5a` (exit code 2). The source also refuses to compile without
MSVC 19.5x, x64, `/MT` and without AVX.

## Building

```
scripts\build.cmd                      REM build\cloudnoise_gen.exe with the pinned toolset
build\cloudnoise_gen.exe out.vkcn      REM about 5 s on a 16-core machine
build\cloudnoise_gen.exe --verify out.vkcn
build\cloudnoise_gen.exe --fingerprint
```

CI ([`.github/workflows/build.yaml`](.github/workflows/build.yaml)) builds the generator, generates twice, requires the
two files to be byte-identical and to have the pinned SHA-256, and (on a tag `v*`) packs
`cloudnoise-<version>.<run>-common-<run>.tar.zst` with `autobuild package` and publishes a GitHub release with the
package, `cloud_noise.vkcn` and its SHA-256.

The package contains `clouds/cloud_noise.vkcn`, `clouds/cloud_noise.vkcn.sha256`, `clouds/README.md` and
`LICENSES/{TileableVolumeNoise,glm,cloudnoise}.txt`.

## Licences

- `generator/`, `scripts/`, `build-cmd.sh`, `autobuild.xml`: MIT (see [`LICENSE`](LICENSE)); chosen so that Aurora
  (GPL-3.0) and Vulkanstorm (GPL-3.0-or-later) can both use the code.
- `vendor/TileableVolumeNoise/`: MIT, Copyright (c) 2017 Sébastien Hillaire.
- `vendor/TileableVolumeNoise/glm/`: GLM 0.9.7, MIT ("Happy Bunny" licence), Copyright (c) 2005 - 2015 G-Truc Creation.
  GLM is only used to run the generator; it is not part of the data file.
- `cloud_noise.vkcn`: generated by the code above from TileableVolumeNoise; it carries the notices in `LICENSES/`.

[Autobuild]: https://wiki.secondlife.com/wiki/Autobuild
