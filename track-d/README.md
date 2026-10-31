# Track D: Traverse

Dependency-free C++20 exact directed shortest paths for Speed Demon 2026.
Reads `/data/graph.txt` and `/data/queries.txt`; writes exactly
`/out/distances.txt`. No third-party runtime dependencies or precomputed
knowledge of the private scoring graph.

## Method

The 92 MB input is read-only memory-mapped. Twelve newline-aligned parsing
threads turn edge lines into packed 64-bit triples, with full-width overflow
weights stored separately. Forward and reverse CSR are built in separate
degree-count and scatter passes to reduce cache contention. Each adjacency
edge takes five bytes: a 21-bit target and 19-bit weight/index field.
Weights too large for the 18-bit inline value are held in a separate table.
CSR start offsets and degree are packed into one 32-bit word per vertex.
Every adjacency array has one padding entry for safe eight-byte unaligned
edge loads.

Eight search workers fetch query indices from an atomic counter. Each runs
bidirectional Dijkstra with two binary heaps, alternating the expanded side.
The termination condition is the sum of the two current frontier minima
being at least the best complete path found. A stale heap entry is ignored.
Alternating rather than choosing the smallest key avoids a worst-case
near-full reverse search when a source's first edge has weight 2^32-1.

Per-direction tentative distances live in 65,536-slot open-addressed hash
tables rather than dense 1.74M-vertex arrays. The normal value occupies
16 bits, with larger distances spilled into a lazily touched 64-bit array.
Each entry stores an epoch tag; tags are cleared after epoch 2,047.
Distinct duplicate queries are answered once, then copied into the
required original order. All arithmetic for path lengths uses 64 bits.

The fixed sparse capacity was chosen from measured full and extra-seed
workloads. It is not a formal upper bound for arbitrary directed graphs:
a query discovering 65,536 distinct vertices on one side could exhaust a
table. A dynamic-resize variant was tested and passed official hashes,
but reduced measured K, so the benchmarked faster variant was retained.
That is a residual holdout-correctness risk, not a proven guarantee.

## Build and verification

The Dockerfile uses pinned allowlisted GCC 15 and Debian 13 images. Its
offline, two-stage build uses `-O3 -march=x86-64-v3 -pthread` and static
libstdc++/libgcc. To compile on the Linux benchmark host:

```sh
g++ -std=c++20 -O3 -march=x86-64-v3 -pthread -DNDEBUG -static-libstdc++ -static-libgcc -o traverse traverse.cpp
./traverse /path/to/data /path/to/existing/output_directory
```

`SD_THREADS=N` selects the query-worker count; `SD_PROFILE=1` prints
separate parse and search durations to stderr.

Official Acer Linux checks matched the Python reference's SHA-256 on
seed-1 sample and full, including after the final optimisations.
Additional independent sample seeds 9–18 also matched fresh references;
earlier round-1 random-graph differential testing passed 100 small graphs.
The final eight-worker code ran in approximately 0.94 s and used 117 MB
cgroup peak memory in the official five-run Acer check. These are Acer
numbers, not scoring-box measurements.

See PERF.md for kept/reverted optimisations, timings, and their provenance.
With more time, profile the official i5-13500 scoring CPU, build the
offline Docker image, and develop a fast zero-regression sparse-table growth
fallback for unexpectedly large search regions.
