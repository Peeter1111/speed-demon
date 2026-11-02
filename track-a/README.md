# Track A: Ingest

`main.cpp` reads `requests.log` once and writes `summary.txt`. C++20, no dependencies. Paths come from the two
optional arguments, then `DATA_DIR` / `OUT_DIR`, then `/data` and `/out`. An empty log gives an empty summary.

## Approach

Two phases, both on every hardware thread (the count comes from the CPU affinity mask).

1. **Parse, once.** The log is mapped read-only and cut into newline-aligned chunks (8 per thread) that threads
   take from an atomic counter. For each line the hour comes from fixed byte offsets (timestamps are always 27
   bytes, and the span is the spec's fixed 72 hours), the method length from one 8-byte word, the endpoint end
   from one AVX2 compare, and the latency from a short digit loop. The endpoint is interned in the thread's own
   open-addressing table (8,192 slots, hash of the first, middle and last 8 bytes, confirmed with `memcmp`).
   Each line becomes a **4-byte record**: 11 bits of local endpoint id, one bit for 5xx, and a 20-bit latency
   payload. Latencies below 524,288 microseconds are stored directly; larger values, including `4294967295`,
   use a 19-bit index into an exact 32-bit per-worker overflow vector. The index marker is the high payload bit.
   Records are appended to **512-entry blocks** chained by thread and hour. The published generator has 1,842
   endpoints, so local ids fit in 11 bits. Parsing remains a single pass, and no line is parsed twice.
2. **Report, per hour.** A barrier merges the per-thread endpoint tables into one list sorted as unsigned bytes
   and maps every local id to its rank. Threads then take hours from a counter. An hour's records (about 43,000)
   are counting-sorted by endpoint rank, so groups come out in output order; each group's three nearest-rank
   values are selected (`std::sort` up to 32 values, three nested `std::nth_element` calls above), and the
   hour's lines are formatted into one buffer. Blocks are freed as they are consumed. The main thread writes
   finished hours in order while later hours are still being computed. The overflow values are recovered
   exactly while scattering records into each hour's latency array, so percentile selection sees full u32s.

The design follows from the scoring: time counts most (square root), so the input is scanned once and the
expensive part (percentiles and formatting) runs in parallel on cache-resident data.

## Limits

The program checks what it assumes and exits non-zero with a message instead of writing a wrong file: a
timestamp outside the 72-hour span, more than 6,000 distinct endpoints, a missing final newline, or more than
2,048 endpoints/524,288 wide latencies per worker (neither can occur for the published generator distribution).

## Another 24 hours

See PERF.md for the kept and rejected round-five trials. On local test laptop, 4-byte records and smaller blocks reduced
peak memory by roughly 3.2 MB and gave median wall-time improvements in two of three repeat checks, though
timings fluctuate with machine contention. Two-digit formatting, `to_chars`, lower selection thresholds,
SWAR newline discovery, different worker counts and mmap advice were tested and reverted. The next large
opportunity is an output/group aggregation design that avoids traversing each block twice or a more cache-
efficient per-hour record container; validate both against three exact full/sample SHA outputs.
