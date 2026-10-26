#!/bin/bash
# Opens the simulator window on the SD card image (builds first if needed).
#
#   run_sim.sh [card image] [simulator options...]
#
# Default card: ~/Projects/DrumBoy/sim-card/drumboy.img. Changes the firmware
# makes (presets, saved files) are kept on the image; add --card-copy to start
# from a fresh copy each time. A control socket (build/Simulator/sim.sock) is
# open for Simulator/tools/simctl.

set -eu
SIM_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FW_DIR="$(dirname "$SIM_DIR")"
CARD="$HOME/Projects/DrumBoy/sim-card/drumboy.img"
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then CARD="$1"; shift; fi

ST="$HOME/Library/Application Support/stm32cube/bundles"
export PATH="$ST/cmake/4.4.0+st.1/CMake.app/Contents/bin:$ST/ninja/1.13.2+st.1/bin:$PATH"
cd "$FW_DIR"
[ -d build/Simulator ] || cmake --preset Simulator >/dev/null
cmake --build --preset Simulator | grep -E 'error|FAILED' || true

cd build/Simulator
exec Simulator/DrumboySim --card "$CARD" --control sim.sock "$@"
