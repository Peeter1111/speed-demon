# Track B: Index

`main.cpp` is a standalone C++20 implementation with no external dependencies. It reads `queries.txt` before indexing, memory-maps `docs.txt`, and writes `results.txt`. Two optional positional arguments override the data and output directories; `DATA_DIR` and `OUT_DIR` are also accepted.

The index is query-driven. Query tokens are interned in a compact open-addressing table; corpus tokens are classified 32 bytes at a time with AVX2, then filtered through a small positive lookup cache and a two-bit Bloom rejector before the general dictionary. Only Boolean terms and one selected anchor per phrase are retained. Boolean terms used more than 22 times by the query workload use dense document bitmaps; the rest use sorted postings assembled from 16-bit per-thread term streams. Phrase anchors store byte offsets and are verified against the mapped document text only for candidate occurrences.

Round 3 packs the scan actions for each query term into one 32-bit metadata word: whether to record a phrase offset, whether to add a document posting, whether that posting is dense, and the corresponding dense or sparse index. This avoids separate scattered flag and identifier loads for every recognized token, and releases temporary scan-setup arrays before corpus processing. The implementation specializes for the specified full-scale limits (less than 4 GiB of corpus bytes, fewer than 262,144 documents and query terms) and rejects input that exceeds those bounds instead of carrying an unused slower phrase-index fallback. A scalar tokenizer is included only for development on non-AVX2 hosts; the contest Docker build selects AVX2.

Boolean expressions are evaluated strictly left-to-right with union, intersection, and difference, including dense/sparse combinations. Phrase matching uses the exact byte tokenizer from the spec: ASCII `[A-Za-z0-9]` runs are tokens, ASCII letters compare case-insensitively, and every other byte is a separator. Repeated phrase occurrences count a document only once. Results preserve original query order and contain count, document-id sum, and the ten smallest matching ids.

## Build and validation

The pinned two-stage Dockerfile uses the allowlisted GCC 15 image and Debian 13 slim runtime. It compiles with `-O3 -DNDEBUG -std=gnu++20 -march=x86-64-v3 -pthread -flto`, links the GCC runtimes statically, and requires no build-time network access. The lead must replace `team = "TEAM"` in `.speed-demon.toml` before submission.

Round 2 was measured with the lead's serialized Acer checker. Round 3 has not been retested on Acer because this worker cannot use SSH. Its Mac ARM64 fallback passed SHA comparisons for seed 1 sample/full, seed 7 sample/full, seed 11 sample, and a newly generated seed 2 **full** corpus. The new seed-2 reference and candidate both hash to `7f224ef65795253d56911fa8f25553a8fd0a2cef4addc7bd411e40e8da990869`. Native x86-64 AVX2 compilation was also checked with Apple clang, but it does not substitute for the required GCC/Linux check. `PERF.md` distinguishes reported Acer Round 2 results from measured Mac Round 3 results.

## With another 24 hours

The dominant remaining cost is corpus token lookup during the parallel scan. A genuinely faster exact term-recognition structure (rather than a second large lookup table) is still the priority. The tested four-way bucket lookup and short-token keyed table both lost time. Another possible direction is compact sparse-document storage that saves part of the 32-bit posting pool without adding excessive decode work. The next essential validation is a serialized GCC 15/Linux Acer check including seed 2 full, cgroup `memory.peak`, and source-size measurement before deciding whether to submit Round 3 over Round 2.
