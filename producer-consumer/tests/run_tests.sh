#!/usr/bin/env bash
# Usage: bash tests/run_tests.sh ./prod_cons   (default binary: ./prod_cons)
# Compares program output with the independent reference (tests/ref_stats.py)
# for every required case, several P/C configs, and several chunk sizes
# (tiny chunks force many transfers and many boundary crossings).
BIN=${1:-./prod_cons}
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
pass=0; fail=0

mk() { printf "$2" > "$T/$1"; }
mk empty.txt ''
mk nonletters.txt '12!'
mk worked.txt 'A a b!? Zz\n'
mk nonl.txt 'Ab'
mk lfcrlf.txt '\n\r\n'
mk lonecr.txt 'A\rB'
mk wordsplit.txt 'ABC'
mk crlfsplit.txt 'A\r\n'
mk nul.txt 'A\000a\377Z'
mk trailing.txt 'abc\r'
mk crcrlf.txt 'x\r\r\ny\r\n\r\n'
python3 -c "import sys;sys.stdout.write('a'*100+' '+'b'*40+'\n'+'x'*250+'\r\n'+'ok\n')" > "$T/long.txt"
python3 -c "import sys;sys.stdout.write('q'*70000+'\n'+'z'*70001)" > "$T/huge_word.txt"   # > DENSE_MAX
head -c 200000 /dev/urandom > "$T/random.bin"
python3 - > "$T/prose.txt" <<'PY'
import random
random.seed(7)
ws=["alpha","Beta","GAMMA","x","delta1","e-f","longerwordhere","Z"]
for i in range(3000):
    print(" ".join(random.choice(ws) for _ in range(random.randint(0,12))), end=random.choice(["\n","\r\n","\n","\r"]))
PY

check() {  # file P C chunk qcap
    local f=$1 P=$2 C=$3 ch=$4 q=$5
    PC_CHUNK=$ch PC_QCAP=$q timeout 30 "$BIN" "$T/$f" $P $C > "$T/out" 2> "$T/err"; local rc=$?
    python3 "$HERE/ref_stats.py" "$T/$f" > "$T/ref"
    if [ $rc -eq 0 ] && cmp -s "$T/out" "$T/ref"; then pass=$((pass+1))
    else fail=$((fail+1)); echo "FAIL: $f P=$P C=$C chunk=$ch qcap=$q rc=$rc"; head -3 "$T/err"; fi
}

FILES="empty.txt nonletters.txt worked.txt nonl.txt lfcrlf.txt lonecr.txt wordsplit.txt crlfsplit.txt nul.txt trailing.txt crcrlf.txt long.txt huge_word.txt random.bin prose.txt"
# CONFIGS="1 1" restricts the run to one P/C pair (used for the stage-by-stage runs);
# several pairs are separated by ';'  e.g. CONFIGS="1 2;2 1;2 2"
IFS=';' read -ra CFGS <<< "${CONFIGS:-1 1;1 2;2 1;2 2;1 3;3 1;3 3;3 2;8 8}"
for f in $FILES; do
  for cfg in "${CFGS[@]}"; do
    for ch in 65536 1 2 3 7 64 1000; do
      check $f $cfg $ch 16
    done
    check $f $cfg 3 1      # queue capacity 1: maximal blocking
  done
done
# repeated runs to exercise scheduling variation
if [ -z "$CONFIGS" ]; then
for i in $(seq 1 100); do check prose.txt 2 2 61 2; check long.txt 3 3 1 1; done
fi

# error handling: each must print to stderr and exit nonzero, stdout empty
err() { out=$("$BIN" "$@" 2>"$T/e"); rc=$?
        if [ $rc -ne 0 ] && [ -s "$T/e" ] && [ -z "$out" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL (error case): $*  rc=$rc"; fi; }
err; err "$T/worked.txt"; err "$T/worked.txt" 1; err "$T/missing.txt" 1 1
err "$T/worked.txt" 0 1; err "$T/worked.txt" 1 0; err "$T/worked.txt" -1 1
err "$T/worked.txt" abc 1; err "$T/worked.txt" 1 2x; err "$T/worked.txt" 1 99999
err "$T/worked.txt" 1 1 extra; err /tmp 1 1
PC_CHUNK=abc "$BIN" "$T/worked.txt" 1 1 >/dev/null 2>&1 && { fail=$((fail+1)); echo "FAIL bad PC_CHUNK"; } || pass=$((pass+1))

echo "passed=$pass failed=$fail"
[ $fail -eq 0 ]
