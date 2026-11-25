#!/usr/bin/env bash
# Generates experimental variants of ../prod_cons.c into ./variants/ together
# with unified diffs against the final implementation. The final source is
# never modified.
set -e
cd "$(dirname "$0")"; mkdir -p variants
S=../prod_cons.c
mut() { # name  sed-expression
  sed -e "$2" $S > variants/$1.c
  if cmp -s $S variants/$1.c; then echo "variant $1: sed did not change anything" >&2; exit 1; fi
  diff -u $S variants/$1.c > variants/$1.patch || true
}
# E1: remove mutual exclusion around the chunk-reservation counter
mut e1_nolock '/pthread_mutex_lock(&sh->claim_lock);/d; /pthread_mutex_unlock(&sh->claim_lock);/d'
# E2: consumer waits with "if" instead of "while"
mut e2_if_wait 's/while (sh->count == 0 \&\& sh->prod_done < sh->nprod \&\& !sh->failed)/if (sh->count == 0 \&\& sh->prod_done < sh->nprod \&\& !sh->failed)/'
# E3: producer frees buffer after handing it off (ownership violation)
mut e3_free_after_handoff 's|^\t\tp->produced++;|\t\tfree(buf);   /* BUG: buffer already owned by consumer */\n\t\tp->produced++;|'
# E4: first producer finishing is treated as global completion
mut e4_first_eof 's/sh->prod_done < sh->nprod \&\& !sh->failed/sh->prod_done < 1 \&\& !sh->failed/'
# E5: final wake-up uses signal instead of broadcast
mut e5_signal_not_broadcast 's/if (sh->prod_done == sh->nprod) pthread_cond_broadcast(&sh->not_empty);/if (sh->prod_done == sh->nprod) pthread_cond_signal(\&sh->not_empty);/'
# E6: naive boundary handling -- every chunk end terminates words (no stitching of words)
mut e6_no_word_stitch 's/uint64_t w = carry_w + b->w_pre;/uint64_t w = b->w_pre;/'
for f in variants/*.c; do
  n=${f%.c}
  cc -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -pthread -fsanitize=address,undefined $f -o ${n}_asan 2>/dev/null
  cc -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -pthread -fsanitize=thread $f -o ${n}_tsan 2>/dev/null
  cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -pthread $f -o ${n} 2>/dev/null
done
ls variants/*.patch
