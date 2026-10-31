# Track C: Join — round 2

This is the round-2, zero-runtime-dependency C++20 solution. It memory-maps the two CSVs, uses a dense one-byte region-to-group lookup (a separate sentinel for unmatched), and parses the generated hostile CSV without allocating per record. Amounts remain signed integer cents. It correctly counts escaped quote bytes, embedded note newlines, both record terminators, empty names, trailing spaces, and mid-file BOMs.

For large files eight parser threads split the stream at the next **record-boundary BOM** after each roughly even eighth of the file. The official generator guarantees mid-file record BOMs in every dataset. A fully quote-aware arbitrary-boundary AVX2 splitter was benchmarked, but its extra full-file pass lost score and was reverted. Files below 1 MiB use one parser thread. The `SPEED_TIMING=1` environment variable prints the region, splitting, parsing, and output phases to stderr without changing the output.

For distinct names, a 131,072-slot interner hashes the first/last bytes plus length, checks exact raw name bytes stored once in an 8 MiB reserved arena, and resolves collisions by exact comparison. This representation is equivalent to decoded equality on the generator's canonical quoted names; both empty encodings share a zero-length key. The 8 MiB reservation leaves room for longer holdout names, while untouched BSS pages do not count toward peak resident memory. The mask for each interned name has 256 country bits. A slot belongs to one parser thread; other threads send `(slot,country)` updates through fixed-size single-producer/single-consumer queues, so country masks are changed without atomic bitset contention. Each parser maintains its own scalar aggregates, merged after all workers finish.

## Build and run

The pinned two-stage Dockerfile builds with GCC 15, `-O3 -march=x86-64-v3` and no external libraries. The runtime uses the pinned Debian 13 slim image. Invoke `./run [data_dir] [out_dir]`; omitted paths default to `/data` and `/out`. The program emits only `aggregates.txt`.

## Evidence and next steps

The previous Acer `check1.sh` run of this 8 MiB-arena variant reported **0.101 s** median full-file time, **10,384 KB** memory.peak, approximately **429** source lines, **K ≈ 18.4**; it passed both official sample/full hashes. These are Acer estimates, not a scoring-box measurement. The Mac's later native-architecture correctness run matched the official full SHA and three additional sample seeds (see `REPORT.md` in the parent folder). A later attempt to shrink the arena to 2 MiB was reversed in the final source because it provided negligible practical gain and less holdout headroom.

With more time, measure the official i5-13500 scoring system (12 threads rather than Acer's 8), tune consumer queue cadence there, and explore faster owner-local key comparison without expanding the live working set. Never substitute a cross-machine timing for the contest score.
