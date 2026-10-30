#!/bin/sh
# Checks of the guided builder (bicgen --guided):
#   1. a session of nothing but Enters writes the same file as the same seed
#      without --guided (the guide adds no random draws of its own);
#   2. scripted sessions in tests/guided/*.ans run to completion, bicverify
#      finds the files clean, and the saved answers replay to identical bytes;
#   3. quitting (or running out of input) writes nothing.
#
#   tests/guided.sh        needs build/bicgen, build/bicverify and the NWN1 install
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gen="$root/build/bicgen"
ver="$root/build/bicverify"
install=${NWN_HOME:-"/mnt/c/Program Files (x86)/Steam/steamapps/common/Neverwinter Nights"}
work=$(mktemp -d "${TMPDIR:-/tmp}/bicgen-guided.XXXXXX")
trap 'rm -rf "$work"' EXIT

[ -x "$gen" ] || { echo "build/bicgen missing; run make" >&2; exit 2; }
fail=0

# 1. Enter-only sessions equal the random builder
n=0
while IFS= read -r args; do
    n=$((n + 1))
    mkdir "$work/a$n" "$work/b$n"
    # shellcheck disable=SC2086
    "$gen" $args --out-dir "$work/a$n" >/dev/null
    # shellcheck disable=SC2086
    yes '' | "$gen" --guided $args --out-dir "$work/b$n" >/dev/null 2>&1 || true
    if ! cmp -s "$work/a$n"/*.bic "$work/b$n"/*.bic; then
        echo "identity FAILED: $args" >&2
        fail=1
    fi
done <<'LIST'
--seed 11
--seed 12 --level 8 --class wizard --school necromancy
--seed 13 --level 14 --max-classes 3
--seed 14 --level 10 --class cleric
--seed 15 --classes fighter:5,wizard:3,rogue:2 --hp avg
--seed 16 --level 6 --class sorcerer --variety 0
LIST
echo "identity: $n Enter-only sessions compared"

# 2. scripted sessions: complete, verify, replay
for ans in "$here"/guided/*.ans; do
    name=$(basename "$ans" .ans)
    seed=$(sed -n 's/^# seed \([0-9]*\).*/\1/p' "$ans" | head -1)
    seed=${seed:-77}
    mkdir "$work/$name" "$work/$name-replay"
    "$gen" --guided --seed "$seed" --out-dir "$work/$name" <"$ans" >/dev/null
    f=$(ls "$work/$name"/*.bic)
    "$gen" --guided --seed "$seed" --out-dir "$work/$name-replay" \
        <"${f%.bic}.answers" >/dev/null
    if ! cmp -s "$f" "$work/$name-replay/$(basename "$f")"; then
        echo "replay FAILED: $name" >&2
        fail=1
    fi
    if [ -d "$install" ] && [ -x "$ver" ]; then
        if ! "$ver" "$install" "$f" | tail -1 | grep -q ' 0 problems'; then
            echo "bicverify FAILED: $name" >&2
            fail=1
        fi
    fi
    echo "session $name: ok"
done

# 3. quitting writes nothing
mkdir "$work/quit"
printf 'elf\n3\nwizard\n' | "$gen" --guided --seed 5 --out-dir "$work/quit" >/dev/null 2>&1 && {
    echo "quit FAILED: exit status 0 after running out of input" >&2
    fail=1
}
printf 'q\n' | "$gen" --guided --seed 5 --out-dir "$work/quit" >/dev/null 2>&1 && fail=1
if ls "$work/quit"/* >/dev/null 2>&1; then
    echo "quit FAILED: a file was written" >&2
    fail=1
fi
echo "quit: nothing written"

[ "$fail" -eq 0 ] && echo "guided ok"
exit "$fail"
