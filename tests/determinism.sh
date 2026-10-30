#!/bin/sh
# Generates a fixed, option-rich workload with bicgen and compares one hash of
# all the output against tests/golden.sha256. A mismatch means output for some
# (seed, options) pair changed: either deliberate (re-run with --update after
# reviewing) or an accident.
#
#   tests/determinism.sh [--update]       needs build/bicgen and the NWN1 install
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gen="$root/build/bicgen"
golden="$here/golden.sha256"
work=$(mktemp -d "${TMPDIR:-/tmp}/bicgen-det.XXXXXX")
trap 'rm -rf "$work"' EXIT

[ -x "$gen" ] || { echo "build/bicgen missing; run make" >&2; exit 2; }

c=0
while [ "$c" -le 10 ]; do
    r=0
    while [ "$r" -le 6 ]; do
        "$gen" --class "$c" --race "$r" --seed $((c * 1000 + r)) --count 3 \
            --out-dir "$work" >/dev/null
        r=$((r + 1))
    done
    c=$((c + 1))
done
"$gen" --class wizard --race elf --gender f --align NG --str 10 --dex 14 \
    --con 12 --int 16 --wis 10 --cha 8 --name "Ana Test" --seed 4242 \
    --out-dir "$work" >/dev/null

count=$(ls "$work"/*.bic | wc -l | tr -d ' ')
sum=$(cd "$work" && sha256sum ./*.bic | sort -k2 | sha256sum | cut -d' ' -f1)
line="$sum  $count files"

# Specialist wizards live in their own workload so adding them never changes
# the hash above.
spec="$work/spec"
mkdir "$spec"
s=1
while [ "$s" -le 8 ]; do
    "$gen" --class wizard --race "$((s % 7))" --school "$s" --seed $((7000 + s)) \
        --count 3 --out-dir "$spec" >/dev/null
    s=$((s + 1))
done
"$gen" --class wizard --school random --seed 8000 --count 24 \
    --out-dir "$spec" >/dev/null
scount=$(ls "$spec" | wc -l | tr -d ' ')
ssum=$(cd "$spec" && sha256sum ./*.bic | sort -k2 | sha256sum | cut -d' ' -f1)
sline="$ssum  $scount files (specialist wizards)"
line="$line
$sline"

# Leveled and multiclass builds: another workload, so the hashes above stay put.
lev="$work/lev"
mkdir "$lev"
l=2
while [ "$l" -le 20 ]; do
    "$gen" --level "$l" --max-classes 3 --seed $((9000 + l)) --count 2 \
        --out-dir "$lev" >/dev/null
    l=$((l + 3))
done
"$gen" --classes "fighter:5,wizard:3,rogue:2" --hp avg --seed 9100 --out-dir "$lev" >/dev/null
"$gen" --classes "cleric:6,sorcerer:4" --hp max --seed 9101 --out-dir "$lev" >/dev/null
"$gen" --classes "wizard:8,ranger:4" --school necromancy --seed 9102 --out-dir "$lev" >/dev/null
"$gen" --class druid --level 20 --seed 9103 --out-dir "$lev" >/dev/null
"$gen" --class fighter --level 12 --variety 0 --seed 9104 --out-dir "$lev" >/dev/null
"$gen" --class fighter --level 12 --variety 3 --seed 9105 --out-dir "$lev" >/dev/null
"$gen" --class rogue --level 15 --variety 1 --seed 9106 --out-dir "$lev" >/dev/null
"$gen" --class wizard --school necromancy --level 12 --variety 3 --seed 9107 --out-dir "$lev" >/dev/null
"$gen" --spec "$root/examples/duelist.ini" --out-dir "$lev" >/dev/null
"$gen" --class cleric --level 8 --pin "Extra Turning@3, Combat Casting" --avoid "Toughness" --seed 9108 --out-dir "$lev" >/dev/null
lcount=$(ls "$lev" | wc -l | tr -d ' ')
lsum=$(cd "$lev" && sha256sum ./*.bic | sort -k2 | sha256sum | cut -d' ' -f1)
line="$line
$lsum  $lcount files (leveled and multiclass)"

# A random class with an explicit alignment draws only from classes that allow
# it (a fourth workload, so the hashes above stay put).
al="$work/align"
mkdir "$al"
for a in LG NG CG LN N CN LE NE CE; do
    "$gen" --align "$a" --seed 9200 --count 6 --out-dir "$al" >/dev/null
    "$gen" --align "$a" --level 5 --max-classes 2 --seed 9300 --count 2 --out-dir "$al" >/dev/null
done
acount=$(ls "$al" | wc -l | tr -d ' ')
asum=$(cd "$al" && sha256sum ./*.bic | sort -k2 | sha256sum | cut -d' ' -f1)
line="$line
$asum  $acount files (random class, explicit alignment)"

if [ "${1:-}" = "--update" ]; then
    printf "%s\n" "$line" > "$golden"
    echo "golden updated: $line"
    exit 0
fi
if [ ! -f "$golden" ]; then
    echo "no golden file; run with --update first" >&2
    exit 2
fi
if [ "$(cat "$golden")" = "$line" ]; then
    echo "determinism ok: $line"
else
    echo "OUTPUT CHANGED" >&2
    echo "  expected: $(cat "$golden")" >&2
    echo "  got:      $line" >&2
    exit 1
fi
