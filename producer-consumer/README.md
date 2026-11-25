# HW2: Multi-threaded Text Statistics

**Due date: Oct. 30th**

Build a producer-consumer program in C using POSIX threads (`pthread`). Producers supply work from one input file or partitions derived from it; consumers compute ASCII letter counts and word-length and line-length statistics for the original file.

Threads have separate execution contexts but share the process address space. This assignment asks you to reason about shared state, synchronization, memory ownership, and termination, then evaluate how your design behaves as the numbers of producers and consumers change.

Work through the four development stages and keep a [development and experiment notebook](#development-and-experiment-notebook) as you go.

- [Getting started](#getting-started)
- [Program requirements](#program-requirements)
- [Design choices](#design-choices)
- [Correctness checks](#correctness-checks)
- [Performance comparison](#performance-comparison)
- [Development and experiment notebook](#development-and-experiment-notebook)
- [What to submit](#what-to-submit)

## Getting started

### Starting code

The example programs intentionally contain errors. They are starting points to investigate and modify; their current behavior does not define the required behavior of your submission.

| File | Purpose |
| --- | --- |
| `pthread.c` | Examples of creating threads, passing arguments, and collecting return values. |
| `prod_cons.c` | A producer-consumer starting point that transfers lines through shared state. |
| `char_stat.c` | Ideas for collecting and displaying statistics. Use the rules below to determine correct results. |

For the supplied single-file starting point, a possible build command is:

```sh
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -pthread prod_cons.c -o prod_cons
```

Adapt and document the build command for your implementation. Successful compilation alone does not establish correctness.

### Development stages

All four stages are required. `P` and `C` denote producer and consumer thread counts; the main thread is additional.

| Stage | Configuration | Goal |
| --- | --- | --- |
| **1. Reliable data transfer** | 1P / 1C | Coordinate shared state, waiting, buffer ownership, and completion. |
| **2. Correct text statistics** | 1P / 1C | Implement the counting rules and verify them against known results. |
| **3. Parallel work sharing** | Multiple P and C | Partition and distribute work, handle boundaries, and determine global completion. |
| **4. Performance analysis** | Vary P and C independently | Measure and explain the cost and benefit of concurrency. |

These are development checkpoints, not separate programs or operating modes. You may revise your design at any stage. Follow the [correctness checks](#correctness-checks) for Stages 1–3 and the [performance comparison](#performance-comparison) for Stage 4, recording investigations and lessons in the [notebook](#development-and-experiment-notebook) throughout.

## Program requirements

### Required scope and design freedom

| Required — must | Your choice — may |
| --- | --- |
| Implement producers and consumers as C POSIX threads with the required command line and counting behavior. | Restructure the starting code, add files, and choose input preparation and partitioning methods. |
| Preserve data ownership, correct waiting, complete coverage, and termination for all requested workers. | Choose work units, buffers, synchronization primitives, locking granularity, and result-merging algorithms. |
| Complete Stages 1–3 and the specified Stage 4 measurements. | Investigate additional configurations, optimize a verified implementation, or perform the available-CPU experiment. |
| Submit the final source, reproducible evidence, report, and experiment notebook. | Organize shared evidence in appendices or companion files and reference it from the report. |

**The basic assignment includes all four stages and the notebook.** The [optimization challenge](#optional-optimization-challenge) and [available-CPU experiment](#optional-experiment-available-cpu-count) are optional; additional features are not needed to complete the assignment. Optional investigations may count as notebook entries when they meet its requirements, but do not waive required checks, baseline measurements, entry minima, or concept coverage.

### Command line and thread counts

Your executable or launcher must support this interface, where `input-file` identifies the original input:

```sh
./prod_cons <input-file> <num-producers> <num-consumers>
./prod_cons ./sample.txt 1 2
```

- P and C are independent positive decimal integers. Create the requested number of each worker role, in addition to main.
- Support at least counts `1`, `2`, and `3` independently for each role. Document any upper limits you impose and accept positive counts up to those limits, subject to available system resources. Reject unsupported counts instead of silently changing them.
- All three arguments are required. For missing or invalid arguments, an unreadable input, or a failure that prevents completion, print an explanatory message to standard error and exit with a nonzero status. A successful run exits with status `0`. Do not present a partial count as a successful result.

### Input and counting rules

The logical input is one original regular file. Read its contents as bytes without modifying it. You may assume its contents do not change during a run. Your implementation may prepare derived partitions as described in [Input partitioning and preparation](#input-partitioning-and-preparation); the required result always describes the original file.

All lengths are measured in **bytes**. Unicode decoding and locale-dependent alphabet rules are outside the required scope. Compute all three kinds of statistics below over the complete original input.

**ASCII letter counts**

- Count the ASCII letters `A`–`Z` and `a`–`z`. Combine uppercase and lowercase into the same 26 counters: `A` and `a` both contribute to the `A` counter.
- Ignore every other byte for the letter counts, including digits, punctuation, whitespace, NUL bytes, and bytes outside ASCII. These bytes still participate in the word and line rules below.
- Empty files and files with no ASCII letters produce 26 zero letter counts.

**Word lengths**

- A word is a maximal nonempty run of ASCII letters: `[A-Za-z]+`. Every other byte is a separator, including digits, punctuation, NUL, and non-ASCII bytes. Case does not affect word boundaries.
- Record the total number of words and, for each observed length, the number of words of exactly that length. There are no length-zero words. For example, `ab12C` contains words of lengths 2 and 1.

**Line lengths**

- Every LF byte (`0A`) terminates one line. The LF is excluded from the line length. If the byte immediately before that LF is CR (`0D`), exclude that CR as well: the CRLF pair is one line ending. A CR not immediately followed by LF is part of the line contents.
- Count every other content byte in the line length, including spaces, digits, NUL, and non-ASCII bytes. Empty lines have length 0.
- If nonempty contents remain after the last LF, or the nonempty file contains no LF, count those remaining bytes as one final line. A trailing CR in this final line is content. An empty file has zero lines, and a file ending in LF does not gain an additional empty line after it.
- Record the total number of lines and, for each observed length, the number of lines of exactly that length.

**Boundaries and limits**

Work-unit and partition boundaries do not end words or lines. Preserve their full lengths across any number of boundaries, including when CR and LF fall in different units. Derived partitions must not introduce logical boundaries into the original input.

Both length distributions must retain exact lengths without truncating a word or line, clamping its length, or combining longer lengths into an overflow bucket. There is no length limit of 30 from the starting code. Choose data structures that support the lengths in the input; if a resource or numeric limit prevents completion, report failure instead of an incorrect successful result.

### Worked example

Create a small input with:

```sh
printf 'A a b!? Zz\n' > sample.txt
./prod_cons sample.txt 1 2
```

The expected letter counts are `A = 2`, `B = 1`, and `Z = 2`; every other letter has count `0`. There are 4 words: three of length 1 and one of length 2. There is 1 line of length 10; its terminating LF is excluded.

### Output

- Print one final summary to standard output containing all 26 letters and their exact integer counts, including zeros, the total word and line counts, and the two separate length distributions. Labels must distinguish letter counts, word lengths, and line lengths. You may omit unobserved lengths from a distribution; omitted lengths mean frequency 0. You may choose the layout and add clearly labeled timing or diagnostic information.
- The frequencies in each length distribution must sum to its reported word or line total.
- All required text statistics must be identical for the same input across repeated runs and all supported thread counts. Counter or length overflow must not silently produce an incorrect result.

### Work sharing and lifecycle

- Partitions must cover the original bytes exactly once. Producers supply the work, and consumers compute the statistics so that each letter, complete word, and complete line is counted once, without omissions or duplication. Each consumer must not independently analyze the whole file.
- Producers and preparation tools may inspect bytes to select boundaries and attach offsets, lengths, or other boundary metadata. Consumers must analyze the supplied text to compute the required statistics or partial results; producers must not precompute the required counts and merely pass them to consumers. Results and boundary information may be combined after consumption. Exactly-once coverage concerns contributions to the result, not how often bytes are inspected to find boundaries.
- All producers must finish supplying work and all supplied work must be consumed before reporting success. EOF for one producer or one partition does not establish completion of the whole input. All worker threads must terminate, including producers and consumers that receive no work.
- Correctness must not depend on a particular thread schedule, creation order, or an arbitrary delay. Avoid data races, invalid memory accesses, and deadlocks; release resources your program owns.
- Work need not be divided equally among workers. Processing and display order are unrestricted.

### Work units

Document the rule for forming work units and transfer separate units when the input contains them. The implementation must support repeated producer-to-consumer transfers, including with 1P/1C; handing off the entire file as one item must not be the general processing strategy. A small input or a single complete-line unit may legitimately produce only one item.

No fixed byte limit is prescribed. Complete lines, fixed-size chunks, and adaptive units are permitted, subject to the counting and boundary rules. Demonstrate repeated transfers using a suitable input containing multiple units in the correctness checks below.

## Design choices

You may restructure the starting code and add source files while preserving the [program requirements](#program-requirements). No particular queue, locking granularity, waiting primitive, or statistics-merging algorithm is required.

Choose and explain your own:

- Input preparation, partitioning, and assignment of partitions to producers.
- Work unit: lines, fixed-size chunks, or another partition of the input.
- Shared buffer organization, capacity, and memory ownership rules.
- Synchronization and waiting strategy, including how completion is communicated.
- Statistics storage, handling of words and lines at boundaries, and combination strategy.
- Allocation strategy and placement of work inside or outside critical sections.

POSIX mutexes and condition variables are useful tools to study, including `pthread_mutex_lock()`, `pthread_mutex_unlock()`, `pthread_cond_wait()`, and `pthread_cond_signal()`.

If you attempt the optional optimization challenge, distinguish the change from your baseline design and preserve all program requirements.

### Input partitioning and preparation

You may coordinate reads from the original file, assign file regions to producers, divide the file into multiple physical files, or use another partitioning method that meets the required behavior. These are examples, not prescribed implementations.

- To preserve word and line boundaries, you may combine fragment information in original input order, adjust partition boundaries to complete words or lines, or use another correct design. Boundary information may be merged after consumption.
- The number of partitions is independent of the number of producers. A producer may handle several partitions; describe how work is assigned and how completion is detected.
- Preparation may be part of the C program or performed by helper tools invoked through a launcher. Provide a complete, reproducible invocation starting from the original file, using the interface above. The requested producer and consumer workers must still be implemented as POSIX threads in C.
- Describe how derived files or other preparation artifacts are created, located, reused, and cleaned up. Report any additional helper processes or threads used for preparation; they are not included in the requested producer/consumer counts.
- Follow the [measurement procedure](#measurement-procedure) when accounting for preparation costs.

## Correctness checks

Use the following progression:

1. **Stage 1 — transfer and termination:** With 1P/1C, verify that each work item arrives once, its buffer remains valid during use, and pending work is consumed before both workers exit. Test empty input too. Temporary tracing or line printing can help; no debug-output format is required.
2. **Stage 2 — statistics:** With 1P/1C, check all required statistics against known answers or an independently checked reference using the cases below.
3. **Stage 3 — concurrency:** Repeat those checks with 1P/2C and 2P/1C, then 2P/2C. Compare with the independent answers as well as Stage 2, check that every worker terminates, and repeat runs to exercise scheduling variations.

Explain how you obtained the expected results independently of the starting code. Agreement between thread counts or a single successful run does not establish correctness. Check at least:

| Case | What to check |
| --- | --- |
| Empty file | All 26 letter counts are zero, word and line totals are zero, both distributions are empty or zero, and all threads terminate. |
| File containing only nonletters, such as `12!` | Letter and word counts are zero; this example still has one line of length 3. |
| Mixed case and punctuation | Check all statistics against the [worked example](#worked-example). |
| Final line without a newline, such as `Ab` | Count one word of length 2 and one line of length 2, as well as both letters. |
| Empty lines and trailing LF, such as bytes `0A 0D 0A` | Count two lines of length 0, zero words, and no additional line after the final LF. |
| Standalone CR, such as bytes `41 0D 42` | Count two words of length 1 and one line of length 3. The CR is a word separator and line content. |
| A word crossing a boundary, such as `A` followed by `BC` in the next work unit or partition | Count one word of length 3, not words of lengths 1 and 2. |
| A CRLF pair crossing a boundary, such as `41 0D` followed by `0A` | Count one line of length 1; the split CRLF still excludes both ending bytes. |
| A word and a line longer than 30 bytes, spanning several work units or input partitions where applicable | Preserve their full lengths and count each complete word and line once. Check more than one crossed boundary. |
| A file containing bytes `41 00 61 FF 5A` in hexadecimal | `A = 2`, `Z = 1`, and all other letter counts are zero. Count three words of length 1 and one line of length 5. NUL does not end the input. |
| An input containing multiple work units | Demonstrate repeated transfers with 1P/1C and a multiple-worker configuration, with every unit consumed exactly once. Use traces identifying work units, per-unit consumption counts, or equivalent evidence; no particular logging format is required. |
| Less work than producers or consumers | Workers with no work still terminate. |
| Unequal partition sizes or a producer finishing before others, where applicable | Early completion of one producer does not discard other producers' remaining work. |
| Missing file or invalid thread count | A clear error and nonzero exit status. |

## Performance comparison

This is required Stage 4. Verify correctness before timing each configuration; every timed run must compute all required statistics. Use the same original input, machine, build, and optimization settings throughout a comparison.

Measure a **`1P/1C` baseline**, then compare **`1P/3C`, `2P/2C`, and `3P/1C`**. The latter three have the same total of four workers and test how allocation between roles affects performance. You may explore other allocations too; no ratio is assumed to be fastest.

### Input selection

Use an input large enough for meaningful timing. You may supply your own file or use the [Linux 6.12 source archive](https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz) from the official [Linux Kernel Archives](https://www.kernel.org/). This example pins the version so that repeated downloads refer to the same dataset.

With `curl` and `xz` installed, download and decompress the archive before measurement:

```sh
curl -fL -o linux-6.12.tar.xz https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz
xz -dk linux-6.12.tar.xz
./prod_cons linux-6.12.tar 1 1
```

Use the resulting **`.tar` file**, rather than the compressed `.tar.xz`, as the original input for this example. Count all of its raw bytes, including archive headers and padding; the program does not need to parse the archive or implement decompression. Alternatively, prepare one common input of extracted source text and document its reproducible creation. Use the same original input for every configuration.

Download, decompression, and common dataset creation may happen once outside timing; subsequent implementation-specific preparation, such as splitting that input for producers, follows the measurement procedure below.

### Measurement procedure

1. Measure elapsed **wall-clock time for the complete invocation**, from implementation-specific preparation through final reporting and cleanup. Include partition creation and I/O, thread creation, counting, and result combination. A launcher may group these commands into one timed run.
2. Include the needed preparation in every main run, even if usable partitions already exist. Timings that reuse prepared partitions are supplementary: label them, report their creation cost separately, and do not compare them directly with complete-invocation timings. OS file caching is allowed and does not count as reusing partitions.
3. Disable per-line and per-work-item debug printing, retain the final statistics, and use the same output destination for every configuration.
4. Run each configuration **at least five times**. Record every measurement and the median. Use a consistent warm-up policy and describe it; manually clearing the OS file cache is not required.

### Results and interpretation

Report the input size, CPU model and core count, OS, compiler, build flags, timing method, P and C separately, and any preparation steps or helper processes/threads. Explain trends and likely bottlenecks, including preparation overhead. A speedup is not required; a well-supported explanation of a slowdown is valid. Tools such as `htop` can supplement, but not replace, timing measurements.

### Optional optimization challenge

Use evidence about a bottleneck to guide a hypothesis and design change. Explain the hypothesis and change, recheck correctness, and compare before and after under the same measurement conditions. Report and explain the result even if it is slower. All program requirements continue to apply.

### Optional experiment: available CPU count

Thread count and available CPU count are separate variables. This experiment is optional; if CPU limits cannot be controlled, document the available environment. Otherwise, let G be the number of logical CPUs on which the program may run and compare:

| Comparison | Change | Hold fixed |
| --- | --- | --- |
| Available CPU scaling | G = 1, 2, and 4, using only supported counts | P/C = 1P/3C and the same input and processing settings |
| Producer/consumer allocation | 1P/1C, 1P/3C, 2P/2C, and 3P/1C | The largest tested G and the same allowed CPU IDs |

The comparisons share one configuration: with three values of G, there are six distinct configurations. Use the same correctness and measurement procedure as above, holding input, work units, queue capacity, memory limits, build, and warm-up policy fixed.

In Docker, `--cpuset-cpus` restricts allowed CPU IDs, whereas `--cpus` sets a CPU-time quota. If IDs 0–3 are available and appropriate for the topology, sets `0`, `0,1`, and `0-3` provide G = 1, 2, and 4. Keep quotas nonrestricting and record inherited limits; study quota changes separately. See [Docker CPU resource controls](https://docs.docker.com/engine/containers/resource_constraints/).

Record selected and actual allowed CPU IDs, physical/logical topology where available, VM vCPU allocation, and quotas. Logical CPUs may share cores through SMT, and vCPUs do not establish a fixed mapping to host cores. Prefer nested sets of comparable CPUs and explain topology limitations. Main and preparation helpers share the worker CPU allowance; it does not reserve CPUs exclusively. Keep VM settings fixed while changing the container CPU set.

Label the two speedup measures separately, using measured baselines:

- CPU scaling: `time(G=1, same P/C) / time(G, same P/C)`.
- Thread allocation: `time(same G, 1P/1C) / time(same G, P/C)`.

Compare elapsed time and throughput, then use separate CPU-usage and queue-wait observations to explain possible input, synchronization, memory, or final aggregation limits.

## Development and experiment notebook

Submit **at least 10 distinct entries** tracing your questions, predictions, tests, observations, explanations, and lessons learned. You may reuse the required correctness checks and performance measurements through cross-references; their original requirements still apply. Optional experiments remain optional.

### Categories and counting

Assign each entry one primary category; count it once toward the total and only that category's minimum.

| Category | What to record | Minimum |
| --- | --- | --- |
| **Discovered defects** | Investigate and correct a defect found in the starting code, your code, or AI-generated code. Identify its source and distinguish it from a deliberately introduced defect. | No fixed minimum. |
| **Deliberate changes and tests** | Change or remove part of a synchronization, ownership, or termination protocol to test whether correctness depends on it. Design a test to distinguish the changed version from a correct baseline. | **3 entries** |
| **Hypothesis experiments** | Predict and investigate behavior involving correctness, waiting, work sharing, boundaries, or performance by choosing inputs or varying a relevant factor. | **3 entries** |

Choose remaining entries from any category. Record actual discoveries; you are not required to make or invent accidental mistakes.

**One question and its investigation form one entry.** Repeated runs, P/C settings for the same question, and discovery, correction, and retesting of one defect belong together. Separate entries must address different questions or failure mechanisms.

### Concept coverage and examples

Cover **each of the five concepts below at least once** and label the concepts in each entry. One entry may substantively cover several concepts but still counts once. The questions are examples; choose investigations that fit your design. Condition waiting concerns the predicate that permits progress and how workers wait for it, without requiring a particular API.

| Concept | Example questions |
| --- | --- |
| Mutual exclusion and shared state | What changes when protection is removed from a shared counter? Can unprotected work reservation assign overlapping work to producers? |
| Condition waiting | What happens when a condition-wait loop uses `if` instead of `while` with multiple consumers? Can replacing the final wake-all notification with `signal` leave waiters behind? |
| Memory ownership and termination | What happens when a producer frees or reuses a buffer after handing it off? Does treating one producer's EOF as global completion discard another's work? |
| Input partitioning and result merging | Does a word spanning several work units retain its full length? Is a CRLF pair split across partitions handled correctly? |
| Performance interpretation | How do `1P/3C`, `2P/2C`, and `3P/1C` compare? How does buffer capacity or the amount of work inside a critical section affect time while preserving correct results? |

Explain which property a change tests: signaling after unlocking, using a `NULL` termination marker, or sharing a properly synchronized counter can all be correct designs. A changed version is not automatically faulty.

### Entry format and evidence

Include an index listing each entry's ID, title, primary category, concept labels, and evidence locations. Keep each explanation to at most half a page, excluding patches, result tables, and raw logs. Document common environment and build settings once; reference them and note any differences.

| Field | Required content |
| --- | --- |
| **Question and prediction** | What did you want to find out? What result did you expect, and why? |
| **Setup and change** | Source version and change, input or generation method, P/C, relevant work-unit/buffer settings, and exact build/run commands. State what varied and what stayed fixed. References to shared settings are sufficient. |
| **Actual observations** | Output excerpts, errors, thread states, or measurements, with raw evidence locations. Record repetitions, failures, and any timeout threshold. Separate observations from predictions. |
| **Explanation and follow-up** | Explain the relevant state, ownership, thread execution order, boundary behavior, or cost. For defects and deliberate changes, compare faulty or changed versions with the baseline or corrected version under the same tests, and verify the correction or restoration. State conclusions and limits. |
| **Lesson learned** | State what you learned or confirmed and how it informed your final design, correction, or next test. Connect the lesson to this entry's evidence. |

Include inputs or generators, raw logs, and reproducible source changes. Patches against the submitted final code or another included baseline, or source snapshots, are sufficient. Separate stage programs and full Git history are not required; a commit ID alone is insufficient unless the source is included. Keep experimental variants separate from the final working implementation, and match diagnostic file names and line numbers to the tested version.

### Interpreting results

Failure to reproduce a bug, a contradicted prediction, or no clear performance difference is a valid result with an appropriate test, evidence, and explanation. Passing repeated runs or receiving no sanitizer warning does not prove correctness; a timeout alone does not prove deadlock. Use evidence appropriate to the question, such as expected results, assertions, backtraces, or sanitizer reports. No particular debugger or sanitizer is mandatory. Machine-specific output and commit timestamps alone do not establish that an investigation was performed.

## What to submit

### Submission contents

Include one final implementation, all files needed to build and run it, a report, and the evidence referenced by that report. Include helper scripts, test inputs or generators, raw outputs and measurements, and the source patches or snapshots needed for notebook experiments. Correctness checks may use the final implementation; experimental variants follow the [notebook evidence requirements](#entry-format-and-evidence).

The report may be **Markdown or PDF**. Keep the notebook in the report, an appendix, or a clearly linked companion document. Its per-entry length limit is defined in the notebook format; no additional overall page limit is imposed here. Submit raw evidence as files, including text or CSV timing records, rather than only screenshots. Use a directory layout of your choice with working relative references.

### Report outline

Use the following numbered sections as a checklist. Shared environment descriptions, tables, and logs may be stored once and cross-referenced. Put long outputs and raw measurements in an appendix or companion evidence directory; include their relative paths in the report.

| Report section | Required contents |
| --- | --- |
| **1. Build and run** | Supported environment, compiler and build flags, exact build/run commands starting from the original input, any launchers or preparation tools, thread-count limits, and output format. |
| **2. Final design** | The choices in [Design choices](#design-choices): producer/consumer roles, work units and assignment, buffer capacity and ownership, shared state and synchronization, completion, statistics storage, boundary handling, merging, and allocation/critical-section choices. Identify any additional helper processes or threads. If preparation creates artifacts, explain their creation, location, reuse, and cleanup. |
| **3. Correctness** | How expected answers were obtained independently; expected and observed results for the [Stage 1–3 checks](#correctness-checks); tested P/C configurations, repetitions, and evidence locations. Explain how the final implementation satisfies transfer, boundary, and termination requirements. |
| **4. Required performance comparison** | Input size and preparation, CPU model and core count, OS and build settings, timing command, output destination, warm-up policy, and preparation/helper costs. Give each individual measurement and median for every required P/C configuration, followed by an explanation of the trend and likely bottlenecks. |
| **5. Development and experiment notebook** | The indexed entries meeting the [category, concept, format, and evidence requirements](#development-and-experiment-notebook). Include lessons learned, not only descriptions of changes or outputs. The notebook may be an appendix linked from this section. |
| **6. Optional work, if attempted** | For optimization: bottleneck evidence, hypothesis, change, correctness checks, and comparable before/after measurements. For the CPU experiment: selected/actual CPU IDs, topology and VM allocation where available, quotas and limitations, measurements, and separately labeled baselines. Distinguish optional results from the required comparison; if neither was attempted, state that briefly. |
| **7. AI assistance** | If used, identify the tools/models where known, their purpose, affected code or report sections, what suggestions you adopted or corrected, and how you checked them. Reference relevant notebook entries or tests. If unused, state that. A concise account of relevant assistance is sufficient; full chat transcripts are not required. |

### Assessment criteria

A working implementation that meets the program requirements is the foundation for assessment. Successful compilation or plausible output alone does not satisfy the assignment. Experimental claims must be supported by the submitted code and evidence; optional work does not compensate for missing basic requirements.

The report and notebook are assessed on:

- **Design reasoning:** Clear responsibility, ownership, synchronization, boundary, and termination arguments for the chosen implementation.
- **Experimental design and evidence:** Appropriate questions and controls, independent expected results, consistent measurements, and reproducible source and execution records.
- **Explanation and learning:** Accurate causes, justified performance interpretations, explicit limits, and specific lessons that informed corrections or design decisions.

Meet the entry minima and concept coverage. Additional entries, accidental defects, successful bug reproductions, or faster results alone do not earn a higher score. Optional experiments are evaluated using the same evidence and explanation criteria.

Happy hacking!
Seehwan
