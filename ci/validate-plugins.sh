#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Validates the built plugins: pluginval (VST3, and AU on macOS) and
# clap-validator (CLAP). Downloads pinned releases of both into $TOOLS.
# Usage: ci/validate-plugins.sh [build dir]
set -euo pipefail

BUILD=${1:-build}
ART="$BUILD/plugin/asma_plugin_artefacts/Release"
TOOLS=${TOOLS:-"$BUILD/validators"}
PLUGINVAL_VERSION=v1.0.4
CLAP_VALIDATOR_VERSION=0.4.1
CLAP_VALIDATOR_BUILD=0.4.1-127-g152b982

case "$(uname -s)" in
  Darwin) os=macOS; clap_os=macos-universal ;;
  Linux) os=Linux; clap_os=ubuntu-22.04 ;;
  *) os=Windows; clap_os=windows ;;
esac

mkdir -p "$TOOLS"
fetch() { # url, directory to unpack into
  mkdir -p "$2"
  curl -fsSL "$1" -o "$2/download.zip"
  (cd "$2" && unzip -oq download.zip && for t in *.tar.gz; do [ -e "$t" ] && tar xzf "$t"; done; true)
}
[ -d "$TOOLS/pluginval" ] || fetch \
  "https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_$os.zip" "$TOOLS/pluginval"
[ -d "$TOOLS/clap-validator" ] || fetch \
  "https://github.com/free-audio/clap-validator/releases/download/$CLAP_VALIDATOR_VERSION/clap-validator-$CLAP_VALIDATOR_BUILD-$clap_os.zip" \
  "$TOOLS/clap-validator"

pluginval=$(find "$TOOLS/pluginval" -type f \( -name pluginval -o -name pluginval.exe \) | head -1)
clapval=$(find "$TOOLS/clap-validator" -type f \( -name clap-validator -o -name clap-validator.exe \) | head -1)
chmod +x "$pluginval" "$clapval"

run_pluginval() {
  echo "== pluginval $1"
  "$pluginval" --strictness-level 10 --validate-in-process --validate "$1"
}

run_pluginval "$ART/VST3/asma.vst3"
if [ "$os" = macOS ] && [ -n "${CI:-}" ]; then
  # macOS only finds an AU that is installed; the CI runner is throwaway.
  mkdir -p ~/Library/Audio/Plug-Ins/Components
  cp -R "$ART/AU/asma.component" ~/Library/Audio/Plug-Ins/Components/
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  run_pluginval ~/Library/Audio/Plug-Ins/Components/asma.component
fi

echo "== clap-validator"
# param-conversions divides by the parameter count, and asma has none: the
# validator crashes on itself (it says so). Every other test runs.
"$clapval" validate --exclude '^param-conversions$' "$ART/CLAP/asma.clap"
