#!/usr/bin/env bash
# Validates the macOS build: universal binaries, pluginval (VST3 + AU), auval,
# Steinberg VST3 validator and clap-validator.
#
# Usage: validate_macos.sh <artefacts-dir> <pluginval-binary> <vst3-validator> <clap-validator>
set -euo pipefail

ARTEFACTS="$1"
PLUGINVAL="$2"
VST3_VALIDATOR="$3"
CLAP_VALIDATOR="$4"

VST3="$ARTEFACTS/VST3/Metamorfo.vst3"
AU="$ARTEFACTS/AU/Metamorfo.component"
CLAP="$ARTEFACTS/CLAP/Metamorfo.clap"
APP="$ARTEFACTS/Standalone/Metamorfo.app"

echo "::group::Universal binary check"
for binary in "$VST3/Contents/MacOS/Metamorfo" "$AU/Contents/MacOS/Metamorfo" \
              "$CLAP/Contents/MacOS/Metamorfo" "$APP/Contents/MacOS/Metamorfo"; do
  archs="$(lipo -archs "$binary")"
  echo "$binary: $archs"
  [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || { echo "Not a universal binary: $binary"; exit 1; }
done
echo "::endgroup::"

echo "::group::Install AU for auval/pluginval"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$COMPONENTS"
rm -rf "$COMPONENTS/Metamorfo.component"
cp -R "$AU" "$COMPONENTS/"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
auval -a | grep -E "Mtmf" || { echo "AU not registered"; exit 1; }
echo "::endgroup::"

echo "::group::auval (aumf Mmf1 Mtmf)"
auval -strict -v aumf Mmf1 Mtmf
echo "::endgroup::"

echo "::group::pluginval VST3 (strictness 10)"
"$PLUGINVAL" --strictness-level 10 --verbose --validate "$VST3"
echo "::endgroup::"

echo "::group::pluginval AU (strictness 10)"
"$PLUGINVAL" --strictness-level 10 --verbose --validate "$COMPONENTS/Metamorfo.component"
echo "::endgroup::"

echo "::group::Steinberg VST3 validator"
"$VST3_VALIDATOR" "$VST3"
echo "::endgroup::"

echo "::group::clap-validator"
"$CLAP_VALIDATOR" validate "$CLAP"
echo "::endgroup::"

echo "All macOS validations passed."
