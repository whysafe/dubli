#!/usr/bin/env bash
# End-to-end tests: build a fixture tree, run the binary, check output and the file system.
set -u

BIN=$(cd "$(dirname "${1:-./dubli}")" && pwd)/$(basename "${1:-./dubli}")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

passed=0
failed=0

ok()   { passed=$((passed + 1)); }
fail() { failed=$((failed + 1)); echo "FAIL: $*" >&2; }

expect_contains() { # name, haystack, needle
    if grep -qF -- "$3" <<<"$2"; then ok; else fail "$1: нет строки «$3»"; echo "$2" | sed 's/^/    /' >&2; fi
}
expect_missing() {
    if grep -qF -- "$3" <<<"$2"; then fail "$1: лишняя строка «$3»"; else ok; fi
}
expect_eq() {
    if [ "$2" = "$3" ]; then ok; else fail "$1: ожидалось «$3», получено «$2»"; fi
}
inode() { stat -c %i "$1" 2>/dev/null || stat -f %i "$1"; }

make_tree() {
    local r=$1
    mkdir -p "$r/a" "$r/b" "$r/c" "$r/.hidden" "$r/node_modules"
    printf 'hello\n' > "$r/a/hello.txt"
    ln "$r/a/hello.txt" "$r/a/z-link.txt"
    printf 'hello\n' > "$r/b/hello copy.txt"
    printf 'hellp\n' > "$r/b/other.txt"
    printf 'hello\n' > "$r/.hidden/hello.txt"
    printf 'hello\n' > "$r/node_modules/x.txt"
    # Same size and the same first 4 KiB: only the full hash tells big3 apart.
    { head -c 9999 /dev/zero; printf a; } > "$r/c/big1.bin"
    { head -c 9999 /dev/zero; printf a; } > "$r/c/big2.bin"
    { head -c 9999 /dev/zero; printf b; } > "$r/c/big3.bin"
    : > "$r/c/empty1"
    : > "$r/c/empty2"
    touch -t 202001010000 "$r/a/hello.txt" "$r/node_modules/x.txt" "$r/c/big1.bin" "$r/c/big2.bin"
}

T="$WORK/t"
make_tree "$T"

# --- report ---------------------------------------------------------------
out=$("$BIN" "$T")
expect_eq "код выхода" "$?" "0"
expect_contains "группа больших" "$out" "9.8 КиБ × 2, лишнего 9.8 КиБ"
expect_contains "оставлен big1" "$out" "  = $T/c/big1.bin"
expect_contains "дубликат big2" "$out" "  - $T/c/big2.bin"
expect_missing "big3 не дубликат" "$out" "big3.bin"
expect_contains "группа hello" "$out" "6 Б × 3, лишнего 12 Б"
expect_contains "оставлен первый по алфавиту" "$out" "  = $T/a/hello.txt"
expect_contains "пробел в имени" "$out" "  - $T/b/hello copy.txt"
expect_missing "жёсткая ссылка не дубль" "$out" "z-link.txt"
expect_missing "скрытые пропущены" "$out" ".hidden"
expect_contains "ссылки посчитаны" "$out" "Жёстких ссылок пропущено: 1"
expect_contains "воронка" "$out" "Отбор: по размеру 7 → по началу 6 → по содержимому 5"
expect_contains "итог" "$out" "Групп дубликатов: 2, лишних файлов: 3, можно освободить: 9.8 КиБ"
expect_contains "просмотрено" "$out" "Просмотрено: 10 файлов в 5 папках"

first=$(head -n 1 <<<"$out")
expect_eq "крупнейшая группа первой" "$first" "9.8 КиБ × 2, лишнего 9.8 КиБ"

out=$("$BIN" -s "$T")
expect_missing "summary без групп" "$out" "  = "
expect_contains "summary с итогом" "$out" "Групп дубликатов: 2"

# --- filters --------------------------------------------------------------
out=$("$BIN" -x node_modules "$T")
expect_contains "exclude" "$out" "6 Б × 2"
expect_missing "exclude убрал папку" "$out" "node_modules"

out=$("$BIN" -x '*.bin' -x 'node_modules' "$T")
expect_contains "несколько exclude" "$out" "Групп дубликатов: 1"

out=$("$BIN" -a "$T")
expect_contains "hidden" "$out" "6 Б × 4"

out=$("$BIN" -e "$T")
expect_contains "пустые" "$out" "0 Б × 2, лишнего 0 Б"
expect_contains "пустые: три группы" "$out" "Групп дубликатов: 3"

out=$("$BIN" -m 1K "$T")
expect_contains "min-size" "$out" "Групп дубликатов: 1"
expect_missing "min-size отсёк мелкие" "$out" "hello"

# --- keep rules -----------------------------------------------------------
out=$("$BIN" -k newest "$T/a" "$T/b" "$T/node_modules")
expect_contains "keep newest" "$out" "  = $T/b/hello copy.txt"

out=$("$BIN" "$T/node_modules" "$T/a")
expect_contains "keep first по аргументам" "$out" "  = $T/node_modules/x.txt"

out=$("$BIN" -k shortest "$T/node_modules/x.txt" "$T/b/hello copy.txt")
expect_contains "keep shortest" "$out" "  = $T/b/hello copy.txt"

# --- machine-readable formats --------------------------------------------
json=$("$BIN" -f json "$T")
groups=$(python3 -c 'import json,sys; d=json.load(sys.stdin); print(len(d["groups"]), d["total"]["duplicates"], d["total"]["wasted"], d["scanned"]["hardlinks"])' <<<"$json")
expect_eq "json" "$groups" "2 3 10012 1"
keep=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["groups"][1]["duplicates"][0])' <<<"$json")
expect_eq "json пути" "$keep" "$T/b/hello copy.txt"

csv=$("$BIN" -f csv "$T")
expect_eq "csv заголовок" "$(head -n 1 <<<"$csv")" "group,size,role,path"
expect_contains "csv строка" "$csv" "2,6,duplicate,\"$T/b/hello copy.txt\""
expect_eq "csv строк" "$(wc -l <<<"$csv" | tr -d ' ')" "6"

# --- inode dedup and errors ----------------------------------------------
out=$("$BIN" "$T/a/hello.txt" "$T/a/hello.txt" "$T/a/z-link.txt")
expect_contains "один inode" "$out" "Дубликатов не найдено."

"$BIN" "$WORK/missing" >/dev/null 2>&1
expect_eq "нет пути" "$?" "1"
"$BIN" >/dev/null 2>&1
expect_eq "нет аргументов" "$?" "2"
"$BIN" -d -l "$T" >/dev/null 2>&1
expect_eq "delete+link" "$?" "2"
"$BIN" -m 10X "$T" >/dev/null 2>&1
expect_eq "плохой размер" "$?" "2"
"$BIN" -n "$T" >/dev/null 2>&1
expect_eq "dry-run без действия" "$?" "2"
"$BIN" -f xml "$T" >/dev/null 2>&1
expect_eq "плохой формат" "$?" "2"

# --- dry run changes nothing ---------------------------------------------
out=$("$BIN" -d -n "$T")
expect_contains "проба" "$out" "[проба] удалить $T/c/big2.bin"
expect_contains "проба итог" "$out" "[проба] Удалено: 3, освобождено: 9.8 КиБ"
[ -f "$T/c/big2.bin" ] && [ -f "$T/b/hello copy.txt" ] && ok || fail "проба удалила файлы"

# --- link -----------------------------------------------------------------
L="$WORK/l"
make_tree "$L"
out=$("$BIN" -l "$L")
expect_contains "link лог" "$out" "связан: $L/c/big2.bin → $L/c/big1.bin"
expect_eq "link inode" "$(inode "$L/b/hello copy.txt")" "$(inode "$L/a/hello.txt")"
expect_eq "link содержимое" "$(cat "$L/b/hello copy.txt")" "hello"
expect_eq "link без временных файлов" "$(find "$L" -name '*.dubli-*' | wc -l | tr -d ' ')" "0"
out=$("$BIN" "$L")
expect_contains "после link дублей нет" "$out" "Дубликатов не найдено."

# --- delete ---------------------------------------------------------------
D="$WORK/d"
make_tree "$D"
json=$("$BIN" -d -f json "$D" 2>"$WORK/log")
python3 -c 'import json,sys; json.load(sys.stdin)' <<<"$json" && ok || fail "json испорчен логом"
expect_contains "лог в stderr" "$(cat "$WORK/log")" "удалён: $D/c/big2.bin"
[ -f "$D/c/big1.bin" ] && [ ! -e "$D/c/big2.bin" ] && [ -f "$D/c/big3.bin" ] && ok || fail "delete big"
[ -f "$D/a/hello.txt" ] && [ ! -e "$D/b/hello copy.txt" ] && [ ! -e "$D/node_modules/x.txt" ] && ok || fail "delete hello"
[ -f "$D/b/other.txt" ] && [ -f "$D/.hidden/hello.txt" ] && ok || fail "delete задел лишнее"
out=$("$BIN" "$D")
expect_contains "после delete дублей нет" "$out" "Дубликатов не найдено."

# --- trusting the hash ----------------------------------------------------
C="$WORK/c"
mkdir -p "$C"
printf 'same\n' > "$C/1"
printf 'same\n' > "$C/2"
touch -t 202001010000 "$C/1" "$C/2"
out=$("$BIN" --fast "$C")
expect_contains "fast" "$out" "Групп дубликатов: 1"

echo "Интеграционные тесты: пройдено $passed, провалено $failed"
[ "$failed" -eq 0 ]
