#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# autobuild build script: builds the pinned generator, generates cloud_noise.vkcn twice, requires the two
# files to be byte-identical and to match EXPECTED_SHA256, and stages the package contents.
cd "$(dirname "$0")"
exec 4>&1; export BASH_XTRACEFD=4; set -x
set -e
set -u

stage="$(pwd)/stage"
rm -rf "$stage/clouds" "$stage/LICENSES"
mkdir -p "$stage/clouds" "$stage/LICENSES" build

# the generator needs the pinned MSVC toolset (scripts/pinned.cmd); it is Windows only
case "${AUTOBUILD_PLATFORM:-}" in
    windows*|common) ;;
    *) echo "unsupported platform ${AUTOBUILD_PLATFORM:-}" >&2; exit 1 ;;
esac
cmd.exe //c "$(cygpath -w "$(pwd)/scripts/build.cmd")"

gen=build/cloudnoise_gen.exe
"$gen" --fingerprint
"$gen" build/gen1.vkcn
"$gen" build/gen2.vkcn
cmp build/gen1.vkcn build/gen2.vkcn
"$gen" --verify build/gen1.vkcn

actual="$(sha256sum build/gen1.vkcn | cut -d' ' -f1)"
expected="$(grep -v '^#' EXPECTED_SHA256 | head -n1 | cut -d' ' -f1)"
if [ "$actual" != "$expected" ]; then
    echo "cloud_noise.vkcn SHA-256 $actual differs from EXPECTED_SHA256 $expected" >&2
    exit 1
fi

cp build/gen1.vkcn "$stage/clouds/cloud_noise.vkcn"
echo "$actual" > "$stage/clouds/cloud_noise.vkcn.sha256"
cp LICENSES/*.txt "$stage/LICENSES/"
cp README.md "$stage/clouds/README.md"

build=${AUTOBUILD_BUILD_ID:=0}
echo "$(tr -d '\r\n' < VERSION.txt).${build}" > "$stage/VERSION.txt"
