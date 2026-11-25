
#!/usr/bin/env bash
set -euo pipefail

# Reproduces E10: input-size scaling experiment.
# Five runs per configuration, with no warm-up.

cd "$(dirname "$0")/.."

LOG=${1:-evidence/e10_scaling_reproduced.log}
mkdir -p evidence
: > "$LOG"

for SIZE in 32 256; do
    if [ "$SIZE" -eq 32 ]; then
        FILE=evidence/benchmark32.txt
    else
        FILE=evidence/benchmark.txt
    fi

    if [ ! -f "$FILE" ]; then
        bash bench/make_input.sh "$FILE" "$SIZE"
    fi

    for CONFIG in "1 1" "1 3"; do
        read -r P C <<< "$CONFIG"

        for RUN in 1 2 3 4 5; do
            elapsed=$(
                { /usr/bin/time -f '%e' \
                    env PC_CHUNK=65536 PC_QCAP=16 \
                    ./prod_cons "$FILE" "$P" "$C" > /dev/null; } 2>&1
            )

            echo "size=${SIZE}MB P=$P C=$C run=$RUN elapsed=$elapsed" >> "$LOG"
        done
    done
done

echo "Results saved to $LOG"
