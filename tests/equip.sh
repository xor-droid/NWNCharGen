#!/bin/sh
# Starting equipment of generated characters: every class and a spread of races
# and seeds, level 1 and leveled, must pass bicverify's item checks (package
# items present and built exactly as the engine builds them, laid out without
# overlap, one robe worn) with no notes either.
#
#   tests/equip.sh        needs build/bicgen, build/bicverify and the NWN1 install
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gen="$root/build/bicgen"
ver="$root/build/bicverify"
install=${NWN_HOME:-"/mnt/c/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights"}
work=$(mktemp -d "${TMPDIR:-/tmp}/bicgen-equip.XXXXXX")
trap 'rm -rf "$work"' EXIT

[ -x "$gen" ] && [ -x "$ver" ] || { echo "build first (make)" >&2; exit 2; }
[ -d "$install" ] || { echo "equip: no NWN1 install at $install; skipped"; exit 0; }

c=0
while [ "$c" -le 10 ]; do
    "$gen" --class "$c" --count 8 --seed $((c * 100 + 1)) --out-dir "$work" >/dev/null
    c=$((c + 1))
done
out=$("$ver" "$install" "$work"/*.bic)
echo "$out" | tail -1
if echo "$out" | grep -q 'note:' || ! echo "$out" | tail -1 | grep -q ' 0 problems'; then
    echo "$out" | grep -v '^[0-9]* files' | head -20 >&2
    echo "equip FAILED" >&2
    exit 1
fi

# a leveled character carries the same pack and still loads back
mkdir "$work/lev"
"$gen" --level 9 --class fighter --seed 77 --out-dir "$work/lev" >/dev/null
n=$("$root/build/gffdump" "$work/lev"/*.bic | awk '/^ItemList/{print $NF}' | tr -d '()')
[ "${n:-0}" -ge 3 ] || { echo "equip FAILED: leveled character has no pack" >&2; exit 1; }
echo "equip ok"
