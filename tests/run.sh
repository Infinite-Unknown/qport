#!/bin/sh
# Fixture tests for qport. Usage: sh tests/run.sh ./qport
set -u
Q=${1:-./qport}
T=$(dirname "$0")
F="$T/fixtures"
OUT="$T/out"
rm -rf "$OUT"
mkdir -p "$OUT"
export NO_COLOR=1 QPORT_NO_PROGRESS=1
unset QNX_TARGET

pass=0
fail=0

ok()  { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL  %s\n' "$1"; }

# has FILE PATTERN NAME / hasnt FILE PATTERN NAME
has()   { if grep -E -q -- "$2" "$1"; then ok "$3"; else bad "$3"; fi; }
hasnt() { if grep -E -q -- "$2" "$1"; then bad "$3"; else ok "$3"; fi; }
count() { n=$(grep -E -c -- "$2" "$1"); if [ "$n" -eq "$3" ]; then ok "$4"; else bad "$4 (got $n, want $3)"; fi; }

echo "guards: #if branches a QNX build skips"
$Q scan --brief --show-guarded "$F/guards" > "$OUT/guards.txt"
# guarded findings are demoted to notes and tagged
has   "$OUT/guards.txt" "guards.c:4:.*note QNX002"                    "epoll header under #ifdef __linux__ is guarded"
has   "$OUT/guards.txt" "guards.c:11:.*note QNX003"                   "#else of #if defined(__QNX__) is guarded"
has   "$OUT/guards.txt" "guards.c:15:.*note QNX021"                   "#ifndef __QNX__ is guarded"
has   "$OUT/guards.txt" "guards.c:19:.*note QNX023"                   "#if 0 is guarded"
has   "$OUT/guards.txt" "guards.c:23:.*error QNX025"                  "__linux__ || __QNX__ is compiled on QNX"
has   "$OUT/guards.txt" "guards.c:29:.*note QNX024"                   "#elif defined(__linux__) is guarded"
has   "$OUT/guards.txt" "guards.c:31:.*warning QNX028"                "#else after non-QNX branches is compiled"
has   "$OUT/guards.txt" "guards.c:35:.*note QNX032"                   "__GLIBC__ >= 2 is false on QNX"
has   "$OUT/guards.txt" "guards.c:39:.*error QNX022"                  "unknown macro is treated as compiled"
$Q scan --brief "$F/guards" > "$OUT/guards-default.txt"
count "$OUT/guards-default.txt" "QNX0" 3                              "default output hides guarded findings"

echo "false positives: comments, strings, members, shims"
$Q scan --brief "$F/falsepos" > "$OUT/falsepos.txt"
count "$OUT/falsepos.txt" "QNX0" 0                                    "nothing reported in falsepos.cpp"

echo "stack: large local arrays and VLAs"
$Q scan --brief "$F/stack" > "$OUT/stack.txt"
has   "$OUT/stack.txt" "stack.c:18:.*QNX083 frames \(1.0 MiB\)"       "macro-sized 2D array (1 MiB) flagged"
has   "$OUT/stack.txt" "stack.c:19:.*QNX083 ring \(128 KiB\)"         "struct array sized from struct layout (128 KiB)"
has   "$OUT/stack.txt" "stack.c:20:.*QNX083 grid \(78 KiB\)"          "double grid[100][100] flagged"
has   "$OUT/stack.txt" "stack.c:21:.*QNX084 vla"                      "VLA noted"
hasnt "$OUT/stack.txt" "small|kept|file_scope|ok "                    "small, static and file-scope arrays not flagged"
$Q scan --brief --stack-threshold 4096 "$F/stack" > "$OUT/stack-4k.txt"
has   "$OUT/stack-4k.txt" "QNX083 ok \(4 KiB\)"                       "--stack-threshold lowers the limit"

echo "sysroot: header oracle against a QNX target directory"
$Q scan --brief --sysroot "$F/sysroot/qnx" "$F/sysroot/proj" > "$OUT/sysroot.txt"
count "$OUT/sysroot.txt" "QNX090" 1                                   "exactly one missing header"
has   "$OUT/sysroot.txt" "QNX090 openssl/ssl.h"                       "openssl/ssl.h reported missing"
QNX_TARGET="$F/sysroot/qnx" $Q scan --brief "$F/sysroot/proj" > "$OUT/sysroot-env.txt"
has   "$OUT/sysroot-env.txt" "QNX090 openssl/ssl.h"                  "\$QNX_TARGET is used by default"
QNX_TARGET="$F/sysroot/qnx" $Q scan --brief --no-sysroot "$F/sysroot/proj" > "$OUT/sysroot-off.txt"
count "$OUT/sysroot-off.txt" "QNX090" 0                               "--no-sysroot disables it"
$Q scan --sysroot /nonexistent "$F/sysroot/proj" > /dev/null 2>&1
[ $? -eq 2 ] && ok "bad --sysroot exits 2" || bad "bad --sysroot exits 2"

echo "suppression comments"
$Q scan --brief "$F/suppress" > "$OUT/suppress.txt"
count "$OUT/suppress.txt" "QNX0" 1                                    "only the wrongly-suppressed line remains"
has   "$OUT/suppress.txt" "suppress.c:3:.*QNX021"                     "qport:ignore with another id does not hide"

echo "build files: link libraries"
$Q scan --brief "$F/linked" > "$OUT/linked.txt"
hasnt "$OUT/linked.txt" "QNX080"                                      "socket linked under if(QNX): no QNX080"

echo "example project"
$Q scan --brief "examples/linux-telemetryd" > "$OUT/example.txt"
[ $? -eq 1 ] && ok "errors give exit status 1" || bad "errors give exit status 1"
has   "$OUT/example.txt" "QNX080 -lsocket"                            "missing -lsocket found"
has   "$OUT/example.txt" "QNX081 -lregex"                             "missing -lregex found"
has   "$OUT/example.txt" "QNX082 -lfsnotify"                          "missing -lfsnotify found"
has   "$OUT/example.txt" "QNX083 batch \(400 KiB\)"                   "400 KiB stack batch found"
has   "$OUT/example.txt" "Makefile:2:.*QNX073"                        "uname host detection found"
has   "$OUT/example.txt" "CMakeLists.txt:12:.*QNX072"                 "Linux-only CMake branch found"
hasnt "$OUT/example.txt" "linux/watchdog.h"                           "guarded linux/watchdog.h hidden by default"
$Q scan --fail-on never "examples/linux-telemetryd" > /dev/null
[ $? -eq 0 ] && ok "--fail-on never exits 0" || bad "--fail-on never exits 0"

echo "machine-readable output"
$Q scan -f json "examples/linux-telemetryd" > "$OUT/example.json"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["summary"]["error"]>0 and d["qnx_link_libs"]==["socket","regex","fsnotify"]' "$OUT/example.json" \
    && ok "JSON parses and carries summary + link libs" || bad "JSON parses and carries summary + link libs"
$Q scan -f sarif "examples/linux-telemetryd" > "$OUT/example.sarif"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); r=d["runs"][0]; assert d["version"]=="2.1.0" and r["results"] and all(x["locations"][0]["physicalLocation"]["region"]["startLine"]>0 for x in r["results"])' "$OUT/example.sarif" \
    && ok "SARIF 2.1.0 parses with locations" || bad "SARIF 2.1.0 parses with locations"
$Q scan -f md "examples/linux-telemetryd" > "$OUT/example.md"
has   "$OUT/example.md" "^\| error \| QNX020"                         "markdown table rows"

$Q scan -f html -o "$OUT/example.html" "examples/linux-telemetryd"
python3 - "$OUT/example.html" <<'PY' && ok "HTML report embeds the scan as JSON" || bad "HTML report embeds the scan as JSON"
import json, re, sys
html = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r'<script id="qport-data" type="application/json">(.*?)</script>', html, re.S)
d = json.loads(m.group(1))
assert d["summary"]["error"] == 17 and len(d["file_list"]) == 7 and all("ref" in f for f in d["findings"])
assert html.count("</script>") == 2
PY

echo "toolchain generator"
mkdir -p "$OUT/tc"
$Q toolchain --arch x86_64 --out "$OUT/tc" "examples/linux-telemetryd" > /dev/null
has   "$OUT/tc/qnx-x86_64.cmake" "set\(CMAKE_SYSTEM_NAME QNX\)"       "CMake toolchain sets QNX"
has   "$OUT/tc/qnx-x86_64.cmake" "CMAKE_C_COMPILER_TARGET gcc_ntox86_64" "qcc target for x86_64"
has   "$OUT/tc/qnx-x86_64.cmake" "-lsocket -lregex -lfsnotify"       "scan's link libraries carried over"
has   "$OUT/tc/qnx-x86_64.mk" "TARGET_ARCH = -Vgcc_ntox86_64"         "make fragment target arch"
$Q toolchain --arch mips --out "$OUT/tc" "examples/linux-telemetryd" > /dev/null 2>&1
[ $? -eq 2 ] && ok "unknown --arch exits 2" || bad "unknown --arch exits 2"

echo "self-scan: qport's own source is QNX-clean"
$Q scan src > "$OUT/self.txt"
[ $? -eq 0 ] && ok "qport src exits 0" || bad "qport src exits 0"
has   "$OUT/self.txt" "0 errors, 0 warnings, 0 notes"                 "no findings in src/"

echo
echo "fixtures: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
