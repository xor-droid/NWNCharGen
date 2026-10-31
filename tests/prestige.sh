#!/bin/sh
# Prestige-class entry: builds that meet the requirements verify clean, builds
# that cannot are refused with the reason.
#
#   tests/prestige.sh        needs build/bicgen, build/bicverify and the NWN1 install
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gen="$root/build/bicgen"
ver="$root/build/bicverify"
work=$(mktemp -d "${TMPDIR:-/tmp}/bicgen-pres.XXXXXX")
trap 'rm -rf "$work"' EXIT
fail=0

# refused: the command line, and a phrase the message must contain
while IFS='|' read -r phrase args; do
    # shellcheck disable=SC2086
    msg=$("$gen" $args --out-dir "$work" 2>&1 >/dev/null || true)
    case $msg in
    *"$phrase"*) ;;
    *) echo "FAILED: expected '$phrase' for: $args (got: $msg)" >&2; fail=1 ;;
    esac
done <<'LIST'
base attack bonus|--classes fighter:6,divinechampion:2 --seed 1
base attack bonus|--classes cleric:9,dwarvendefender:3 --seed 1
not open to this race|--classes fighter:8,dwarvendefender:3 --race human --seed 1
cannot be the first class|--class divinechampion --level 3 --seed 1
does not allow this alignment|--classes fighter:8,divinechampion:2 --align CE --seed 1
not modelled yet|--classes sorcerer:8,palemaster:3 --seed 1
needs to cast level 3 arcane|--classes wizard:4,palemaster:3 --seed 1
not open to this race|--classes fighter:6,wizard:1,arcanearcher:3 --race human --seed 1
does not allow this alignment|--classes rogue:6,assassin:3 --align LG --seed 1
limited to|--classes bard:8,harper:7 --seed 1
before character level 21|--classes wizard:8,palemaster:11 --seed 1
LIST

# accepted: every file passes bicverify
n=0
# one more line of options per spec after a bar
while IFS='|' read -r spec extra; do
    for s in 1 2; do
        # shellcheck disable=SC2086
        "$gen" --classes "$spec" $extra --seed "$s" --out-dir "$work/ok" >/dev/null 2>"$work/err" || {
            echo "FAILED: $spec $extra seed $s: $(cat "$work/err")" >&2
            fail=1
        }
        n=$((n + 1))
    done
done <<'LIST'
fighter:9,weaponmaster:4|
druid:6,shifter:5|
sorcerer:5,dragondisciple:10|
bard:6,dragondisciple:4|
fighter:6,wizard:1,arcanearcher:8|--race elf
ranger:6,sorcerer:1,arcanearcher:10|--race halfelf
rogue:8,shadowdancer:6|
bard:6,harper:4|
rogue:6,assassin:6|--align NE
rogue:3,fighter:6,blackguard:10|--align NE
paladin:12,purpledragonknight:5|
LIST
for spec in fighter:9,divinechampion:3 paladin:8,fighter:2,dwarvendefender:5 \
            ranger:8,dwarvendefender:6 cleric:3,fighter:8,divinechampion:6 wizard:8,palemaster:6 \
            fighter:2,wizard:8,palemaster:11; do
    for s in 1 2 3; do
        "$gen" --classes "$spec" --seed "$s" --out-dir "$work/ok" >/dev/null 2>"$work/err" || {
            echo "FAILED: $spec seed $s: $(cat "$work/err")" >&2
            fail=1
        }
        n=$((n + 1))
    done
done
if [ -d "$work/ok" ] && [ -x "$ver" ]; then
    dir=${NWN_HOME:-"/mnt/c/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights"}
    if [ -d "$dir" ] && ! "$ver" "$dir" "$work"/ok/*.bic | tail -1 | grep -q ' 0 problems'; then
        echo "FAILED: bicverify on prestige builds" >&2
        fail=1
    fi
fi
[ "$fail" = 0 ] && echo "prestige ok: $n builds"
exit "$fail"
