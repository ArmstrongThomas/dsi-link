#!/bin/sh
# Builds DSiLink.nds inside the pinned BlocksDS Docker image.
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
image='skylyrac/blocksds@sha256:779f6193d1030c62e60d1f0f39c3e32cf4045c0bd224a45c0ceadf9d9ab01a51'
docker run --rm --network none --mount "type=bind,source=$repo,target=/work" --workdir /work "$image" make -B -j2
sha256sum "$repo/DSiLink.nds" 2>/dev/null || shasum -a 256 "$repo/DSiLink.nds"
