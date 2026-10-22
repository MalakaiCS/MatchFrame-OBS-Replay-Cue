#!/bin/zsh
# Builds obs-replay-cue.plugin against an installed OBS.app using only the
# Xcode Command Line Tools.
#
# Required environment (all must match the same OBS release):
#   OBS_APP     path to OBS.app                    e.g. /Applications/OBS.app
#   OBS_SOURCE  obs-studio source tree             (OBS-Studio-<ver>-Sources.tar.gz)
#   OBS_DEPS    obs-deps macOS package             (macos-deps-<date>-<arch>.tar.xz)
#   QT_DIR      obs-deps Qt 6 package              (macos-deps-qt6-<date>-<arch>.tar.xz)
# Optional:
#   CMAKE       cmake binary (default: cmake on PATH)
#   SDKROOT     macOS SDK to build with
#   BUILD_DIR   build directory (default: build_macos_dev in the repo)
#
# Usage: tools/macos-dev/build.sh [--install]
#   --install copies the plugin to ~/Library/Application Support/obs-studio/plugins

set -euo pipefail

here=${0:A:h}
root=${here:h:h}
cmake_bin=${CMAKE:-cmake}
build_dir=${BUILD_DIR:-$root/build_macos_dev}

for var in OBS_APP OBS_SOURCE OBS_DEPS QT_DIR; do
  if [[ -z ${(P)var:-} || ! -e ${(P)var} ]]; then
    print -u2 "error: set $var (see the header of $0)"
    exit 1
  fi
done

"$cmake_bin" -S "$here" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
  ${SDKROOT:+-DCMAKE_OSX_SYSROOT=$SDKROOT} \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DOBS_APP="$OBS_APP" \
  -DOBS_SOURCE="$OBS_SOURCE" \
  -DOBS_DEPS="$OBS_DEPS"

"$cmake_bin" --build "$build_dir" --parallel

plugin="$build_dir/obs-replay-cue.plugin"
print "built $plugin"

if [[ ${1:-} == --install ]]; then
  dest="$HOME/Library/Application Support/obs-studio/plugins"
  mkdir -p "$dest"
  rm -rf "$dest/obs-replay-cue.plugin"
  ditto "$plugin" "$dest/obs-replay-cue.plugin"
  print "installed to $dest"
fi
