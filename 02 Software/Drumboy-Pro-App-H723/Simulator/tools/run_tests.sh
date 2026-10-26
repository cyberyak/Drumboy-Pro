#!/bin/bash
# Runs simulator test scripts, each on a fresh clone of the card image.
#
#   run_tests.sh [card image] [test.sim ...]
#
# Defaults: ~/Projects/DrumBoy/sim-card/drumboy.img and Simulator/tests/*.sim.
# Output (log, screenshots, recordings) goes to build/Simulator/test-output/<test>/.
# Exit status is the number of failed tests.

set -u
SIM_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FW_DIR="$(dirname "$SIM_DIR")"
BIN="$FW_DIR/build/Simulator/Simulator/DrumboySim"
CARD="${1:-$HOME/Projects/DrumBoy/sim-card/drumboy.img}"
shift $(( $# > 0 ? 1 : 0 ))
TESTS=("$@")
[ ${#TESTS[@]} -eq 0 ] && TESTS=("$SIM_DIR"/tests/*.sim)

if [ ! -x "$BIN" ]; then
    echo "Simulator not built: cmake --preset Simulator && cmake --build --preset Simulator" >&2
    exit 100
fi

failed=0
for test in "${TESTS[@]}"; do
    name="$(basename "$test" .sim)"
    out="$FW_DIR/build/Simulator/test-output/$name"
    rm -rf "$out" && mkdir -p "$out"
    if "$BIN" --card "$CARD" --card-copy --delay-scale 0.2 --out "$out" --script "$test" > "$out/log.txt" 2>&1; then
        echo "PASS $name"
    else
        echo "FAIL $name (see $out/log.txt)"
        tail -5 "$out/log.txt" | sed 's/^/    /'
        failed=$((failed + 1))
    fi
done
exit $failed
