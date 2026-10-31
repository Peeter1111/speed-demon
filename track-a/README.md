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
   Each line becomes a 6-byte record, endpoint id with the 5xx flag in its top bit plus the latency, appended to
   the thread's block chain for that hour. No line is parsed twice.
2. **Report, per hour.** A barrier merges the per-thread endpoint tables into one list sorted as unsigned bytes
   and maps every local id to its rank. Threads then take hours from a counter. An hour's records (about 43,000)
   are counting-sorted by endpoint rank, so groups come out in output order; each group's three nearest-rank
   values are selected (`std::sort` up to 32 values, three nested `std::nth_element` calls above), and the
   hour's lines are formatted into one buffer. Blocks are freed as they are consumed. The main thread writes
   finished hours in order while later hours are still being computed.

The design follows from the scoring: time counts most (square root), so the input is scanned once and the
expensive part (percentiles and formatting) runs in parallel on cache-resident data.

## Limits

The program checks what it assumes and exits non-zero with a message instead of writing a wrong file: a
timestamp outside the 72-hour span, more than 6,000 distinct endpoints, a missing final newline.

## Another 24 hours

See PERF.md: the parse phase is within about 1.5x of the Acer's memory-read floor, so the next gains are in the
report phase (a sorting network for small groups, two-digit formatting tables) and in peak memory.
