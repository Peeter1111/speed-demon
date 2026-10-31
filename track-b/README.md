# Track B: Index

`main.cpp` is a standalone C++20 implementation with no external dependencies. It reads `queries.txt` before indexing, memory-maps `docs.txt`, and writes the required `results.txt`. The two optional positional arguments override the data and output directories; `DATA_DIR` and `OUT_DIR` are also accepted.

The index is query-driven. Query tokens are interned in a compact open-addressing table; corpus tokens are classified 32 bytes at a time with AVX2, then filtered through a small positive lookup cache and a two-bit Bloom rejector before the general dictionary. Only Boolean terms and one selected anchor per phrase are retained. Boolean terms used more than 22 times by the query workload use dense document bitmaps; the rest use sorted postings assembled from 16-bit per-thread term streams. Phrase anchors store byte offsets and are verified against the mapped document text only for candidate occurrences.

Boolean expressions are evaluated strictly left-to-right with union, intersection, and difference, including dense/sparse combinations. Phrase matching uses the exact byte tokenizer from the spec: ASCII `[A-Za-z0-9]` runs are tokens, ASCII letters compare case-insensitively, and every other byte is a separator. Repeated phrase occurrences count a document only once. Results preserve original query order and contain count, document-id sum, and the ten smallest matching ids.

## Build and validation

The pinned two-stage Dockerfile uses the allowlisted GCC 15 image and Debian 13 slim runtime. It compiles with `-O3 -DNDEBUG -std=gnu++20 -march=x86-64-v3 -pthread -flto`, links the GCC runtimes statically, and requires no build-time network access. The lead must replace `team = "TEAM"` in `.speed-demon.toml` before submission.

Round 2 was measured with the lead's serialized Acer checker after every meaningful change. The final source passes the official seed-1 sample and full SHA checks. Two additional generated sample datasets also match the official Python reference byte-for-byte: seed 7 is `7f41992e4446b5b88cf7a46ca6bae971f5bafb7984208164c3be3d6dda5c4139`, and seed 11 is `762883fd099a7a153931756ce3ec58050c21a411800575f8e83f807edfcc6d0e`. `PERF.md` records the baseline, profiling breakdown, every kept/reverted experiment, and Acer measurements.

## With another 24 hours

The dominant remaining cost is corpus token lookup during the parallel scan. The most promising next step is a substantially different exact term-recognition structure rather than more posting/query micro-tuning; attempts to fuse query evaluation into the scan cut memory sharply but made Boolean evaluation far slower. A second direction is compact sparse-document storage that saves the roughly 44 MB 32-bit posting pool without adding enough decode work to erase the memory-score gain.
