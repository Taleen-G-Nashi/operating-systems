/*
 * prod_cons.c -- POSIX-thread producer/consumer text statistics.
 *
 *   ./prod_cons <input-file> <num-producers> <num-consumers>
 *
 * Design summary (details in REPORT.md, section 2):
 *   - The original file is logically split into fixed-size chunks (default
 *     64 KiB). No derived files are created: producers read the original
 *     file with pread(), so there is no preparation phase and no helper
 *     process/thread.
 *   - Producers reserve chunk indices from a mutex-protected counter, read the
 *     chunk into a malloc'd buffer, and push {index, buffer, length} into a
 *     bounded ring buffer (mutex + 2 condition variables).
 *   - Consumers pop items, OWN the buffer, scan it, add the chunk-internal
 *     results to a private per-consumer stats object (no locking), and record a
 *     small fixed-size "boundary record" for the chunk in bounds[index].
 *   - After all threads are joined, main merges per-consumer stats and then
 *     walks the boundary records IN CHUNK ORDER to stitch together words and
 *     lines that cross chunk boundaries (incl. CR | LF split across chunks).
 *
 * Optional environment variables (for tests/experiments only):
 *   PC_CHUNK   chunk size in bytes   (default 65536)
 *   PC_QCAP    queue capacity, items (default 16)
 *   PC_TRACE   if set to 1: trace every transfer to stderr
 *   PC_TIMING  if set to 1: print elapsed time + per-thread counts to stderr
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_THREADS 256          /* documented upper limit per role */
#define DENSE_MAX   65536        /* lengths < DENSE_MAX use a dense array */
#define DEF_CHUNK   (64u * 1024u)
#define DEF_QCAP    16u

/* ------------------------------------------------------------------ */
/* Exact-length histogram: dense array for small lengths, sorted sparse */
/* array for large ones. No clamping, no overflow bucket.              */
/* ------------------------------------------------------------------ */
typedef struct { uint64_t len, cnt; } ovf_t;
typedef struct {
	uint64_t *dense; size_t dcap;
	ovf_t *ovf;      size_t on, ocap;
} hist_t;

static int hist_add(hist_t *h, uint64_t len, uint64_t n)
{
	if (len < DENSE_MAX) {
		if (len >= h->dcap) {
			size_t nc = h->dcap ? h->dcap : 64;
			while (nc <= len) nc *= 2;
			uint64_t *p = realloc(h->dense, nc * sizeof *p);
			if (!p) return -1;
			memset(p + h->dcap, 0, (nc - h->dcap) * sizeof *p);
			h->dense = p; h->dcap = nc;
		}
		if (h->dense[len] > UINT64_MAX - n) return -1;   /* overflow => fail */
		h->dense[len] += n;
		return 0;
	}
	size_t lo = 0, hi = h->on;                            /* binary search */
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (h->ovf[mid].len < len) lo = mid + 1; else hi = mid;
	}
	if (lo < h->on && h->ovf[lo].len == len) {
		if (h->ovf[lo].cnt > UINT64_MAX - n) return -1;
		h->ovf[lo].cnt += n;
		return 0;
	}
	if (h->on == h->ocap) {
		size_t nc = h->ocap ? h->ocap * 2 : 16;
		ovf_t *p = realloc(h->ovf, nc * sizeof *p);
		if (!p) return -1;
		h->ovf = p; h->ocap = nc;
	}
	memmove(&h->ovf[lo + 1], &h->ovf[lo], (h->on - lo) * sizeof(ovf_t));
	h->ovf[lo].len = len; h->ovf[lo].cnt = n; h->on++;
	return 0;
}

static int hist_merge(hist_t *dst, const hist_t *src)
{
	for (size_t i = 0; i < src->dcap; i++)
		if (src->dense[i] && hist_add(dst, i, src->dense[i])) return -1;
	for (size_t i = 0; i < src->on; i++)
		if (hist_add(dst, src->ovf[i].len, src->ovf[i].cnt)) return -1;
	return 0;
}

static uint64_t hist_total(const hist_t *h)
{
	uint64_t t = 0;
	for (size_t i = 0; i < h->dcap; i++) t += h->dense[i];
	for (size_t i = 0; i < h->on; i++) t += h->ovf[i].cnt;
	return t;
}

static void hist_free(hist_t *h)
{
	free(h->dense); free(h->ovf);
	memset(h, 0, sizeof *h);
}

typedef struct {
	uint64_t letters[26];
	hist_t words, lines;
} stats_t;

/* Per-chunk boundary record; written by exactly one consumer. */
typedef struct {
	uint64_t len;            /* chunk length in bytes                       */
	uint64_t w_pre, w_suf;   /* leading / trailing letter-run lengths       */
	uint64_t n_lf;           /* number of LF bytes inside the chunk         */
	uint64_t first_seg_len;  /* bytes before the first LF (if n_lf > 0)     */
	uint64_t last_seg_len;   /* bytes after the last LF (or len if no LF)   */
	uint8_t  all_letters;    /* whole chunk is one letter run               */
	uint8_t  first_cr;       /* byte before first LF (inside chunk) is CR   */
	uint8_t  last_cr;        /* last byte of chunk is CR (and seg nonempty) */
	uint8_t  filled;         /* consumed exactly once check                 */
} bound_t;

static signed char letter_idx[256];   /* read-only after init_tables() */

static void init_tables(void)
{
	for (int i = 0; i < 256; i++) letter_idx[i] = -1;
	for (int c = 'A'; c <= 'Z'; c++) letter_idx[c] = (signed char)(c - 'A');
	for (int c = 'a'; c <= 'z'; c++) letter_idx[c] = (signed char)(c - 'a');
}

/* Consumer-side analysis of one chunk. Returns 0 or -1 (allocation/overflow). */
static int analyze_chunk(const unsigned char *buf, size_t len,
                         stats_t *st, bound_t *b)
{
	size_t run = 0, seg_start = 0;
	int seen_sep = 0, seen_lf = 0;

	for (size_t i = 0; i < len; i++) {
		unsigned char c = buf[i];
		int li = letter_idx[c];
		if (li >= 0) {
			st->letters[li]++;
			run++;
			continue;
		}
		if (run > 0) {                 /* a letter run just ended */
			if (!seen_sep) b->w_pre = run;            /* touches left edge */
			else if (hist_add(&st->words, run, 1)) return -1;
			run = 0;
		}
		seen_sep = 1;
		if (c == '\n') {
			size_t seg = i - seg_start;
			int cr = seg > 0 && buf[i - 1] == '\r';
			if (!seen_lf) {
				b->first_seg_len = seg;
				b->first_cr = (uint8_t)cr;
				seen_lf = 1;
			} else if (hist_add(&st->lines, seg - (cr ? 1 : 0), 1)) {
				return -1;
			}
			seg_start = i + 1;
			b->n_lf++;
		}
	}
	if (run > 0) {
		if (!seen_sep) { b->all_letters = 1; b->w_pre = run; }
		else b->w_suf = run;          /* touches right edge */
	}
	b->last_seg_len = len - seg_start;
	b->last_cr = (b->last_seg_len > 0 && buf[len - 1] == '\r');
	b->len = len;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Shared state                                                        */
/* ------------------------------------------------------------------ */
typedef struct { size_t idx; unsigned char *buf; size_t len; } item_t;

typedef struct {
	/* immutable after setup */
	int fd; off_t size; size_t chunk, nchunks; int nprod, trace;
	bound_t *bounds;                       /* slot k written by one consumer */

	/* work reservation: protected by claim_lock */
	pthread_mutex_t claim_lock;
	size_t next_chunk;

	/* bounded queue: everything below protected by qlock */
	pthread_mutex_t qlock;
	pthread_cond_t  not_full, not_empty;
	item_t *q; size_t cap, head, count;
	int prod_done;                         /* producers that have exited loop */
	int failed;                            /* global abort flag               */
} shared_t;

typedef struct { shared_t *sh; int id; size_t produced; } prod_ctx;
typedef struct { shared_t *sh; int id; size_t consumed; stats_t st; } cons_ctx;

static void set_failed(shared_t *sh, const char *msg)
{
	pthread_mutex_lock(&sh->qlock);
	if (!sh->failed) fprintf(stderr, "prod_cons: error: %s\n", msg);
	sh->failed = 1;
	pthread_cond_broadcast(&sh->not_full);
	pthread_cond_broadcast(&sh->not_empty);
	pthread_mutex_unlock(&sh->qlock);
}

static int is_failed(shared_t *sh)
{
	pthread_mutex_lock(&sh->qlock);
	int f = sh->failed;
	pthread_mutex_unlock(&sh->qlock);
	return f;
}

static int read_full(int fd, unsigned char *buf, size_t len, off_t off)
{
	size_t got = 0;
	while (got < len) {
		ssize_t r = pread(fd, buf + got, len - got, off + (off_t)got);
		if (r < 0) { if (errno == EINTR) continue; return -1; }
		if (r == 0) { errno = EIO; return -2; }     /* file shrank */
		got += (size_t)r;
	}
	return 0;
}

static void *producer(void *arg)
{
	prod_ctx *p = arg;
	shared_t *sh = p->sh;

	for (;;) {
		if (is_failed(sh)) break;

		/* reserve a chunk index (mutual exclusion on next_chunk) */
		pthread_mutex_lock(&sh->claim_lock);
		size_t idx = (sh->next_chunk < sh->nchunks) ? sh->next_chunk++
		                                            : (size_t)-1;
		pthread_mutex_unlock(&sh->claim_lock);
		if (idx == (size_t)-1) break;              /* no work left for me */

		off_t off = (off_t)idx * (off_t)sh->chunk;
		size_t len = sh->chunk;
		if ((off_t)len > sh->size - off) len = (size_t)(sh->size - off);

		unsigned char *buf = malloc(len);
		if (!buf) { set_failed(sh, "out of memory"); break; }
		int rc = read_full(sh->fd, buf, len, off);
		if (rc) {
			free(buf);
			set_failed(sh, rc == -2 ? "input ended early (file changed?)"
			                        : "read error on input");
			break;
		}

		pthread_mutex_lock(&sh->qlock);
		while (sh->count == sh->cap && !sh->failed)
			pthread_cond_wait(&sh->not_full, &sh->qlock);
		if (sh->failed) { pthread_mutex_unlock(&sh->qlock); free(buf); break; }
		sh->q[(sh->head + sh->count) % sh->cap] = (item_t){ idx, buf, len };
		sh->count++;
		/* ownership of buf passes to the queue/consumer here */
		pthread_cond_signal(&sh->not_empty);
		pthread_mutex_unlock(&sh->qlock);

		p->produced++;
		if (sh->trace)
			fprintf(stderr, "[trace] P%d put  chunk %zu off=%lld len=%zu\n",
			        p->id, idx, (long long)off, len);
	}

	/* this producer is finished; the LAST one wakes every waiting consumer */
	pthread_mutex_lock(&sh->qlock);
	sh->prod_done++;
	if (sh->prod_done == sh->nprod) pthread_cond_broadcast(&sh->not_empty);
	pthread_mutex_unlock(&sh->qlock);
	return NULL;
}

static void *consumer(void *arg)
{
	cons_ctx *c = arg;
	shared_t *sh = c->sh;

	for (;;) {
		pthread_mutex_lock(&sh->qlock);
		if (sh->count == 0 && sh->prod_done < sh->nprod && !sh->failed)
			pthread_cond_wait(&sh->not_empty, &sh->qlock);
		if (sh->failed || sh->count == 0) {   /* aborted, or drained & done */
			pthread_mutex_unlock(&sh->qlock);
			break;
		}
		item_t it = sh->q[sh->head];
		sh->head = (sh->head + 1) % sh->cap;
		sh->count--;
		pthread_cond_signal(&sh->not_full);
		pthread_mutex_unlock(&sh->qlock);

		/* From here on this consumer exclusively owns it.buf. */
		bound_t *b = &sh->bounds[it.idx];
		if (b->filled) { set_failed(sh, "internal error: chunk consumed twice"); free(it.buf); break; }
		b->filled = 1;
		if (analyze_chunk(it.buf, it.len, &c->st, b)) {
			set_failed(sh, "allocation failure or counter overflow");
			free(it.buf);
			break;
		}
		free(it.buf);
		c->consumed++;
		if (sh->trace)
			fprintf(stderr, "[trace] C%d took chunk %zu len=%zu\n",
			        c->id, it.idx, it.len);
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
static int parse_count(const char *s, int *out)
{
	if (!*s) return -1;
	for (const char *p = s; *p; p++) if (*p < '0' || *p > '9') return -1;
	errno = 0;
	char *end;
	long v = strtol(s, &end, 10);
	if (errno || *end || v < 1 || v > MAX_THREADS) return -1;
	*out = (int)v;
	return 0;
}

static int env_size(const char *name, size_t def, size_t *out)
{
	const char *s = getenv(name);
	if (!s || !*s) { *out = def; return 0; }
	for (const char *p = s; *p; p++) if (*p < '0' || *p > '9') return -1;
	errno = 0;
	unsigned long long v = strtoull(s, NULL, 10);
	if (errno || v < 1 || v > (1ull << 40)) return -1;
	*out = (size_t)v;
	return 0;
}

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void print_hist(const char *title, const char *unit, const hist_t *h, uint64_t total)
{
	printf("== %s ==\n", title);
	printf("total %s: %" PRIu64 "\n", unit, total);
	printf("length frequency\n");
	for (size_t i = 0; i < h->dcap; i++)
		if (h->dense[i]) printf("%zu %" PRIu64 "\n", i, h->dense[i]);
	for (size_t i = 0; i < h->on; i++)
		printf("%" PRIu64 " %" PRIu64 "\n", h->ovf[i].len, h->ovf[i].cnt);
}

int main(int argc, char **argv)
{
	double t0 = now_s();
	if (argc != 4) {
		fprintf(stderr, "usage: %s <input-file> <num-producers> <num-consumers>\n"
		                "  counts are decimal integers in 1..%d\n", argv[0], MAX_THREADS);
		return 1;
	}
	int np, nc;
	if (parse_count(argv[2], &np)) {
		fprintf(stderr, "prod_cons: invalid producer count '%s' (need 1..%d)\n", argv[2], MAX_THREADS);
		return 1;
	}
	if (parse_count(argv[3], &nc)) {
		fprintf(stderr, "prod_cons: invalid consumer count '%s' (need 1..%d)\n", argv[3], MAX_THREADS);
		return 1;
	}
	size_t chunk, qcap;
	if (env_size("PC_CHUNK", DEF_CHUNK, &chunk) || env_size("PC_QCAP", DEF_QCAP, &qcap)) {
		fprintf(stderr, "prod_cons: invalid PC_CHUNK / PC_QCAP\n");
		return 1;
	}
	const char *e = getenv("PC_TRACE");  int trace  = e && *e == '1';
	e = getenv("PC_TIMING");             int timing = e && *e == '1';

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { fprintf(stderr, "prod_cons: cannot open '%s': %s\n", argv[1], strerror(errno)); return 1; }
	struct stat sb;
	if (fstat(fd, &sb) || !S_ISREG(sb.st_mode)) {
		fprintf(stderr, "prod_cons: '%s' is not a regular file\n", argv[1]);
		close(fd); return 1;
	}

	init_tables();
	shared_t sh;
	memset(&sh, 0, sizeof sh);
	sh.fd = fd; sh.size = sb.st_size; sh.chunk = chunk; sh.nprod = np; sh.trace = trace;
	sh.nchunks = (size_t)((sb.st_size + (off_t)chunk - 1) / (off_t)chunk);
	sh.cap = qcap;
	pthread_mutex_init(&sh.claim_lock, NULL);
	pthread_mutex_init(&sh.qlock, NULL);
	pthread_cond_init(&sh.not_full, NULL);
	pthread_cond_init(&sh.not_empty, NULL);

	sh.q = calloc(qcap, sizeof(item_t));
	sh.bounds = calloc(sh.nchunks ? sh.nchunks : 1, sizeof(bound_t));
	prod_ctx *pc = calloc((size_t)np, sizeof *pc);
	cons_ctx *cc = calloc((size_t)nc, sizeof *cc);
	pthread_t *pt = calloc((size_t)np, sizeof *pt);
	pthread_t *ct = calloc((size_t)nc, sizeof *ct);
	stats_t total;
	memset(&total, 0, sizeof total);
	int status = 1;
	int np_started = 0, nc_started = 0;

	if (!sh.q || !sh.bounds || !pc || !cc || !pt || !ct) {
		fprintf(stderr, "prod_cons: out of memory\n");
		goto cleanup;
	}

	for (int i = 0; i < nc; i++) {
		cc[i].sh = &sh; cc[i].id = i;
		int rc = pthread_create(&ct[i], NULL, consumer, &cc[i]);
		if (rc) { fprintf(stderr, "prod_cons: pthread_create: %s\n", strerror(rc)); set_failed(&sh, "thread creation failed"); break; }
		nc_started++;
	}
	for (int i = 0; i < np && nc_started == nc; i++) {
		pc[i].sh = &sh; pc[i].id = i;
		int rc = pthread_create(&pt[i], NULL, producer, &pc[i]);
		if (rc) { fprintf(stderr, "prod_cons: pthread_create: %s\n", strerror(rc)); set_failed(&sh, "thread creation failed"); break; }
		np_started++;
	}
	/* Join every thread that exists. Consumers may exit only when all np
	 * producers are done, so if creation failed they exit via 'failed'. */
	for (int i = 0; i < np_started; i++) pthread_join(pt[i], NULL);
	for (int i = 0; i < nc_started; i++) pthread_join(ct[i], NULL);

	if (sh.failed || np_started != np || nc_started != nc) goto cleanup;

	/* ---- all threads joined: single-threaded merge from here on ---- */
	for (int i = 0; i < nc; i++) {
		for (int k = 0; k < 26; k++) {
			if (total.letters[k] > UINT64_MAX - cc[i].st.letters[k]) { fprintf(stderr, "prod_cons: counter overflow\n"); goto cleanup; }
			total.letters[k] += cc[i].st.letters[k];
		}
		if (hist_merge(&total.words, &cc[i].st.words) || hist_merge(&total.lines, &cc[i].st.lines)) {
			fprintf(stderr, "prod_cons: out of memory or overflow while merging\n");
			goto cleanup;
		}
	}
	size_t done = 0;
	for (size_t k = 0; k < sh.nchunks; k++) done += sh.bounds[k].filled;
	if (done != sh.nchunks) {
		fprintf(stderr, "prod_cons: error: only %zu of %zu chunks were consumed\n", done, sh.nchunks);
		goto cleanup;
	}

	/* stitch words/lines across chunk boundaries, in original order */
	uint64_t carry_w = 0, open_len = 0;
	int open_cr = 0, bad = 0;
	for (size_t k = 0; k < sh.nchunks && !bad; k++) {
		const bound_t *b = &sh.bounds[k];
		if (b->len == 0) continue;
		if (b->all_letters) {
			carry_w += b->len;
		} else {
			uint64_t w = carry_w + b->w_pre;
			if (w > 0 && hist_add(&total.words, w, 1)) bad = 1;
			carry_w = b->w_suf;
		}
		if (b->n_lf == 0) {
			open_len += b->len;
			open_cr = b->last_cr;
		} else {
			uint64_t L = open_len + b->first_seg_len;
			int cr = b->first_seg_len > 0 ? b->first_cr : open_cr;
			if (L == 0) cr = 0;
			if (hist_add(&total.lines, L - (cr ? 1 : 0), 1)) bad = 1;
			open_len = b->last_seg_len;
			open_cr = b->last_cr;
		}
	}
	if (!bad && carry_w > 0 && hist_add(&total.words, carry_w, 1)) bad = 1;
	if (!bad && open_len > 0 && hist_add(&total.lines, open_len, 1)) bad = 1;   /* final line w/o LF */
	if (bad) { fprintf(stderr, "prod_cons: out of memory or overflow while merging\n"); goto cleanup; }

	/* ---- report ---- */
	printf("== Letter counts ==\n");
	for (int k = 0; k < 26; k++) printf("%c: %" PRIu64 "\n", 'A' + k, total.letters[k]);
	print_hist("Word lengths", "words", &total.words, hist_total(&total.words));
	print_hist("Line lengths", "lines", &total.lines, hist_total(&total.lines));
	if (fflush(stdout) || ferror(stdout)) { fprintf(stderr, "prod_cons: write error on stdout\n"); goto cleanup; }

	if (timing) {
		fprintf(stderr, "[timing] input=%lld bytes chunks=%zu chunk=%zu qcap=%zu P=%d C=%d elapsed=%.6f s\n",
		        (long long)sh.size, sh.nchunks, chunk, qcap, np, nc, now_s() - t0);
		for (int i = 0; i < np; i++) fprintf(stderr, "[timing] producer %d produced %zu chunks\n", i, pc[i].produced);
		for (int i = 0; i < nc; i++) fprintf(stderr, "[timing] consumer %d consumed %zu chunks\n", i, cc[i].consumed);
	}
	status = 0;

cleanup:
	/* free anything still queued (only possible after an abort) */
	if (sh.q) for (size_t i = 0; i < sh.count; i++) free(sh.q[(sh.head + i) % sh.cap].buf);
	if (cc) for (int i = 0; i < nc; i++) { hist_free(&cc[i].st.words); hist_free(&cc[i].st.lines); }
	hist_free(&total.words); hist_free(&total.lines);
	free(sh.q); free(sh.bounds); free(pc); free(cc); free(pt); free(ct);
	pthread_mutex_destroy(&sh.claim_lock);
	pthread_mutex_destroy(&sh.qlock);
	pthread_cond_destroy(&sh.not_full);
	pthread_cond_destroy(&sh.not_empty);
	close(fd);
	return status;
}