#!/usr/bin/env bash
# (run `bash experiments/make_variants.sh` first to build the variants)
# E5 evidence: show that after the run "hangs", the consumer threads are all
# sleeping in a futex wait (i.e. blocked on the condition variable) -- not running.
# Alternative with gdb:  gdb -p <pid> -batch -ex "thread apply all bt"
cd "$(dirname "$0")/.."
OUT=evidence/e5_thread_states.txt
PC_CHUNK=65536 experiments/variants/e5_signal_not_broadcast evidence/tiny.txt 1 4 > /dev/null 2>&1 &
pid=$!
sleep 1
{
  echo "pid=$pid  (variant e5_signal_not_broadcast, tiny.txt, 1P/4C) after 1 s"
  echo "process alive: $(kill -0 $pid 2>/dev/null && echo yes || echo no)"
  for t in /proc/$pid/task/*; do
    printf "tid=%s state=%s wchan=%s\n" "$(basename $t)" "$(grep '^State' $t/status | awk '{print $2 $3}')" "$(cat $t/wchan 2>/dev/null)"
  done
} | tee $OUT
kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null; exit 0
