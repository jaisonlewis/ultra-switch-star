#!/usr/bin/env bash
# Build the .nro inside the devkitPro Docker image — no local toolchain needed.
set -e
export MSYS_NO_PATHCONV=1      # Git Bash would mangle the -w path
cd "$(dirname "$0")"
docker run --rm -v "$PWD/..:/src" -w /src/switch --entrypoint bash \
    devkitpro/devkita64:latest -c '
        source /opt/devkitpro/devkita64.sh
        # devkita64.sh forgets LD/NM; the link rule needs the cross tools
        export LD=aarch64-none-elf-g++ NM=aarch64-none-elf-nm
        make "$@"
    ' -- "$@"
