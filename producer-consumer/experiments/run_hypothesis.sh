#!/usr/bin/env bash
# Reproduces the single-run logs of E7, E8 and E9 (evidence/e7_*, e8_*, e9_*).
# Timing lines come from PC_TIMING=1 (stderr -> .log, statistics -> .out).
cd "$(dirname "$0")/.."
E=evidence; IN=$E/mid.txt
# E7: chunk size (2P/2C, qcap 16)
for c in 64 1024 65536; do
  PC_CHUNK=$c PC_QCAP=16 PC_TIMING=1 ./prod_cons $IN 2 2 > $E/e7_chunk$c.out 2> $E/e7_chunk$c.log
done
# E8: queue capacity (2P/2C, chunk 1024)
for q in 1 16 128; do
  PC_CHUNK=1024 PC_QCAP=$q PC_TIMING=1 ./prod_cons $IN 2 2 > $E/e8_q$q.out 2> $E/e8_q$q.log
done
# E9: allocation with 4 workers (chunk 1024, qcap 16)
for cfg in "1 3 1p3c" "2 2 2p2c" "3 1 3p1c"; do
  set -- $cfg
  PC_CHUNK=1024 PC_QCAP=16 PC_TIMING=1 ./prod_cons $IN $1 $2 > $E/e9_$3.out 2> $E/e9_$3.log
done
echo done
