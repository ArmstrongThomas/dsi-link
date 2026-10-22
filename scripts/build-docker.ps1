# Builds DSiLink.nds inside the pinned BlocksDS Docker image (Docker Desktop required).
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$image = 'skylyrac/blocksds@sha256:779f6193d1030c62e60d1f0f39c3e32cf4045c0bd224a45c0ceadf9d9ab01a51'
& docker run --rm --network none --mount "type=bind,source=$repo,target=/work" --workdir /work $image make -B -j2
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Get-FileHash (Join-Path $repo 'DSiLink.nds') -Algorithm SHA256
