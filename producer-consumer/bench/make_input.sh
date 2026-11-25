#!/usr/bin/env bash
# Option A (recommended, matches the assignment): Linux 6.12 tarball
#   curl -fL -o linux-6.12.tar.xz https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz
#   xz -dk linux-6.12.tar.xz         # -> linux-6.12.tar  (use THIS as input)
# Option B (offline fallback): deterministic synthetic text of a given size in MB.
#   bash bench/make_input.sh synthetic.txt 512
out=${1:-synthetic.txt}; mb=${2:-256}
python3 - "$out" "$mb" <<'PY'
import sys, random
out, mb = sys.argv[1], int(sys.argv[2])
random.seed(12345)
words = ["static","int","return","struct","void","Linux","kernel","if","else","while","for","0x1F","(",")","{","}",";","=","*","->","printk","NULL","unsigned","long","#define","//","buffer","thread","mutex","lock"]
block = []
for _ in range(20000):
    n = random.randint(0, 14)
    block.append(" ".join(random.choice(words) for _ in range(n)) + random.choice(["\n","\n","\n","\r\n"]))
block = "".join(block).encode()
with open(out, "wb") as f:
    written = 0
    while written < mb * 1024 * 1024:
        f.write(block); written += len(block)
PY
ls -l "$out"
