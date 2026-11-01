# Track D: Traverse

Dependency-free C++20 implementation of exact directed shortest-path queries for Speed Demon 2026. It reads `/data/graph.txt` and `/data/queries.txt` and writes only `/out/distances.txt`, preserving input query order. The executable accepts alternate data/output directories as positional arguments for testing.

## Approach

Twelve newline-aligned workers parse the read-only mmap of the ASCII graph into compact 64-bit `(source, target, weight)` records. Rare weights above the inline limit retain their full 32-bit values in side arrays. CSR degree counting is done in separate sequential passes to avoid concurrent random degree updates. The **reverse CSR is built first**, then the packed parse buffers are released. The forward CSR is reconstructed exactly from the reverse edges, avoiding the peak-memory cost of holding both final CSR arrays alongside the temporary parse records.

Each final edge is one 32-bit integer: a 21-bit destination and an 11-bit weight field. Inline weights are 0–1023; field value 1024 is a sentinel, with full weights of 1024 or greater kept in sorted `(CSR-index, weight)` exception arrays. Zero-weight edges, parallel edges, self-loops, and paths exceeding 32 bits remain exact. Node ranges combine a 23-bit start and small degree in one 32-bit word. The graph layout requires no extra runtime libraries or precomputed input-specific index.

Eight query workers take unique queries from an atomic counter. Each uses bidirectional Dijkstra with two compact binary heaps, expanding whichever side has fewer heap entries and stopping only when the sum of frontier minima proves the current complete route optimal. Per-worker tentative distances use epoch-tagged 65,536-entry sparse tables, 16-bit common distances and full 64-bit overflow values; a lazy dense fallback handles any query that fills the sparse table. Fast checks handle identical nodes and vertices with no outgoing/incoming arcs; repeated query pairs reuse the result. Each worker writes into its assigned original output slot, and a final sequential pass produces the exact text file.

An undirected and directed BFS locality renumbering were implemented and checked on two full input seeds, but both lost end-to-end Acer performance to their preprocessing cost. A remapping-aware multiplicative hash significantly improved their search phase, but not enough to justify keeping relabeling in the final design. Unidirectional Dijkstra was also materially slower on the test distribution. All tested variants are logged in `PERF.md`.

## Build and testing

The two-stage Dockerfile pins allowlisted GCC 15 and Debian 13 images, uses `-O3 -march=x86-64-v3 -pthread` with statically linked C++ runtimes, and adds no external runtime dependency.

```sh
g++ -std=c++20 -O3 -march=x86-64-v3 -pthread -DNDEBUG -static-libstdc++ -static-libgcc -o traverse traverse.cpp
./traverse /path/to/data /path/to/output-directory
```

`SD_THREADS=N` overrides the query-worker count. `SD_PROFILE=1` prints parse, CSR and search phase durations on standard error using monotonic timing. `PERF.md` records exact SHA checks against seed-1 sample, seed-1 full and seed-2 full and the Acer five-run medians; these are measured on a test Intel laptop, not an official contest score.

With 24 more hours: benchmark explicit per-phase timing on the official i5-13500 12-thread CPU, explore parallel CSR scatter with per-thread disjoint node partitions, and evaluate whether any small compact landmark potential repays its indexing cost. Any further change must pass the full SHA-256 gate before replacing this design.
