# Track B: Index

`main.cpp` is a standalone C++20 solution with no external dependencies. It reads the queries first, memory-maps `docs.txt`, builds only the index needed by those queries, and writes `results.txt`. Optional positional arguments, or `DATA_DIR` and `OUT_DIR`, override the contest paths for development.

Corpus tokenization runs in parallel. AVX2 classifies 32 bytes at a time; recognized tokens pass through a small positive cache, a compact Bloom rejector, and an open-addressed query-term table. One packed metadata word says whether a term needs a Boolean posting, a dense bitmap, or a phrase-anchor position. Frequent Boolean terms use document bitmaps. Sparse terms are appended as 16-bit ids during the hot scan, then each worker deduplicates its stream by document in a short post-scan compaction pass. Moving that random per-term bookkeeping out of the tokenizer was the main Round 5 speedup. Phrase queries retain positions for one selective anchor and verify surrounding tokens directly against the mapped corpus.

Boolean expressions are evaluated strictly left-to-right, including mixed sparse/bitmap operations. Phrase verification follows the specified byte tokenizer exactly: ASCII letters and digits form tokens, ASCII letters compare case-insensitively, and all other bytes are separators. Results preserve query order and report count, id sum, and the ten smallest matching ids.

## Build and validation

The pinned two-stage Dockerfile uses the allowlisted GCC 15 build image and Debian 13 slim runtime. It compiles with `-O3 -DNDEBUG -std=gnu++20 -march=x86-64-v3 -pthread -flto`, statically links the GCC runtimes, and has zero runtime dependencies. `.speed-demon.toml` names team `Gdnsk`.

Round 5 was validated repeatedly with the official project Acer checker. Every retained change passed byte-exact SHA-256 checks for seed-1 sample, seed-1 full, and an independently generated full-size seed 2 before its five timed runs. `PERF.md` records each measured change, including regressions that were reverted, and the final `REPORT.md` contains the verbatim final checker output.

## With another 24 hours

The remaining dominant cost is exact query-term recognition in the corpus scan. Several larger direct tables, batching/prefetch schemes, wider positive-cache entries, and short-token special cases were measured and lost. The next promising direction would be a smaller exact recognition scheme that removes random dictionary probes without increasing the hot working set; source reduction is also valuable if it can preserve the specialized query paths.
