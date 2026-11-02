# Track D: Traverse

traverse.cpp is a dependency-free C++20 implementation of exact directed shortest-path queries. It reads graph.txt and queries.txt, preserves query order, and writes distances.txt. Optional positional arguments select alternate input and output directories.

## Graph representation

For a full graph, twelve newline-aligned parsing threads convert the ASCII edges into compact 64-bit temporary records. Weights too large for the temporary packed field are kept in side vectors. Degree counts are accumulated into forward and reverse prefix arrays.

The reverse CSR is built first. After reverse scatter, the temporary parsed edge buffers and the reverse scatter cursor are released. Only then is the forward scatter cursor allocated and the forward CSR reconstructed from the reverse CSR. This staged cursor lifetime is a Round 6 memory reduction.

Each final edge is one 32-bit word with a 21-bit destination and an 11-bit weight field. Weights 0 through 2046 are inline. The value 2047 is a sentinel for a full 32-bit weight stored in a sorted side vector keyed by CSR edge index. Node ranges pack a 23-bit start offset and degree into one 32-bit word.

## Search

Eight query threads run bidirectional Dijkstra by default. Each search expands the side with the smaller heap and stops when the two frontier minima prove that the best complete route cannot improve.

The common heap representation is one sortable 64-bit word containing a 21-bit node id and a 43-bit distance. If any valid heap distance reaches 2^43, that heap promotes itself to the full-width representation and remains exact. Per-side tentative distances use epoch-tagged 65,536-entry sparse tables with a 16-bit common-distance value and full 64-bit overflow storage. A dense per-node fallback is allocated lazily if a sparse table fills. Epoch tables are reset before the 11-bit epoch counter wraps.

Repeated query pairs are still detected by the simple exact earlier-query scan. The separate hash-dedup experiment was slightly faster on the test laptop but had the same reported K and more source, so it is not part of the retained source. SD_THREADS=N overrides query-thread count and SD_PROFILE=1 prints phase timings.

## Build and validation

The pinned two-stage Dockerfile builds with GCC 15 using -std=c++20 -O3 -march=x86-64-v3 -pthread -DNDEBUG, statically links the GCC runtimes, and has no external runtime dependency.

The Round 6 test-laptop check passed sample, full seed 1, and independent full seed 2 SHA gates. The retained cursor-big2047 source measured 0.897 s median, 80,632 KB memory.peak, official S 562, and K about 10.0. The same check measured the Round 5 source at 1.074 s, 87,600 KB, S 512, and K about 9.1. Full measurements, rejected variants, and independent local checks are in PERF.md.
