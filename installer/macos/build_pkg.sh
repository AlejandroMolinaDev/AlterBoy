#!/usr/bin/env bash
# Builds dist/Metamorfo-<version>-macOS.pkg from the (ad-hoc signed) bundles.
# Installs VST3, AU and CLAP into /Library/Audio/Plug-Ins/* and the app into /Applications.
# Usage: installer/macos/build_pkg.sh <artefacts-dir> <version>
set -euo pipefail

ARTEFACTS="$1"
VERSION="$2"
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" \
         "$STAGE/Library/Audio/Plug-Ins/CLAP" "$STAGE/Applications"
ditto "$ARTEFACTS/VST3/Metamorfo.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/Metamorfo.vst3"
ditto "$ARTEFACTS/AU/Metamorfo.component" "$STAGE/Library/Audio/Plug-Ins/Components/Metamorfo.component"
ditto "$ARTEFACTS/CLAP/Metamorfo.clap" "$STAGE/Library/Audio/Plug-Ins/CLAP/Metamorfo.clap"
ditto "$ARTEFACTS/Standalone/Metamorfo.app" "$STAGE/Applications/Metamorfo.app"

mkdir -p "$ROOT_DIR/dist"
OUT="$ROOT_DIR/dist/Metamorfo-$VERSION-macOS.pkg"
pkgbuild --root "$STAGE" \
         --identifier com.metamorfoaudio.metamorfo.pkg \
         --version "$VERSION" \
         --install-location / \
         --scripts "$ROOT_DIR/installer/macos/scripts" \
         "$OUT"
echo "Built $OUT"
