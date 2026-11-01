# Track C: Join (round 5)

C++20, zero runtime dependencies, exact-cent CSV join/aggregation. The pinned two-stage Dockerfile builds with GCC using `-O3 -march=x86-64-v3 -flto` (AVX2-capable, never AVX-512). It reads `/data/regions.csv` and `/data/records.csv` and writes only `/out/aggregates.txt`. Two optional positional arguments override the input and output directories for testing.

## Data handling and correctness

Input files are memory mapped read-only. Region IDs index a 1,000,000-byte country lookup, with absent or out-of-range records assigned `_UNMATCHED`. The full hostile CSV is split into eight approximately equal byte ranges by scanning forward to the next BOM at a record boundary. The documented full-scale generator guarantees these BOMs. Tiny files use a single thread. This avoids mistaking a quoted note's embedded LF for a record terminator.

Name and note fields are parsed logically without allocating decoded strings. A quoted `""` escape contributes one byte; CRLF and LF record terminators, embedded LF, trailing spaces, embedded commas, mid-file BOMs, quoted/unquoted empty strings and negative cents all retain the reference semantics. Numeric values accumulate as integers, never float. Country rows are sorted, and only nonempty countries and any `_UNMATCHED` group are output.

## Exact names without per-record allocations

A global interner assigns each exact name a slot in an open-addressed table of 131,072 entries. Two independent CRC32C folds of the first/last eight bytes and length quickly select a candidate slot and fingerprint; collision resolution still verifies the complete raw name bytes exactly. The generator quotes every nonempty name in one canonical escaped form, so raw-string equality is equivalent to decoded equality; empty quoted and unquoted spellings normalize to the same zero-length key. Each newly interned name is copied once to a lazily faulted 8 MiB capacity arena (actual use is much smaller).

Round 5 replaced the previous producer-owner name queues with a single 256-bit atomic country mask per interned name. Relaxed fetch-OR on one 64-bit word tells exactly whether the (country, name) pair was previously seen. This removes queue contention, draining, and end-of-stream synchronization, while preserving the single-byte region lookup and per-thread integer aggregates.

The record parser batches 32 names at a time. It computes each name hash as part of parsing, prefetches the candidate interner table entry, and then resolves and marks those 32 names. This overlaps unpredictable table loads with useful sequential CSV work. Text delimiters use glibc's optimized `memchr` fast path; a separate 64-byte AVX2/PCLMUL structural parser was SHA-correct but slower under the same compact name interner and was reverted. `SPEED_TIMING=1` prints optional phase timing to stderr.

## Measurements and remaining work

The mandated Acer checker returned matching SHA-256 for sample, full, and a separate full-size seed 2 on every measured retained variant. Round 5's 32-name prefetch candidate measured **0.095 seconds median** on Acer (five full runs), **20,128 KB** cgroup `memory.peak`, **441 official formatted scc source lines**, and **K_acer ~16.6**. These are one set of repeated measurements, not a guaranteed scoring-box result. The copied round-3 source initially measured 0.103 s, 26,076 KB, S 504 and K 14.8 in this worker's run. Earlier round 3 obtained K 17.7 with 10,804 KB due to highly variable memory accounting; do not interpret cross-day cgroup peaks as an inherent 2.5x regression. Full step-by-step controls, failed experiments, and repeat results are in `PERF.md`.

With another 24 hours, I would profile the eight-thread parser's random interner access and country-mask cache-line contention on the actual scoring hardware, then evaluate cache-aware batched exact-name lookup or an independently validated per-country radix deduplication path. The existing test history already rejected bigger tables, short-name hand comparisons, per-thread 32 MiB bitmaps, linear probing, smaller atomics and standalone structural scans; a replacement must beat those with matching SHA gates, not just a single fastest time.
