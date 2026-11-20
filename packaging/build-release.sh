#!/usr/bin/env bash
# Buduje przenośną paczkę LogiHub (z Qt) w czystym kontenerze fedora:42 (podman).
#   packaging/build-release.sh 0.1.0   ->  dist/logihub-0.1.0-x86_64.tar.gz
set -euo pipefail
SRC="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
VERSION="${1:-dev}"
mkdir -p "$SRC/dist"
podman run --rm \
    -v "$SRC:/src:ro,z" \
    -v "$SRC/dist:/out:z" \
    registry.fedoraproject.org/fedora:42 \
    bash /src/packaging/make-bundle.sh "$VERSION"
