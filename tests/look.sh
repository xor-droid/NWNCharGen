#!/bin/sh
# Appearance and voice: the options write what was asked, bad values are
# refused with a reason, the lists are not empty, choosing a look leaves the
# rest of a seeded build untouched, and random heads are real models.
#
#   tests/look.sh        needs build/bicgen, build/gffdump and the NWN1 install
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gen="$root/build/bicgen"
dump="$root/build/gffdump"
work=$(mktemp -d "${TMPDIR:-/tmp}/bicgen-look.XXXXXX")
trap 'rm -rf "$work"' EXIT
fail=0

field() { "$dump" "$1" | grep -E "^$2 " | sed 's/.*\] //; s/"//g'; }

# 1. every option lands in the file
"$gen" --class fighter --race elf --gender f --seed 3 --head 9 --portrait el_f_03_ \
    --voice 242 --hair 5 --skin 7 --tattoo1 10 --tattoo2 11 --out-dir "$work/a" >/dev/null
f=$(ls "$work"/a/*.bic)
for pair in Appearance_Head=9 Portrait=po_el_f_03_ SoundSetFile=242 Color_Hair=5 \
            Color_Skin=7 Color_Tattoo1=10 Color_Tattoo2=11; do
    k=${pair%%=*}; v=${pair#*=}
    [ "$(field "$f" "$k")" = "$v" ] || { echo "FAILED: $k is '$(field "$f" "$k")', want $v" >&2; fail=1; }
done

# 2. a voice by name, and the spec file
"$gen" --class fighter --race elf --gender f --seed 3 --voice "Cold Killer" --out-dir "$work/b" >/dev/null
[ "$(field "$work"/b/*.bic SoundSetFile)" = 243 ] || { echo "FAILED: voice by name" >&2; fail=1; }
cat >"$work/look.ini" <<'INI'
[character]
class  = fighter
race   = elf
gender = f
[appearance]
head   = 4
voice  = 220
hair   = 2
INI
"$gen" --spec "$work/look.ini" --seed 3 --out-dir "$work/c" >/dev/null
f=$(ls "$work"/c/*.bic)
[ "$(field "$f" Appearance_Head)" = 4 ] && [ "$(field "$f" SoundSetFile)" = 220 ] &&
    [ "$(field "$f" Color_Hair)" = 2 ] || { echo "FAILED: [appearance] in a spec" >&2; fail=1; }

# 3. refused with a reason
while IFS='|' read -r phrase args; do
    # shellcheck disable=SC2086
    msg=$("$gen" --class fighter --race elf --gender f $args --out-dir "$work/x" 2>&1 >/dev/null || true)
    case $msg in
    *"$phrase"*) ;;
    *) echo "FAILED: expected '$phrase' for: $args (got: $msg)" >&2; fail=1 ;;
    esac
done <<'LIST'
no head 999|--head 999
no portrait 'nope'|--portrait nope
no voice 'nope'|--voice nope
colour numbers run|--hair 200
colour numbers run|--tattoo2 -1
LIST
[ -z "$(ls "$work"/x/*.bic 2>/dev/null)" ] || { echo "FAILED: a refused look wrote a file" >&2; fail=1; }

# 4. lists
[ -n "$("$gen" --list-heads --race human --gender m)" ] || { echo "FAILED: --list-heads empty" >&2; fail=1; }
"$gen" --list-portraits --race human --gender f | grep -q hu_f || { echo "FAILED: --list-portraits" >&2; fail=1; }
"$gen" --list-voices --race human --gender m | grep -q Fighter || { echo "FAILED: --list-voices" >&2; fail=1; }
"$gen" --list-voices --race human >/dev/null 2>&1 && { echo "FAILED: --list-voices without --gender" >&2; fail=1; }

# 5. choosing a look changes only the look
"$gen" --class wizard --level 5 --seed 77 --race human --gender m --out-dir "$work/p" >/dev/null
"$gen" --class wizard --level 5 --seed 77 --race human --gender m --head 20 --hair 3 --voice 1 \
    --out-dir "$work/q" >/dev/null 2>&1 || "$gen" --class wizard --level 5 --seed 77 --race human \
    --gender m --head 20 --hair 3 --out-dir "$work/q" >/dev/null
"$dump" "$work"/p/*.bic >"$work/p.txt"
"$dump" "$work"/q/*.bic >"$work/q.txt"
others=$(diff "$work/p.txt" "$work/q.txt" | grep -E '^[<>]' | grep -vE 'Appearance_Head|Color_Hair|SoundSetFile' || true)
[ -z "$others" ] || { echo "FAILED: a look choice changed something else: $others" >&2; fail=1; }

# 6. random heads are real models and vary
heads=""
for s in 1 2 3 4 5 6 7 8 9 10 11 12; do
    "$gen" --class fighter --race human --gender m --seed "$s" --out-dir "$work/h" >/dev/null
done
valid=" $("$gen" --list-heads --race human --gender m | tr '\n' ' ') "
for f in "$work"/h/*.bic; do
    h=$(field "$f" Appearance_Head)
    case $valid in *" $h "*) ;; *) echo "FAILED: random head $h is not a listed model" >&2; fail=1 ;; esac
    heads="$heads $h"
done
[ "$(echo $heads | tr ' ' '\n' | sort -u | wc -l)" -gt 3 ] || { echo "FAILED: heads do not vary:$heads" >&2; fail=1; }

[ "$fail" = 0 ] && echo "look ok"
exit "$fail"
