# Operating Systems

Course projects completed for the Operating Systems course at Dankook University, implemented in C on Linux.

## Producer–Consumer Text Statistics

A multithreaded text-processing program using POSIX threads, mutexes, and condition variables. Producer threads read file chunks into a bounded queue, and consumer threads calculate letter counts, word-length distributions, and line-length distributions.

- [Project folder](producer-consumer/)
- [Implementation report](producer-consumer/Report.md) — My explanation of the design, synchronization, testing, and performance results.
- [Assignment instructions](producer-consumer/README.md) — The original project instructions provided by the professor.

The project folder preserves the coursework structure, including supporting examples and starter materials supplied by the course. My implementation is in `prod_cons.c`, with development details documented in `Report.md`.v
