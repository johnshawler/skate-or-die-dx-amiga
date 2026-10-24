#!/bin/bash
# Artifact manifest with SHA-256 checksums of the product build (dist/) and the identities of the inputs and tools it was built from.
# usage: make_manifest.sh <out.md> [extra files to list...]       (run after ./build.sh and tools/mkhdf.sh)
set -e
cd "$(dirname "$0")/.."
OUT=${1:?out file}; shift || true
ROOT=$(git rev-parse --show-toplevel)
{
echo "# Artifact manifest"
echo
echo "Generated $(date -u +%Y-%m-%dT%H:%M:%SZ) from commit \`$(git rev-parse HEAD)\` (working tree: $(git status --porcelain -- . | wc -l) modified/untracked paths under ecs/game)."
echo
echo "## Product (dist/)"
echo '```'
(cd dist && sha256sum SkateOrDie $(ls data | sed 's#^#data/#'))
echo '```'
for f in "$@"; do echo; echo "## $(basename "$f") (packaged from dist/; archive and HDF bytes contain timestamps, so their hashes change with every packaging run)"; echo '```'; (cd "$(dirname "$f")" && sha256sum "$(basename "$f")"); echo '```'; done
echo
echo "## Tools"
echo '```'
echo "m68k-linux-gnu-gcc: $(m68k-linux-gnu-gcc --version | head -1)"
echo "m68k-linux-gnu-ld : $(m68k-linux-gnu-ld --version | head -1)"
echo "python3           : $(python3 --version)"
echo "FS-UAE            : $(fs-uae --version 2>&1 | head -1)"
echo "amitools (xdftool/rdbtool): $(python3 -c 'import amitools,sys; print(getattr(amitools, "__version__", "installed"))' 2>/dev/null || echo installed)"
echo '```'
echo
echo "## Firmware the product needs to run (not included in dist/)"
echo '```'
(cd "$ROOT/roms" && sha256sum Kickstart*A1200*.rom)
echo '```'
} > "$OUT"
echo "wrote $OUT"
