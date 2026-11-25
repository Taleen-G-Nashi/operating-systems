
#!/usr/bin/env bash
set -euo pipefail

INPUT=${1:?Usage: bash bench/bench.sh <input-file>}
mkdir -p evidence

CHUNK=65536
QCAP=16
RESULTS=evidence/performance.csv

echo "producers,consumers,run,seconds" > "$RESULTS"

for config in "1 1" "1 3" "2 2" "3 1"; do
    read -r P C <<< "$config"

    echo "Testing ${P}P/${C}C"

    PC_CHUNK=$CHUNK PC_QCAP=$QCAP \
        ./prod_cons "$INPUT" "$P" "$C" > /dev/null

    for run in 1 2 3 4 5; do
        elapsed=$(
            { /usr/bin/time -f '%e' \
                env PC_CHUNK=$CHUNK PC_QCAP=$QCAP \
                ./prod_cons "$INPUT" "$P" "$C" > /dev/null; } 2>&1
        )

        echo "$P,$C,$run,$elapsed" >> "$RESULTS"
        echo "  Run $run: $elapsed seconds"
    done
done

echo "Measurements saved in $RESULTS"
