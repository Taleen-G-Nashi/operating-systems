#!/usr/bin/env bash
# Usage: bash bench/sweep.sh <input> chunk|qcap   -- one-factor sweeps (2P/2C), 5 runs each, median
IN=$1; WHAT=$2; BIN=${3:-./prod_cons}
if [ "$WHAT" = chunk ]; then VALS="4096 16384 65536 262144 1048576"; else VALS="1 2 4 16 64 256"; fi
for v in $VALS; do
  [ "$WHAT" = chunk ] && export PC_CHUNK=$v || export PC_QCAP=$v
  "$BIN" "$IN" 2 2 >/dev/null
  ts=""
  for r in 1 2 3 4 5; do s=$(date +%s.%N); "$BIN" "$IN" 2 2 >/dev/null; e=$(date +%s.%N); ts="$ts $(printf %.4f $(echo "$e - $s" | bc -l))"; done
  med=$(echo $ts | tr ' ' '\n' | sort -n | sed -n 3p)
  echo "$WHAT=$v runs:$ts median=$med"
done
