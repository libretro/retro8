#!/bin/sh
# Builds a host copy of the core around tools/harness.cpp and runs the test cart.
# Pass a folder of carts to also report which of them load and run 30 frames.
#
#   tools/check.sh [CART_DIR]

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/tools/out"
FLAGS="${FLAGS:--O1 -w -D__LIBRETRO__}"
INC="-I$ROOT/src -I$ROOT/src/io -I$ROOT/src/lua -I$ROOT/src/vm -I$ROOT/src/libretro"

mkdir -p "$OUT/obj"
for C in "$ROOT"/src/lua/*.c; do
	gcc $FLAGS -c "$C" -I"$ROOT/src/lua" -o "$OUT/obj/$(basename "$C").o"
done
for C in "$ROOT"/src/io/*.cpp "$ROOT"/src/vm/*.cpp "$ROOT"/src/libretro/slock_wrapper.cpp "$ROOT/tools/harness.cpp"; do
	g++ $FLAGS -std=c++14 -c "$C" $INC -o "$OUT/obj/$(basename "$C").o"
done
g++ -o "$OUT/harness" "$OUT"/obj/*.o -lpthread

printf "Test cart: "
"$OUT/harness" "$ROOT/tests/ops.p8" 1 </dev/null | grep -v "^DONE$"
printf "API cart: "
"$OUT/harness" "$ROOT/tests/api.p8" 1 </dev/null | grep -v "^DONE$"

[ $# -gt 0 ] || exit 0

OK=0
FAIL=0
for CART in "$1"/*; do
	RESULT=$(timeout 15 "$OUT/harness" "$CART" 30 </dev/null 2>&1 || true)
	if printf '%s\n' "$RESULT" | grep -q "^Error on"; then
		FAIL=$((FAIL + 1))
		printf 'ERROR  %s: %s\n' "$(basename "$CART")" "$(printf '%s\n' "$RESULT" | grep -A1 "^Error on" | sed -n 2p)"
	elif printf '%s\n' "$RESULT" | grep -q "^DONE$"; then
		OK=$((OK + 1))
	else
		FAIL=$((FAIL + 1))
		printf 'CRASH  %s\n' "$(basename "$CART")"
	fi
done
printf '%d ran, %d failed\n' "$OK" "$FAIL"
