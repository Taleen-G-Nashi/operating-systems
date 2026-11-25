#!/usr/bin/env bash
# Runs every variant against the reference; writes raw logs to ../evidence/
cd "$(dirname "$0")"; E=../evidence; mkdir -p $E
python3 -c "
import random
random.seed(1)
ws=['alpha','Beta','GAMMA','x','delta1','e-f','longerwordhere']
open('$E/mid.txt','w').write(''.join(' '.join(random.choice(ws) for _ in range(10))+random.choice(['\n','\r\n']) for _ in range(20000)))
open('$E/longword.txt','w').write('a'*100+' '+'b'*40+'\n'+'x'*250+'\r\n')
open('$E/tiny.txt','w').write('abc def\n')
"
python3 ../tests/ref_stats.py $E/mid.txt > $E/mid.ref
python3 ../tests/ref_stats.py $E/longword.txt > $E/longword.ref
python3 ../tests/ref_stats.py $E/tiny.txt > $E/tiny.ref
run() { # label binary input P C chunk qcap runs
  local ok=0 bad=0 hang=0 crash=0
  for i in $(seq 1 $8); do
    PC_CHUNK=$6 PC_QCAP=$7 timeout 3 $2 $3 $4 $5 > $E/tmp.out 2> $E/tmp.err; rc=$?
    if [ $rc -eq 124 ]; then hang=$((hang+1)); cp $E/tmp.err $E/$1.lasterr
    elif [ $rc -ne 0 ]; then crash=$((crash+1)); cp $E/tmp.err $E/$1.lasterr
    elif cmp -s $E/tmp.out ${3%.txt}.ref; then ok=$((ok+1)); else bad=$((bad+1)); cp $E/tmp.out $E/$1.lastbad; fi
  done
  echo "$1: runs=$8 ok=$ok wrong_output=$bad nonzero_exit=$crash timeout=$hang   (P=$4 C=$5 chunk=$6 qcap=$7)" | tee -a $E/experiments_summary.txt
}
: > $E/experiments_summary.txt
echo "== baseline (final implementation) ==" | tee -a $E/experiments_summary.txt
run baseline_2P2C ../prod_cons $E/mid.txt 2 2 61 2 100
run baseline_1P4C_tiny ../prod_cons $E/tiny.txt 1 4 65536 16 200
echo "== E1 no lock on chunk counter ==" | tee -a $E/experiments_summary.txt
run e1_normal variants/e1_nolock $E/mid.txt 4 2 61 2 200
run e1_tsan  variants/e1_nolock_tsan $E/mid.txt 4 2 61 2 5
echo "== E2 if instead of while ==" | tee -a $E/experiments_summary.txt
run e2_if_wait variants/e2_if_wait $E/mid.txt 1 8 8 1 60
echo "== E3 free after hand-off ==" | tee -a $E/experiments_summary.txt
run e3_plain variants/e3_free_after_handoff $E/mid.txt 1 1 61 2 100
run e3_asan  variants/e3_free_after_handoff_asan $E/mid.txt 1 1 61 2 5
echo "== E4 first producer's EOF = global EOF ==" | tee -a $E/experiments_summary.txt
run e4_first_eof variants/e4_first_eof $E/mid.txt 2 2 61 2 200
echo "== E5 signal instead of broadcast ==" | tee -a $E/experiments_summary.txt
run e5_signal variants/e5_signal_not_broadcast $E/tiny.txt 1 4 65536 16 10
run e5_signal_busy variants/e5_signal_not_broadcast $E/mid.txt 2 4 61 2 20
echo "== E6 no stitching of words across chunks ==" | tee -a $E/experiments_summary.txt
run e6_chunk_big variants/e6_no_word_stitch $E/longword.txt 1 1 65536 16 5
run e6_chunk_7   variants/e6_no_word_stitch $E/longword.txt 1 1 7 16 5
rm -f $E/tmp.out $E/tmp.err

