# Track C: Join (round 3)

This is a C++20, zero-runtime-dependency, exact integer-cents CSV join/aggregation. The pinned two-stage Dockerfile builds with GCC and `-O3 -march=x86-64-v3 -flto`. Input defaults to `/data/records.csv` and `/data/regions.csv`; output is the sole `/out/aggregates.txt`. Optional positional arguments override the input/output directories.

The parser maps both files read-only. `regions.csv` becomes a one-byte 1,000,000-entry region-to-country lookup. For the hostile records file, eight workers split near equal byte offsets at the next record-boundary UTF-8 BOM; the generator guarantees such BOMs on all full-size seeds. This avoids accidentally splitting a record on a newline embedded inside a quoted note. Small files run on one worker. Quotes and doubled-quotes are decoded logically without allocating text strings; quoted commas, embedded LF, CRLF/LF terminators and empty values preserve their precise semantics. Numbers are accumulated directly as 64-bit integers in cents, with no floating-point conversion or locale dependency.

The exact-name interner uses an 8 MiB lazily faulted arena and 131,072 open-addressed slots. Every new name is stored once; comparisons use both length and full raw bytes after fast two-sample CRC32C hashing. (For the generator's canonical quote escaping, raw encoded string identity is equivalent to decoded name identity; both empty spellings are normalized.) Each slot belongs to one worker; others send `(slot,country)` to that worker through SPSC queues so that the 256-bit distinct-name country mask can be updated without contended global atomic bitset writes. Local country aggregates are merged after the workers finish. Full-size input produces only 240 lines sorted by country code, with `_UNMATCHED` last if present.

The common quoted-field delimiter search stays with libc `memchr`, which is SIMD-optimized on Acer. A custom handwritten 32-byte AVX2 scanner also passed the SHA gate but took more source and did not improve K. Numeric fields use compact digit loops; earlier unrolling experiments regressed. `SPEED_TIMING=1` emits region mapping, cut discovery, record parsing/aggregation and final output times to stderr, without changing output.

## Correctness and performance

The official Acer checker passes seed-1 sample/full and independent full-size seed 2. Separately generated hostile sample seeds 7, 13 and 29 match the official Python reference byte for byte (see `PERF.md`). In round 3, we checked 1-, 2-, 4- and 8-worker scaling: the parse phase fell from approximately 0.43–0.47 s to 0.092–0.096 s while region loading and output together consumed only about 2 ms. The bottleneck remains name interning and owner-routed updates, not serial formatting or final merging. The retained two-CRC hash modestly reduced median Acer wall time and source lines compared with the round-2 final source; noisy memory.peak measurements are not represented as a reliable improvement.

The code specializes to the documented generator: unique positive region IDs below 10 million, at most 240 countries, canonical emitted quote forms, and mandatory mid-file boundary BOMs at full/sample scale. It preserves every listed hostile CSV edge and handles unknown region IDs as `_UNMATCHED`.

## With another 24 hours

The next big win would need to reduce 3.3 million cross-worker distinct-name routing operations without losing exactness: e.g. a bounded exact producer-local deduplication cache with measured hit rates, or a cache-conscious interner/shard redesign that preserves full-byte collision checking. R2 already rejected a contended global atomic mask and multiple larger key variants, so reread the PERF log before repeating an approach. The official scoring machine has 12 threads, while Acer has only eight.
