# HW2: Multi-threaded Text Statistics

## 1. Build and Run

### 1.1 Development Environment

This program was developed in Ubuntu 24.04 Linux environment using the vscode version supplied by the university. The implementation is written in C and uses POSIX threads for concurrent executions.

### 1.2 Compilation

The project includes a Makefile to compile the source code. From the repository's root directory, you can run

```bash
make
```

The makefile executes this command:

```bash
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -pthread prod_cons.c -o prod_cons
```

It generates an executable named prod_cons. The last time this was compiled, it did so successfully without reporting any warnings and errors.

### 1.3 Execution and Command-line Arguments

The program takes in 3 command line arguemtns:

```bash
./prod_cons <input_file> <num_producers> <num_consumers>
```

### 1.4 Limits, Error Handling, and Optional Settings

The program accepts between 1 and 256 producer threads and between 1 and 256 consumer threads. Invalid thread counts, missing arguments, and additional arguments are rejected.

The implementation checks for file access errors, invalid input files, memory allocation failures, and counter overflow. When an error occurs, the program prints an explanatory message to standard error and exits with a nonzero status. Successful executions return status 0.

The following environment variables are available for testing and experimentation:

- `PC_CHUNK`: Sets the chunk size in bytes (default: 65536).
- `PC_QCAP`: Sets the queue capacity (default: 16).
- `PC_TRACE=1`: Enables tracing of chunk transfers.
- `PC_TIMING=1`: Displays elapsed time and per-thread work counts.

The program reads directly from the original input file using `pread()`. No intermediate files or separate input preparation are required.

### 1.5 Output Format

 The program prints out 3 categories of statistics

 1. Letter Counts A-Z and a-z combining uppercase and lowercase letters
 2. Word-length distribution: The total number of words and their frequencies for each word length.
 3.Line-length ditribution: the total number if lines and their frequencies for each line length.

## 2. Final Design

 ### 2.1 Program Overview

 The input is divided into chunks that are processed by multiple threads. Producer threads read chunks from the file and place them in a shared bounded queue. Consumer threads retrieve these chunks and calculate partial statistics.

The main thread initializes the shared structures, creates the producer and consumer threads, waits for their completion, and combines the partial results into the final statistics.

The design separates file reading from text processing, allowing the numbers of producers and consumers to be changed independently.

### 2.2 Producers and Consumers Threads

The producer and consumer roles are implemented as separate thread functions, producer() and consumer().

Each producer reserves a chunk index using a shared counter protected by claim_lock. It then reads the corresponding bytes from the input file and places the chunk into the shared queue. The producer waits when the queue is full and signals a waiting consumer after adding work.

Each consumer removes a chunk from the queue and calls analyze_chunk() to calculate its statistics. The queue is unlocked before the analysis begins, allowing other threads to access the queue while the consumer processes its chunk.

The number of producer and consumer threads is determined by the command-line arguments. All threads performing the same role execute the same function, but operate on different work items.

### 2.3 Work Units and Input Partitioning

The input file is divided into fixed-size byte chunks. Each chunk is identified by an index, and its starting offset is calculated by multiplying the index by the chunk size. The last chunk may be smaller if the file size is not an exact multiple of the chunk size.

Producers reserve chunk indices using the shared next_chunk counter, protected by claim_lock. This ensures that each chunk is assigned to only one producer. The reserved bytes are read using pread(), which allows each producer to read from a specified file offset without changing a shared file position.

Chunk boundaries are not treated as word or line boundaries. The program retains information about chunk boundaries for later result merging.

### 2.4 Shared Queue and Memory Ownership

The program uses a bounded circular queue to transfer work from producers to consumers. Each queue item stores the chunk index, a pointer to its data buffer, and the number of bytes in the chunk.

The queue uses head, count, and cap to manage insertion and removal. A producer inserts a chunk at the next available position, while a consumer removes the item at the head of the queue.

Each producer allocates a buffer for a chunk using malloc(). Once the buffer is inserted into the queue, its ownership is transferred to the queue and subsequently to the consumer that retrieves it. The consumer releases the buffer using free() after completing its analysis.

This ownership rule ensures that the buffer remains valid while the consumer is processing its contents.

### 2.5 Synchronization and Waiting

The implementation uses two mutexes and two condition variables.

The claim_lock mutex protects the shared next_chunk counter, preventing multiple producers from reserving the same chunk.

The qlock mutex protects shared queue operations and the associated completion and failure state. It ensures that insertion, removal, and queue-state updates are synchronized.

The not_full condition variable allows producers to wait when the queue reaches its capacity. The not_empty condition variable allows consumers to wait when no work is currently available.

Condition waiting is performed inside while loops so that the relevant condition is checked again after a thread wakes up.

The queue mutex is released before a consumer performs text analysis. This allows other threads to access the queue while a consumer processes its assigned chunk.

### 2.6 Thread Termination

The program tracks the number of completed producers using prod_done.

When a producer has no remaining chunks to reserve, it exits its processing loop and increments prod_done while holding the queue mutex.

When the final producer finishes, it broadcasts on not_empty to wake any consumers waiting for additional work. Consumers continue processing queued chunks and terminate when the queue is empty and all producers have finished.

The shared failed flag is used to communicate errors. If a failure occurs, the program sets this flag and broadcasts to both condition variables, allowing waiting producers and consumers to exit instead of remaining blocked.

This termination mechanism ensures that the program does not interpret the completion of one producer as the completion of all producers.

### 2.7 Statistics Calculation and Result Merging

Each consumer calls analyze_chunk() to process its assigned chunks. The function examines the input byte by byte and calculates ASCII letter frequencies, word lengths, and line lengths.

Letter counts are stored in an array of 26 counters. Uppercase and lowercase ASCII letters contribute to the same counter. A word is defined as a consecutive sequence of ASCII letters, while line lengths are calculated from the bytes between LF characters, excluding the LF and any immediately preceding CR.

Word-length and line-length frequencies are stored using dynamically allocated histograms. The implementation uses a dense array for smaller lengths and a separate ordered structure for larger lengths. This allows the program to retain exact lengths without imposing the 30-byte limit present in the starting code.

Each consumer maintains its own statistics in stats_t, reducing the need for synchronization during counting. After all threads have terminated, the main thread merges the consumer statistics using hist_merge().

Chunk boundaries require additional processing because a word or line may extend across multiple chunks. Each consumer records boundary information in a bound_t structure, including leading and trailing word fragments, line segments, and carriage-return information.

The main thread processes these boundary records in the original file order. It combines word fragments and line segments that cross chunk boundaries and adds the completed lengths to the final histograms.

Before merging, the program verifies that every chunk was consumed. This helps detect missing work. The implementation also checks for allocation failures and counter overflow, reporting an error instead of producing an incorrect successful result.


### 2.8 Critical-Section Placement, Helper Threads, and Preparation

**Operations outside critical sections:**

Memory allocation using `malloc()`, file reading using `pread()`, text analysis, and buffer deallocation using `free()` are performed outside the mutex-protected critical sections.

The `claim_lock` mutex protects the reservation of chunk indices through `next_chunk`. The `qlock` mutex protects queue operations, including updates to queue indices and counts, as well as the shared completion and failure states.

**Thread organization:**

The program uses one main thread, P producer threads, and C consumer threads. No additional helper threads or processes are created.

**Input preparation:**

The program does not generate intermediate files or pre-partition the input. Producers read directly from the original file using byte offsets. Therefore, no separate preparation phase or preparation artifacts are required.

**Chunk-boundary handling:**

Word and line boundaries may extend across multiple chunks. Each chunk records information about its leading and trailing word fragments and line segments.

After the worker threads complete, the main thread processes the boundary records in their original file order. This allows words split across chunks to be reconstructed and ensures that CRLF sequences crossing chunk boundaries are handled correctly.

## 3. Correctness

 ### 3.1 Testing Methodology

 Correctness was evaluated by comparing the program's output against independently calculated reference resultss. The Python script tests/ref_stats.py was used to generate expected letter counts, word-lenght distributions, nd line-length distributions.
 
 The automated tests were exeuted using:
 
 ```bash
 make test
 ```

 The test script, tests/run_tests.sh, runs the program with different inputs and producer-consumer configurations and compares the resulting statistics with the Python reference.

 The tests are intended to check both the accuracy of the calculations and the correct coordination of threads. In particular, the implementation must process every chunk exactly once, preserve words and lines that cross chunk boundaries, and terminate after all work has been consumed.

 
### 3.2 Stage 1: Reliable Data Transfer

Stage 1 focuses on correct transfer of work between producers and consumers, initially using one producer and one consumer.

Producers reserve chunk indices, read the corresponding bytes, and insert the resulting work items into the bounded queue. Consumers remove the work items and process them. Chunk reservation and queue operations are protected by mutexes to prevent duplicate assignments and unsynchronized queue access.

The program tracks completed producers and allows consumers to terminate only after production has finished and the queue has been drained.

To verify the transfers, the following configuration was used:

```bash
PC_CHUNK=2000 PC_TRACE=1 PC_TIMING=1 ./prod_cons evidence/mid.txt 1 1
```

The input contains 1,293,854 bytes, corresponding to 647 chunks. The recorded trace contains 647 insertions and 647 removals, with each chunk index appearing once in each category. The trace is stored in `evidence/stage1_trace_1P1C.txt`.

An empty-file test was also performed using three producers and three consumers. The program terminated successfully and produced zero letter counts, zero words, and zero lines. The corresponding evidence is stored in `evidence/empty_run.out` and `evidence/empty_run.err`.

The automated test suite provides additional verification of 1P/1C operation across different input sizes and chunk boundaries.


 ### 3.3 Stage 2: Correct Text Statistics

 The second stage focuses on verifying the letter, word-length, and line-length calculations.

 A manual test was performed using the input:

 ```bash
 A a b!? Zz
 ```

 The expected results were:

 | Statistic | Expected result |
|---|---|
| A | 2 |
| B | 1 |
| Z | 2 |
| Other letters | 0 |
| Total words | 4 |
| Words of length 1 | 3 |
| Words of length 2 | 1 |
| Total lines | 1 |
| Lines of length 10 | 1 |

The program was executed using:

```bash
./prod_cons sample.txt 1 1
```

The observed results matched the expected values.

The automated tests provide additional checks using different input contents and lengths.


### 3.4 Stage 3: Parallel Work Sharing

Stage 3 evaluates whether the implementation maintains correct results when multiple producers and consumers execute concurrently.

The program was tested with different thread configurations, chunk sizes, and queue capacities. The following baseline experiments were conducted:

| Configuration | Runs | Correct | Failures |
|---|---:|---:|---:|
| 2P/2C, chunk size 61, queue capacity 2 | 100 | 100 | 0 |
| 1P/4C, chunk size 65536, queue capacity 16 | 200 | 200 | 0 |
| **Total** | **300** | **300** | **0** |

All 300 executions completed successfully and produced the expected output. No incorrect results, nonzero exits, or timeouts were recorded.

The implementation uses separate mutexes for chunk reservation and shared queue operations. Each consumer maintains its own statistics, which are merged after the threads terminate.

Additional work-sharing evidence was collected using two producers and two consumers. In the recorded execution, the producers handled 206 and 226 chunks, while the consumers processed 202 and 230 chunks, respectively. Both totals equal 432, demonstrating that the work was distributed across the threads.

The results support the correctness of the implementation under the tested configurations, although repeated executions cannot guarantee that every possible thread interleaving has been exercised.

**Evidence:**

- `evidence/experiments_summary.txt`
- `evidence/stage3_2P2C_workshare.txt`

 ### 3.5 Test Results

 The final implementation was tested using automated test suite provided in tests/run_tests.sh, with statistics generated by tests/ref_stats.py

 Everything was executed using:

 ```bash
 make test
```

The report result was:
```text
passed = 1293 failed=0
```

All 1,293 automated checks passed, with no reported failures.

Additional manual tests were executed from the Ubuntu terminal using the input `A a b!? Zz\n`, with configurations of one producer and one consumer (1P/1C), and two producers and two consumers (2P/2C).

Both configurations produced the following results:

- Letter counts: A = 2, B = 1, Z = 2, with all other letters equal to zero.
- Total words: 4, consisting of three words of length 1 and one word of length 2.
- Total lines: 1, with a line length of 10 bytes.

These results support the correctness of the implementation for the tested inputs and configurations. Additional concurrency investigations are documented in Section 5.

The automated test scripts and experimental evidence are included in the submitted project files.


### 3.6 Required Correctness Cases

Correctness was evaluated using the Python reference implementation in `tests/ref_stats.py`. The automated test suite compares the C program's statistical output with independently generated reference results.

The following cases were included to verify text counting, chunk-boundary handling, and error conditions.

| Test case | Expected result | Observed result |
|---|---|---|
| Empty file | 0 letter counts, 0 words, 0 lines; successful termination | Pass |
| Only non-letters (`12!`) | 0 words, 1 line of length 3 | Pass |
| Sample (`A a b!? Zz\n`) | A=2, B=1, Z=2; 4 words; 1 line of length 10 | Pass |
| No final newline (`Ab`) | A=1, B=1; 1 word of length 2; 1 line of length 2 | Pass |
| Empty lines (`0A 0D 0A`) | 2 empty lines, no additional trailing line | Pass |
| Standalone CR (`41 0D 42`) | 2 words of length 1; 1 line of length 3 | Pass |
| Word across chunks | A word spanning multiple chunks is counted once at its full length | Pass |
| CRLF across chunks | CRLF is treated as one line ending even when split between chunks | Pass |
| Long words and lines | Correct lengths are recorded beyond individual chunk boundaries | Pass |
| Lengths above 65535 | Exact word and line lengths are preserved | Pass |
| NUL and non-ASCII bytes | Only ASCII letters contribute to word and letter counts | Pass |
| Random binary and prose inputs | Results match the independent Python reference | Pass |
| Multiple producers and consumers | All chunks processed; final statistics match reference | Pass |
| Fewer chunks than workers | Threads terminate without missing or duplicating work | Pass |
| Invalid arguments and files | Error message, nonzero exit status, empty standard output | Pass |

The automated test suite was run using:

```bash
make test
```

The final result was:

```text
passed=1293 failed=0
```

These results support correctness across the tested inputs, chunk sizes, and producer-consumer configurations. The automated checks do not guarantee that every possible thread scheduling order has been exercised.

**Evidence:**

- `tests/run_tests.sh`
- `tests/ref_stats.py`
- `evidence/test_results.txt`
- `evidence/experiments_summary.txt`


## 4. Required Performance Comparison

### 4.1 Experimental Environment and Input

Performance measurements were conducted in Ubuntu 24.04.3 LTS, using Linux kernel 6.8.0-71-generic and GCC 13.3.0.

The program was compiled using the C11 standard, `-O2` optimization, and POSIX thread support.

A deterministic synthetic input file was generated using:

```bash
bash bench/make_input.sh evidence/benchmark.txt 256
```

The resulting file contained 269,053,538 bytes. The generation script uses a fixed random seed to create reproducible input containing words, symbols, and line endings.

The benchmark used the following fixed parameters:

- Chunk size: 65536 bytes
- Queue capacity: 16
- Input: `evidence/benchmark.txt`
- Optimization level: `-O2`
- Warm-up: one unmeasured execution per configuration
- Measurements: five executions per configuration


The experiments were conducted on a system reporting an Intel Xeon Processor (SapphireRapids), with 4 logical CPUs, 4 cores per socket, and 1 thread per core.

The system was running Ubuntu 24.04.3 LTS with Linux kernel 6.8.0-71-generic and GCC 13.3.0.


### 4.2 Measurement Procedure

The benchmark was executed using:

```bash
bash bench/bench.sh evidence/benchmark.txt
```

Four producer-consumer configurations were evaluated: 1P/1C, 1P/3C, 2P/2C, and 3P/1C.

The benchmark used `/usr/bin/time` to measure elapsed wall-clock time for each complete program invocation. The input file was prepared before the timing runs, and the same file was used for every configuration.

Each configuration was executed once as a warm-up, followed by five measured runs. The program's standard output was redirected to `/dev/null` consistently across configurations.

All individual measurements were saved in `evidence/performance.csv`. The median was used to compare the configurations.

### 4.3 Timing Results

The recorded execution times were:

| Configuration | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 | Median (s) |
|---|---:|---:|---:|---:|---:|---:|
| 1P/1C | 1.46 | 1.47 | 1.45 | 1.45 | 1.45 | 1.45 |
| 1P/3C | 0.64 | 0.56 | 0.68 | 0.84 | 0.45 | 0.64 |
| 2P/2C | 1.04 | 0.86 | 1.15 | 0.95 | 0.51 | 0.95 |
| 3P/1C | 1.45 | 0.91 | 1.44 | 0.88 | 1.45 | 1.44 |

Speedup was calculated relative to the 1P/1C baseline:

Speedup = baseline median time / configuration median time

| Configuration | Median (s) | Speedup |
|---|---:|---:|
| 1P/1C | 1.45 | 1.00 |
| 1P/3C | 0.64 | 2.27 |
| 2P/2C | 0.95 | 1.53 |
| 3P/1C | 1.44 | 1.01 |


### 4.4 Performance Analysis

**Overall results:**

The 1P/3C configuration achieved the lowest median execution time of 0.64 seconds, corresponding to a 2.27x speedup over the 1P/1C baseline (1.45 seconds). The 2P/2C configuration achieved a median of 0.95 seconds (1.53x speedup), while 3P/1C achieved 1.44 seconds (1.01x speedup).

**Likely performance bottleneck:**

The consumer threads perform the byte-by-byte text analysis, including letter counting, word detection, and histogram updates. Producers primarily read file chunks and transfer them through the shared queue.

Increasing the number of producers while retaining one consumer (3P/1C) did not substantially improve median performance. In contrast, using three consumers (1P/3C) provided the largest improvement.

This suggests that consumer-side text processing was the primary bottleneck for the tested workload.

**Why performance does not scale perfectly:**

The 1P/3C configuration achieved a 2.27x speedup rather than an ideal 3x. Possible explanations include synchronization overhead, memory bandwidth limitations, producer-side work, thread creation, and final result merging.

The measured speedup is therefore lower than the theoretical improvement expected from perfectly parallel processing.

**Variability between runs:**

The 1P/1C configuration produced relatively stable execution times between 1.45 and 1.47 seconds. However, the multithreaded configurations showed greater variation.

The 1P/3C measurements ranged from 0.45 to 0.84 seconds. The 2P/2C configuration ranged from 0.51 to 1.15 seconds, while 3P/1C ranged from 0.88 to 1.45 seconds.

These differences may be related to thread scheduling, CPU availability, or other system activity. The benchmark environment reported four logical CPUs, and variations in resource availability may have influenced performance.

The exact causes were not separately investigated. Consequently, median execution times were used to reduce the influence of individual unusually fast or slow runs.

**Comparison with Experiment E9:**

Experiment E9 found that 2P/2C was the fastest configuration on the smaller 1.3 MB input, whereas the main benchmark found 1P/3C to be fastest on the approximately 269 MB input.

E9 used only one execution per configuration, with recorded times between approximately 0.009 and 0.017 seconds. At this scale, thread startup, scheduling, and measurement variability can have a relatively large influence on the results.

The required performance comparison used five measured executions per configuration on a substantially larger input. Its median results therefore provide a more reliable basis for comparing the configurations under the tested conditions.

**Conclusion:**

The results indicate that the allocation of threads between producer and consumer roles affects performance. For the larger benchmark input, assigning more threads to consumption produced a greater improvement than assigning more threads to production.

All four benchmark configurations produced identical statistical outputs, confirming consistency of the results across the tested thread allocations.

**Evidence:**

- `evidence/performance.csv`
- `evidence/benchmark_checks/`
- `bench/bench.sh`

## 5. Development and Experiment Notebook


### 5.1 Experiment Index

This notebook documents ten investigations into synchronization, memory ownership, thread termination, input partitioning, and performance.

The deliberate-change experiments compare the correct implementation against modified versions stored in `experiments/variants/`. The hypothesis experiments investigate how different program configurations and input sizes affect correctness and execution time.

The experiments were conducted using the final implementation in `prod_cons.c` and the experimental scripts. Results and supporting evidence are stored in the `evidence/` directory.

| ID | Investigation | Primary category | Concept | Evidence |
|---|---|---|---|---|
| E1 | Removing the chunk reservation lock | Deliberate change | Mutual exclusion and shared state | `e1_normal.lasterr` |
| E2 | Replacing `while` with `if` | Deliberate change | Condition waiting | `experiments_summary.txt` |
| E3 | Freeing the buffer after handoff | Deliberate change | Memory ownership and termination | `e3_asan.lasterr` |
| E4 | Treating the first producer's EOF as completion | Deliberate change | Memory ownership and termination | `e4_first_eof.lasterr` |
| E5 | Replacing `broadcast` with `signal` | Deliberate change | Condition waiting | `experiments_summary.txt` |
| E6 | Removing word-boundary stitching | Deliberate change | Input partitioning and result merging | `e6_chunk_7.lastbad` |
| E7 | Effect of chunk size on work distribution | Hypothesis experiment | Input partitioning and result merging | `e7_chunk64.log` |
| E8 | Effect of queue capacity on execution time | Hypothesis experiment | Condition waiting and performance interpretation | `e8_q1.log` |
| E9 | Effect of producer-consumer allocation | Hypothesis experiment | Performance interpretation and work sharing | `e9_1p3c.log` |
| E10 | Effect of input size on parallel performance | Hypothesis experiment | Performance interpretation | `e10_scaling.log` |

Evidence filenames in the table refer to files in the `evidence/` directory. Source modifications for E1–E6 are stored as patches in `experiments/variants/`.

Each experiment is documented below with its question, prediction, setup, observations, explanation, and lesson learned.

### 5.2 E1 — Removing Mutual Exclusion from Chunk Reservation

**Primary category:** Deliberate changes and tests

**Concept:** Mutual exclusion and shared state

**Question and prediction:**

What happens if multiple producers reserve chunks without protecting the shared `next_chunk` counter?

The expected risk is that two producers may read the same counter value before either updates it, causing both to reserve the same chunk.

**Setup and change:**

The mutex protecting the shared chunk counter was removed in the experimental variant `e1_nolock.c`.

The configuration was:

- Producers: 4
- Consumers: 2
- Chunk size: 61 bytes
- Queue capacity: 2
- Input: `evidence/mid.txt`
- Repetitions: 200

The experiment was executed using `experiments/run_experiments.sh`.

**Observations:**

All 200 runs terminated with a nonzero exit status. The recorded error was:

```text
prod_cons: error: internal error: chunk consumed twice
```

Five additional ThreadSanitizer executions also returned nonzero exit statuses.

**Explanation and follow-up:**

Without the reservation mutex, multiple producers can access `next_chunk` concurrently. This can lead to overlapping chunk assignments. The duplicate-consumption error is consistent with this race condition.

The final implementation retains `claim_lock` to ensure that only one producer updates the counter at a time.

**Lesson learned:**

Shared counters used to distribute work must be synchronized. Without mutual exclusion, correct work assignment cannot be guaranteed.

**Evidence:**

- `evidence/experiments_summary.txt`
- `evidence/e1_normal.lasterr`
- `evidence/e1_tsan.lasterr`
- `experiments/variants/e1_nolock.patch`


### 5.3 E2 — Replacing `while` with `if` in Condition Waiting

**Primary category:** Deliberate changes and tests

**Concept:** Condition waiting

**Question and prediction:**

What happens if a waiting thread checks the queue condition using `if` instead of `while`?

The expected risk is that a thread may continue after waking without verifying that the condition allowing it to proceed is still satisfied.

**Setup and change:**

The condition-waiting logic was modified in `e2_if_wait.c`, replacing the original `while` check with `if`.

The configuration was:

- Producers: 1
- Consumers: 8
- Chunk size: 8 bytes
- Queue capacity: 1
- Input: `evidence/mid.txt`
- Repetitions: 60
- Timeout threshold: 3 seconds per execution

**Actual observations:**

Of the 60 executions, 56 completed with correct output and 4 exceeded the timeout threshold. No incorrect successful outputs or nonzero exits other than timeouts were recorded.


**Explanation and follow-up:**

Condition-variable notifications do not guarantee that the condition being waited for is still satisfied when a thread resumes. Another consumer may remove an item before the awakened consumer reacquires the mutex, or a spurious wake-up may occur.

In the modified implementation, replacing `while` with `if` allows a consumer to proceed without rechecking whether the queue contains work. If the queue is empty, the consumer may incorrectly terminate even though producers are still active.

If enough consumers terminate prematurely, the remaining consumers may be unable to drain the queue. Producers can then become blocked while waiting on `not_full`, preventing the program from completing.

This mechanism is consistent with the four timeouts observed in 60 executions, but it was inferred from the modified code rather than directly confirmed through thread-state inspection.

The final implementation retains `while` loops around condition-variable waits to ensure that threads recheck the relevant conditions after waking.

**Lesson learned:**

Condition-variable waiting must be based on a repeatedly checked condition rather than assuming that receiving a notification guarantees progress.

**Evidence:**

- `evidence/experiments_summary.txt`
- `experiments/variants/e2_if_wait.patch`


### 5.4 E3 — Freeing a Buffer Immediately After Handoff

**Primary category:** Deliberate changes and tests

**Concept:** Memory ownership and termination

**Question and prediction:**

What happens if the producer frees a buffer immediately after transferring it to the shared queue?

The expected risk is that the consumer may access memory that has already been released, causing invalid memory access or program failure.

**Setup and change:**

The experimental variant `e3_free_after_handoff.c` was modified so that the producer frees the buffer immediately after handing it off.

The configuration was:

- Producers: 1
- Consumers: 1
- Chunk size: 61 bytes
- Queue capacity: 2
- Input: `evidence/mid.txt`
- Normal repetitions: 100
- AddressSanitizer repetitions: 5

**Actual observations:**

All 100 normal executions exited with nonzero status. The recorded error was:

```text
free(): double free detected in tcache 2
```

The five AddressSanitizer executions also failed.

AddressSanitizer reported:

```text
ERROR: AddressSanitizer: heap-use-after-free
```

The trace identified the following operations:

- The producer allocated the buffer at line 261.
- The producer freed the buffer at line 281.
- The consumer accessed the released memory through `analyze_chunk()` at line 150.

**Explanation and follow-up:**

After the producer places a buffer in the queue, the consumer still needs that memory for processing.

If the producer releases the buffer immediately, the consumer may read freed memory. The normal execution also detected a double-free error because the buffer was released more than once.

The final implementation transfers ownership of the buffer to the queue and then to the consumer, which frees it only after completing its analysis.

**Lesson learned:**

Memory ownership must be clearly defined when transferring data between threads. A buffer must remain valid until the thread responsible for processing it has finished.

**Evidence:**

- `evidence/experiments_summary.txt`
- `evidence/e3_plain.lasterr`
- `evidence/e3_asan.lasterr`
- `experiments/variants/e3_free_after_handoff.patch`


### 5.5 E4 — Treating the First Producer's Completion as Global Completion

**Primary category:** Deliberate changes and tests

**Concept:** Memory ownership and termination

**Question and prediction:**

What happens if the program considers production complete when the first producer finishes, even though other producers may still be working?

The expected risk is that consumers may terminate before all chunks have been processed.

**Setup and change:**

The termination logic was modified in `e4_first_eof.c` so that the first producer's completion could be treated as global completion.

The experiment used the following configuration:

- Input: `evidence/mid.txt`
- Producers: 2
- Consumers: 2
- Chunk size: 61 bytes
- Queue capacity: 2
- Repetitions: 200
- Timeout threshold: 3 seconds per execution

The experiment was executed using `experiments/run_experiments.sh`.

**Actual observations:**

Of the 200 executions, 155 completed with correct output and 45 terminated with a nonzero exit status. No timeouts or incorrect successful outputs were recorded.

The error log contained:

```text
prod_cons: error: only 21210 of 21211 chunks were consumed
```

This indicates that one chunk was not processed in the recorded failed execution.



**Explanation and follow-up:**

The modified implementation incorrectly treats the completion of one producer as the completion of all producers.

Since different producers may finish at different times, this can cause consumers to terminate before the remaining chunks have been supplied or processed.

In the recorded failed execution, the program detected that only 21,210 of the expected 21,211 chunks had been consumed. It reported an error rather than presenting incomplete statistics as a successful result.

The correct implementation tracks completed producers using `prod_done` and considers production complete only when `prod_done == nprod`.


**Lesson learned:**

The completion of a single producer does not indicate that all work has been finished. The termination protocol must account for every producer and ensure that all chunks have been consumed before reporting success.



**Evidence:**

- `evidence/experiments_summary.txt`
- `evidence/e4_first_eof.lasterr`
- `experiments/variants/e4_first_eof.patch`


### 5.6 E5 — Replacing Broadcast with Signal

**Primary category:** Deliberate changes and tests

**Concept:** Condition waiting

**Question and prediction:**

What happens if the final producer uses `pthread_cond_signal()` instead of `pthread_cond_broadcast()` to notify waiting consumers?

The expected risk is that some consumers may remain blocked after production has finished, preventing the program from terminating.

**Setup and change:**

The completion notification was modified in `e5_signal_not_broadcast.c`, replacing the broadcast operation with a signal operation.

Two configurations were tested:

| Configuration | Input | Runs | Chunk size | Queue capacity |
|---|---|---:|---:|---:|
| 1P/4C | `tiny.txt` | 10 | 65536 | 16 |
| 2P/4C | `mid.txt` | 20 | 61 | 2 |

Each execution had a timeout threshold of 3 seconds.

**Actual observations:**

For the small input, all 10 executions exceeded the timeout threshold.

For the larger input, 11 of 20 executions completed correctly, while 9 exceeded the timeout threshold.

No incorrect successful outputs or other nonzero exits were recorded.

**Explanation and follow-up:**

When production is complete, multiple consumers may be waiting on `not_empty`.

Using `pthread_cond_signal()` may wake only one consumer, leaving other consumers blocked without further notifications.

The final implementation uses `pthread_cond_broadcast()` when the last producer finishes, allowing all waiting consumers to recheck the termination condition.

**Lesson learned:**

The notification strategy must account for the number of waiting threads. A broadcast is appropriate in this termination protocol because all waiting consumers need to detect global completion.

**Evidence:**

- `evidence/experiments_summary.txt`
- `experiments/variants/e5_signal_not_broadcast.patch`


### 5.7 E6 — Removing Word-Boundary Stitching

**Primary category:** Deliberate changes and tests

**Concept:** Input partitioning and result merging

**Question and prediction:**

What happens if word fragments crossing chunk boundaries are not combined during final result merging?

The expected risk is that words spanning multiple chunks will be omitted or counted with incorrect lengths.

**Setup and change:**

Word-boundary stitching was disabled in the experimental variant `e6_no_word_stitch.c`.

The experiment used `evidence/longword.txt`, which contains words longer than the smaller chunk size.

Two configurations were tested:

| Configuration | Producers | Consumers | Chunk size | Runs |
|---|---:|---:|---:|---:|
| Large chunks | 1 | 1 | 65536 | 5 |
| Small chunks | 1 | 1 | 7 | 5 |

The queue capacity was 16 for both configurations.

**Actual observations:**

With a chunk size of 65536 bytes, all five executions produced correct results.

With a chunk size of 7 bytes, all five executions completed successfully but produced incorrect statistics.

The incorrect output was saved in `evidence/e6_chunk_7.lastbad`.

**Explanation and follow-up:**

When the chunk size is large enough, complete words may remain within individual chunks, allowing the modified implementation to produce correct results.

With smaller chunks, words frequently cross chunk boundaries. Without stitching, the partial word information is not correctly combined into complete word lengths.

The final implementation records leading and trailing word fragments and merges them in the original file order.

**Lesson learned:**

Chunk boundaries must not be treated as logical word boundaries. Correctness must be verified with inputs that force words to span multiple chunks.

**Evidence:**

- `evidence/experiments_summary.txt`
- `evidence/e6_chunk_7.lastbad`
- `evidence/longword.txt`
- `evidence/longword.ref`
- `experiments/variants/e6_no_word_stitch.patch`


### 5.8 E7 — Effect of Chunk Size on Work Distribution

**Primary category:** Hypothesis experiment

**Concept:** Input partitioning and result merging

**Question and prediction:**

How does chunk size affect the number of work transfers and execution time?

The prediction was that smaller chunks would increase the number of transfers between producers and consumers, resulting in greater synchronization overhead. The calculated statistics were expected to remain unchanged.

**Setup and change:**

The experiment used the same input file, `evidence/mid.txt`, with two producers and two consumers.

Three chunk sizes were tested: 64, 1024, and 65536 bytes. The queue capacity remained fixed at 16, and the implementation was not modified.

The experiment was conducted using the `PC_CHUNK` environment variable and the program's timing output.

The experiment was conducted using the following commands:

```bash
PC_CHUNK=64 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
PC_CHUNK=1024 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
PC_CHUNK=65536 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
```

The input file, numbers of producers and consumers, and queue capacity remained constant. Only the chunk size was changed.


**Actual observations:**

The input file contained 1,293,854 bytes.

| Chunk size (bytes) | Number of chunks | Execution time (s) |
|---|---:|---:|
| 64 | 20217 | 0.114681 |
| 1024 | 1264 | 0.009002 |
| 65536 | 20 | 0.005994 |

The number of chunks decreased significantly as chunk size increased. The recorded execution time also decreased.

For each configuration, the total number of chunks produced equaled the total number consumed.


**Explanation and follow-up:**

Smaller chunks require more producer-consumer transfers. Each transfer involves queue operations and synchronization, increasing overhead.

Larger chunks reduce the number of transfers and allow consumers to process more data per work item.

The observed measurements support the prediction that excessively small chunks can reduce performance. However, each configuration was measured only once, so additional repeated measurements would be necessary for a reliable performance comparison.

**Lesson learned:**

Chunk size influences the amount of synchronization and queue-management work. Choosing larger chunks reduced execution time in this experiment, although chunk size must also preserve correct boundary handling.

**Evidence:**

- `evidence/e7_chunk64.out`
- `evidence/e7_chunk64.log`
- `evidence/e7_chunk1024.out`
- `evidence/e7_chunk1024.log`
- `evidence/e7_chunk65536.out`
- `evidence/e7_chunk65536.log`


### 5.9 E8 — Effect of Queue Capacity on Execution Time

**Primary category:** Hypothesis experiment

**Concept:** Condition waiting and performance interpretation

**Question and prediction:**

How does the capacity of the shared queue affect execution time when the numbers of producers and consumers remain constant?

The prediction was that a smaller queue would cause producers to wait more frequently, potentially increasing execution time. Increasing the capacity was expected to reduce waiting, although the improvement might become smaller at larger capacities.

**Setup and change:**

The experiment used the input file `evidence/mid.txt`, containing 1,293,854 bytes.

The following settings were kept constant:

- Producers: 2
- Consumers: 2
- Chunk size: 1024 bytes
- Input file: `evidence/mid.txt`

Queue capacities of 1, 16, and 128 were tested using `PC_QCAP`. Each configuration was executed once with timing enabled.

The experiment was conducted using the following commands:

```bash
PC_CHUNK=1024 PC_QCAP=1 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
PC_CHUNK=1024 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
PC_CHUNK=1024 PC_QCAP=128 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
```

The input file, chunk size, and numbers of producer and consumer threads remained constant. Only the queue capacity was changed.

**Actual observations:**

| Queue capacity | Chunks processed | Execution time (s) |
|---|---:|---:|
| 1 | 1264 | 0.030573 |
| 16 | 1264 | 0.010728 |
| 128 | 1264 | 0.009316 |

All three configurations reported 1264 chunks. For each run, the total number of chunks produced was equal to the total number consumed.

The queue capacity of 1 produced the longest execution time. Increasing the capacity to 16 resulted in a substantial reduction in elapsed time. Increasing it further to 128 produced a smaller additional improvement.

**Explanation and follow-up:**

A small queue can become full frequently, forcing producers to wait for consumers to remove items.

A larger queue allows producers to supply multiple chunks before waiting, potentially reducing synchronization overhead.

The observed results support the prediction that increasing queue capacity can improve performance. However, the experiment consisted of one execution per configuration, so repeated measurements would be necessary to establish whether the observed differences are consistent.

**Lesson learned:**

Queue capacity affects producer-consumer coordination and can influence execution time. A very small capacity may introduce additional waiting, while larger capacities can reduce this overhead. Increasing capacity does not necessarily produce proportional performance improvements.

**Evidence:**

- `evidence/e8_q1.out`
- `evidence/e8_q1.log`
- `evidence/e8_q16.out`
- `evidence/e8_q16.log`
- `evidence/e8_q128.out`
- `evidence/e8_q128.log`


### 5.10 E9 — Effect of Producer-Consumer Allocation

**Primary category:** Hypothesis experiment

**Concept:** Performance interpretation and work sharing

**Question and prediction:**

How does the allocation of producer and consumer threads affect execution time when the total number of workers remains constant?

The hypothesis was that an unbalanced allocation could limit performance if either the producer or consumer role became a bottleneck.

**Setup and change:**

The experiment used `evidence/mid.txt`, containing 1,293,854 bytes.

The following settings remained constant:

- Total worker threads: 4
- Chunk size: 1024 bytes
- Queue capacity: 16
- Input file: `evidence/mid.txt`

Three configurations were tested: 1P/3C, 2P/2C, and 3P/1C. Each was executed once using the same compiled program.


The experiment was conducted using the following commands:

```bash
PC_CHUNK=1024 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 1 3
PC_CHUNK=1024 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 2 2
PC_CHUNK=1024 PC_QCAP=16 PC_TIMING=1 ./prod_cons evidence/mid.txt 3 1
```

Each configuration used four worker threads, the same input file, chunk size, and queue capacity.

**Actual observations:**

| Configuration | Chunks processed | Execution time (s) | Correct output |
|---|---:|---:|---|
| 1P/3C | 1264 | 0.017353 | Yes |
| 2P/2C | 1264 | 0.009506 | Yes |
| 3P/1C | 1264 | 0.016822 | Yes |

All three configurations produced identical statistics, matching the independently calculated results from `evidence/mid.ref`.

The 2P/2C configuration had the shortest execution time in this set of measurements.

**Explanation and follow-up:**

In the 1P/3C configuration, a single producer supplies work to three consumers. In the 3P/1C configuration, one consumer must perform all text analysis.

The 2P/2C configuration distributes both production and consumption across two threads. This may provide a better balance between file reading and text processing for the tested workload.

However, these results are based on one execution per configuration. Differences in thread scheduling and system activity could affect the observed times.


**Comparison with the required performance benchmark:**

In E9, the 2P/2C configuration recorded the shortest execution time (0.009506 seconds). However, the required performance comparison in Section 4 found that 1P/3C had the lowest median execution time.

E9 used a smaller input of approximately 1.3 MB, with only one execution per configuration. The measurements ranged from approximately 9 to 17 milliseconds, making them particularly sensitive to thread startup, scheduling, and measurement variability.

Section 4 used an approximately 269 MB input and five measured executions per configuration. Its median results provide a more reliable comparison for the larger workload.

Therefore, E9 demonstrates that thread allocation can influence execution time, but its single-run measurements are insufficient to establish a consistent performance ranking.


**Lesson learned:**

Increasing the number of threads in one role does not necessarily improve performance. The allocation between producers and consumers can affect execution time even when the total number of workers remains constant.

**Evidence:**

- `evidence/e9_1p3c.out`
- `evidence/e9_1p3c.log`
- `evidence/e9_2p2c.out`
- `evidence/e9_2p2c.log`
- `evidence/e9_3p1c.out`
- `evidence/e9_3p1c.log`
- `evidence/mid.ref`


### 5.11 E10 — Effect of Input Size on Parallel Performance

**Primary category:** Hypothesis experiment

**Concept:** Performance interpretation

**Question and prediction:**

Does increasing the input size change the performance benefit of using multiple consumer threads?

The prediction was that increasing the input size would increase execution time for both configurations, while using multiple consumers would continue to provide a performance benefit.

**Setup and change:**

Two input files were generated using `bench/make_input.sh` with requested sizes of 32 MiB and 256 MiB.

The generated files contained 34,143,850 bytes and 269,053,538 bytes, respectively.

The following settings remained constant:

- Producers: 1
- Consumer configurations: 1 and 3
- Chunk size: 65536 bytes
- Queue capacity: 16
- Compiler optimization: `-O2`


Each configuration was executed five times using `/usr/bin/time`, and elapsed wall-clock time was recorded. The same chunk size (65536 bytes) and queue capacity (16) were used for all configurations. No separate warm-up run was performed for this experiment. The original results are recorded in `evidence/e10_scaling.log`. The script `experiments/run_scaling.sh` was subsequently added to reproduce the experiment.


The input files were generated using:

```bash
bash bench/make_input.sh evidence/benchmark32.txt 32
bash bench/make_input.sh evidence/benchmark.txt 256
```

The experiment was conducted using the following loop:

```bash
for SIZE in 32 256; do
    if [ "$SIZE" -eq 32 ]; then
        FILE=evidence/benchmark32.txt
    else
        FILE=evidence/benchmark.txt
    fi

    for CONFIG in "1 1" "1 3"; do
        read -r P C <<< "$CONFIG"

        for RUN in 1 2 3 4 5; do
            /usr/bin/time -f "size=${SIZE}MB P=$P C=$C run=$RUN elapsed=%e" \
                env PC_CHUNK=65536 PC_QCAP=16 \
                ./prod_cons "$FILE" "$P" "$C" > /dev/null
        done
    done
done 2> evidence/e10_scaling.log
```

Five runs were performed for each configuration without a separate warm-up. The chunk size and queue capacity remained constant.


**Actual observations:**

| Input size | Configuration | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 | Median (s) |
|---|---|---:|---:|---:|---:|---:|---:|
| 32 MiB | 1P/1C | 0.18 | 0.18 | 0.18 | 0.18 | 0.18 | 0.18 |
| 32 MiB | 1P/3C | 0.07 | 0.07 | 0.07 | 0.07 | 0.07 | 0.07 |
| 256 MiB | 1P/1C | 1.44 | 1.44 | 1.45 | 1.48 | 1.45 | 1.45 |
| 256 MiB | 1P/3C | 0.59 | 0.67 | 0.57 | 0.70 | 0.40 | 0.59 |

The 1P/3C configuration had a lower median execution time for both input sizes.

The measured speedup was approximately 2.57 for the smaller input and 2.46 for the larger input.

**Explanation and follow-up:**

Increasing the input size increases the amount of data that must be read and analyzed, leading to longer execution times.

Using three consumers allows the analysis work to be distributed across multiple threads. This may reduce the processing time compared with a single consumer.

The recorded results show a similar relative performance benefit at both input sizes. However, variations in thread scheduling and system activity may affect the measured execution times, particularly for the 256 MiB input.

**Lesson learned:**

For the tested input sizes, using multiple consumers improved the observed performance. Execution time increased approximately in proportion to input size, while the relative benefit of additional consumers remained similar.

**Evidence:**

- `evidence/e10_scaling.log`
- `bench/make_input.sh`
- `evidence/benchmark32.txt`
- `evidence/benchmark.txt`
- `experiments/run_scaling.sh`


## 7. AI Assistance

ChatGPT was used to assist with debugging and understanding concepts related to multithreading, synchronization, and memory management.

AI also assisted with preparing test scripts, experimental variants, interpreting results, and organizing the report.