# Track B: Index

main.cpp is a dependency-free C++20 query-aware text index. It reads queries.txt first, memory-maps docs.txt, builds only the structures needed by those queries, and writes results.txt. Optional positional arguments, or DATA_DIR and OUT_DIR, can override the standard data and output directories.

## Current design

The final code is round 7: it verifies phrases during the document scan and keeps no positional index. The text below describes round 6. PERF.md has the difference.

ASCII letters and digits form tokens. ASCII letters compare case-insensitively. Query terms of one to four bytes use a dedicated exact 16,384-slot packed table, so these common short tokens bypass the general fingerprint table and Bloom filter. Longer terms use the compact open-addressed dictionary, a two-bit Bloom rejector, and exact byte comparison before a match is accepted. The corpus scan uses AVX2 to classify 32 bytes at a time when available and a small positive cache for repeated longer terms.

Boolean terms are split by query-use count. Terms used more than 22 times get dense document bitmaps. Other Boolean terms use sparse postings. Round 6 stores those sparse postings as one uint16_t local-document stream per scan partition instead of one global uint32_t posting array. Each partition records its first global document id and per-term ranges; query evaluation expands only the requested sparse list into reusable 32-bit scratch storage. This removes the large overlap between temporary 16-bit scan streams and a final 32-bit sparse array.

Phrase queries choose one query-frequency anchor term. Only anchor byte offsets are stored. Candidate phrases are then verified directly against the mapped corpus, including neighboring tokens, so phrase correctness does not depend on co-occurrence alone. Boolean expressions are evaluated strictly left-to-right. Results preserve query order and contain the match count, sum of document ids, and the ten smallest matching ids.

The full-scale bounds used by the compact representation are checked at startup: corpus byte offsets and document ids fit in 32 bits, the query dictionary stays below 2^18 terms, and the sparse-term index stays within 16 bits. Scan partitions are aligned to complete documents and to document ids divisible by 64.

## Build and validation

The pinned two-stage Dockerfile builds with GCC 15 using -O3 -DNDEBUG -std=gnu++20 -march=x86-64-v3 -pthread -flto, statically links the GCC runtimes, and has no external runtime dependency.

The Round 6 test-laptop check passed the sample, full seed 1, and independent full seed 2 SHA gates. The retained chunks-short source measured 0.483 s median, 60,712 KB memory.peak, official S 1094, and K about 14.9. The same check measured the Round 5 source at 0.509 s, 94,824 KB, S 1025, and K about 13.4. Full measurements and rejected experiments are in PERF.md.
