# Track B: optimization and validation log

**Hardware distinction:** All timings here are **MEASURED locally on the ARM64 macOS host with Apple clang**, not on Acer or the official Intel scoring machine. Do not present these as leaderboard results. Local reference: Python 3.14, 38.27 s wall, 2,270,904,320 bytes maximum RSS on full seed 1 (one `/usr/bin/time -l` run). The official scoring-box reference time is 56.35 s, a separate published number.

The versions below were rebuilt with `g++` (Apple clang), `-O3 -DNDEBUG -std=c++20 -pthread` and SDK 26.5, each verified against the exact reference full-seed-1 SHA-256. Version trial timings are three runs per version, except the final which has five runs; timings are noisy at the subsecond scale. Peak RSS for intermediate versions is an independent `/usr/bin/time -l` run, not a five-run maximum.

| Step | Change | Mac median wall (s) | Mac peak RSS (MB, decimal) | Outcome |
|---|---|---:|---:|---|
| Reference | Python reference, full seed 1 | 38.27 (one run) | 2270.9 | Reference hash |
| v1 | Initial query-aware postings and parallel scan | 0.564 | 745.4 | Correct, retained as baseline |
| v5 | Packed postings and more selective phrase work | 0.282 | 410.4 | Correct, faster and smaller |
| v7 | Compact 16-bit per-thread sparse-term stream | 0.271 | 387.1 | Correct, smaller |
| v9 | Faster token comparison / hashing experiments | 0.293 | 383.0 | Correct; slower in this local sample |
| v10 | Dense boolean bitsets and phrase anchor offsets | 0.283 | 382.9 | Correct; further reduced memory |
| final | Fixed partial refactor, 32-bit position offsets, 32-bit occurrence counts, byte-exact result checks | **0.264** | **375.7** | Correct on all four datasets |

Final five consecutive full-seed-1 process wall times, measured by `bench.py`: **0.265093, 0.301344, 0.261592, 0.257563, 0.263721 s**. **Median 0.263721 s**, peak `ru_maxrss` **366,864 KiB** (Darwin returns bytes; `bench.py` converts). The comparable sample/full/extra-seed SHA checks were all made after the final encoding change.

Official reference byte hashes and locally reproduced submission hashes:

| Dataset | SHA-256 of `results.txt` | Match |
|---|---|---|
| seed 1 sample | `eeeec1e9319da98a6f3c664c78f93e7078fd603c25c2bb3027174e81e3638a8f` | Yes |
| seed 1 full | `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` | Yes |
| seed 7 full | `a5a568281ee327b54011c43ac63e971b7a344948f58be60ee57ff07ef50f9c0a` | Yes |
| seed 7 sample | `7f41992e4446b5b88cf7a46ca6bae971f5bafb7984208164c3be3d6dda5c4139` | Yes |
| seed 11 sample | `762883fd099a7a153931756ce3ec58050c21a411800575f8e83f807edfcc6d0e` | Yes |

The fallback code path, forced on seed-1 sample, independently reproduced the reference sample hash. The final source was compiled and rechecked with the normal path after the encoding change, including the second full-sized corpus on seed 7. Compilation with GCC 15 in Docker, the required Acer `flock`/five-run benchmark, cgroup `memory.peak`, and exact `scc` count remain **unmeasured** under the no-SSH restriction. An LLVM-style `clang-format` local estimate is approximately 980 counted code lines including the Dockerfile; this is not an official `scc` result.

Using Mac-local reference time/RSS, the five-run final median, **estimated** S=980, D=1, and the contest formula gives **K≈12.09 as a cross-environment-invalid proxy**, **not** an Acer/scoring-box score. Mac RSS also differs from Linux cgroup `memory.peak` for memory-mapped `/data`, so the official score cannot be derived from these figures.

Board run ID: not available (the lead handles submission). No SSH, server modification, account access, or publication was performed.

## Round 2: official Acer loop

The Round 2 worker is authorized to use the contest Acer host. All Round 2 timing and memory numbers below come from the lead's official serialized checker (`check1.sh`), which compiles with the contest-style flags, verifies the seed-1 sample and full SHA-256 outputs, times five full runs, and reads cgroup `memory.peak`.

| Step | Change | Acer median wall (s) | memory.peak (KB) | S~ | K_acer~ | Outcome |
|---|---|---:|---:|---:|---:|---|
| r2-baseline | Unmodified Round 1 source | 0.717 (0.699, 0.717, 0.717, 0.735, 0.754) | 98,292 | 358 | 13.2 | Sample/full SHA OK; retained as baseline |
| r2-1 | AVX2 token-boundary classification in 32-byte blocks during corpus scan | 0.673 (0.652, 0.665, 0.673, 0.686, 0.699) | 99,060 | 365 | 13.5 | Sample/full SHA OK; **kept** (6.1% lower median) |
| r2-2 | Parallelize sparse-posting merge across build threads | 0.651 (0.645, 0.651, 0.651, 0.662, 0.665) | 110,208 | 365 | 13.5 | Sample/full SHA OK; **reverted**: only 3.3% faster than r2-1 but +11,148 KB peak, no K gain |
| r2-3 | Double query-term hash table from 2^17 to 2^18 slots | 0.728 (0.718, 0.726, 0.728, 0.742, 0.754) | 100,304 | 365 | 13.0 | Sample/full SHA OK; **reverted**: 8.2% slower and +1,244 KB peak vs r2-1 |
| r2-4 | 1,048,576-bit query-term Bloom rejector before hash-table probing | 0.662 (0.659, 0.661, 0.662, 0.668, 0.673) | 100,560 | 367 | 13.6 | Sample/full SHA OK; **kept**: lower median and higher K despite small memory cost |
| r2-5 | Shrink Bloom rejector to 524,288 bits | 0.677 (0.651, 0.663, 0.677, 0.682, 0.690) | 99,500 | 367 | 13.5 | Sample/full SHA OK; **reverted**: lower memory but slower and lower K than r2-4 |
| r2-6 | Cap workers at 4 on Acer's 4C/8T CPU | 0.834 (0.808, 0.817, 0.834, 0.834, 0.835) | 98,332 | 367 | 12.2 | Sample/full SHA OK; **reverted**: memory saved, but wall time +26% and K fell sharply |
| r2-7 | Store only term IDs in the open-addressing table; read the fingerprint from the compact term array instead of duplicating it in every slot | 0.663 (0.640, 0.658, 0.663, 0.755, 0.822) | 89,160 | 366 | 13.9 | Sample/full SHA OK; **kept**: essentially flat median but -11,400 KB peak and higher K |
| r2-8 | Double the Bloom rejector again, from 1,048,576 to 2,097,152 bits | 0.738 (0.693, 0.736, 0.738, 0.739, 0.755) | 99,012 | 366 | 12.9 | Sample/full SHA OK; **reverted**: larger filter hurt cache locality and both time and memory |
| r2-9 | Direct base-36 lookup table for every 1-3 byte query token, bypassing hashing for short/common words | 0.802 (0.759, 0.771, 0.802, 1.081, 1.098) | 98,552 | 369 | 12.4 | Sample/full SHA OK; **reverted**: direct-key encoding/cache traffic was much slower than the compact hash path |
| r2-10 | Use a 16-bit per-thread document stamp for sparse-posting deduplication and reuse count arrays as merge cursors | 0.843 first check; 0.953 confirm | 95,996 / 97,432 | 369 | 12.1 / 11.4 | Sample/full SHA OK twice; **reverted**: both serialized checks were materially slower and memory was not lower |
| r2-11 | `#pragma GCC optimize("Ofast,unroll-loops")` | 0.971 (0.714, 0.715, 0.971, 1.004, 1.007) | 94,692 | 367 | 11.4 | Sample/full SHA OK; **reverted**: an immediate restored-baseline check was 0.979, so the pragma gave no credible speed gain and cost one source line |
| r2-12 | Per-scan-thread 1,024-entry positive term cache, with full-byte confirmation on every cache hit | 0.719 (0.703, 0.710, 0.719, 0.957, 1.005) | 97,032 | 366 | 13.1 | Sample/full SHA OK; **kept**: under the same period of Acer contention, the immediately preceding restored baseline was 0.979 (0.961-1.006); caching repeated Zipf-heavy terms removed most repeated dictionary probes |
| r2-13 | Enlarge the positive term cache to 2,048 entries | 1.009 (0.762, 0.767, 1.009, 1.021, 1.049) | 98,828 | 366 | 11.1 | Sample/full SHA OK; **reverted**: larger L1 footprint was slower than 1,024 entries |
| r2-14 | Shrink the positive term cache to 512 entries | 1.025 (1.022, 1.025, 1.025, 1.032, 1.034) | 97,544 | 366 | 11.0 | Sample/full SHA OK; **reverted**: collision/eviction rate erased the cache benefit |
| r2-15 | Cache an exact folded 64-bit key for <=8-byte positive-cache hits, avoiding the term-string read | 1.020 (0.720, 0.741, 1.020, 1.034, 1.058) | 99,112 | 368 | 11.0 | Sample/full SHA OK; **reverted**: adjacent same-code confirmation was 1.075 and restored r2-12 was 1.025, so no credible gain and extra memory/source |
| r2-16 | Fuse all BOOL and PHRASE evaluation into the corpus scan, storing only per-thread result accumulators | 5.186 (5.137, 5.141, 5.186, 5.210, 5.259) | 36,928 | 409 | 5.7 | Sample/full SHA OK; **reverted**: memory was excellent, but BOOL seed terms generated 209,094,387 candidate query evaluations (PHRASE only 1,249,186), making it ~7x slower |
| r2-17 | Two-bit Bloom rejection at the same 128 KiB size before open-addressing probes | 0.720 (0.699, 0.709, 0.720, 0.978, 1.020) | 95,204 | 366 | 13.2 | Sample/full SHA OK; **kept**: immediate one-bit A/B was 0.998 (0.711, 0.801, 0.998, 1.002, 1.032), 95,296 KB, K 11.2 |
| r2-18 | Evaluate only PHRASE queries inline during the corpus scan while retaining the Boolean index | 0.718 (0.680, 0.716, 0.718, 0.722, 0.960) | 107,740 | 379 | 12.8 | Sample/full SHA OK; **reverted**: time was flat and per-thread phrase accumulators raised peak memory/source enough to lower K |
| r2-19 | Two-way set-associative 1,024-entry positive term cache | 0.734 (0.681, 0.718, 0.734, 1.017, 1.021) | 98,800 | 366 | 13.0 | Sample/full SHA OK; **reverted**: hit rate rose from 39.5% to 42.0%, but extra second-probe/byte-compare work did not beat the r2-17 13.2 K check |
| r2-20 | Process two AVX2 token-classification vectors as one 64-byte mask before falling back to the existing 32-byte loop | 0.820 (0.694, 0.696, 0.820, 0.898, 0.992) | 99,736 | 368 | 12.2 | Sample/full SHA OK; **reverted**: extra 64-bit run-extraction branches/source did not beat the 32-byte loop |
| r2-21 | Tiny 1,024-slot L1 hot-term table for the 432 dense Boolean terms, probed before the general dictionary/cache | 0.745 (0.706, 0.721, 0.745, 0.913, 1.042) | 99,184 | 368 | 12.8 | Sample/full SHA OK; **reverted**: the mandatory extra hot-table probe on every token outweighed cheaper lookups for common terms |
| r2-22 | Store per-document sparse-posting counts as 16-bit values instead of 32-bit cumulative ends | 0.700 (0.684, 0.699, 0.700, 0.701, 0.873) | 99,880 | 367 | 13.2 | Sample/full SHA OK; **reverted**: wall improved slightly but measured peak memory/source did not, leaving K tied with r2-17 rather than improving it |
| r2-23 | Force 12 scan/query workers instead of using Acer's reported 8 hardware threads | 0.733 (0.725, 0.731, 0.733, 0.734, 0.747) | 102,872 | 366 | 12.9 | Sample/full SHA OK; **reverted**: oversubscription was slower and raised memory |
| r2-24 | Merge sparse postings with two worker threads after the scan | 0.763 (0.671, 0.673, 0.763, 0.887, 0.966) | 100,400 | 366 | 12.7 | Sample/full SHA OK; **reverted**: even two-way merge parallelism did not improve the measured K over the serial merge |
| r2-25 | Tighten temporary pair/position/end vector reservations using the measured full-data occupancy | 0.825 (0.796, 0.816, 0.825, 0.830, 0.856) | 98,600 | 366 | 12.2 | Sample/full SHA OK; **reverted**: slightly lower allocation headroom caused enough extra allocation/copy work to hurt wall time materially |
| r2-26 | Sweep the dense-bitmap cutoff upward from `uses > 20` | >23: 0.692 / 100,220 KB / K 13.3; >24: 0.695 / 99,808 KB / K 13.3; >25: 0.696 / 101,156 KB / K 13.2; >22: **0.673 / 99,540 KB / K 13.5**; >21: 1.111 / 98,968 KB / K 10.5 under a later contention spike | 99,540 kept | 366 | 13.5 | Sample/full SHA OK at every cutoff; **kept `uses > 22`** (382 dense terms), the best measured K and a cleaner time/memory balance than the original 432 dense terms |

Final serialized check of the retained source: `0.681 s` median (`0.661 0.666 0.681 0.692 0.710`), `99,580 KB` memory.peak, `S~366`, `K_acer~13.4`, with both official sample and full SHA checks OK. Relative to the unmodified Round 1 Acer baseline, median wall time improved from `0.717 s` to `0.681 s` (5.0%) and estimated K improved from `13.2` to `13.4`; the checker-reported peak moved from `98,292 KB` to `99,580 KB`.

The Acer became substantially busier during r2-10 onward (the same r2-7 source later re-measured at 0.979 s versus 0.663 s earlier; host load was 2.70 with concurrent rsyncs). Absolute later timings therefore include host contention. Decisions in this section use adjacent A/B checks where possible; the final number will come from a fresh official check.

The fused-query experiment was useful despite being rejected: phrase matching directly in the scan produced only 1.25 million candidate verifications and may still be viable as a phrase-only optimization. The failure was entirely the boolean side, where document-local evaluation expanded to 209 million candidate queries.

After the third retained Round 2 change, extra sample seeds were regenerated and compared against the unchanged official Python reference on Acer. Seed 7 matched `7f41992e4446b5b88cf7a46ca6bae971f5bafb7984208164c3be3d6dda5c4139`; seed 11 matched `762883fd099a7a153931756ce3ec58050c21a411800575f8e83f807edfcc6d0e`. Both complete `results.txt` files were byte-identical.

After the next three retained changes (positive cache, two-bit Bloom rejection, and the final dense cutoff), the same seed-7 and seed-11 samples were re-run with the final source. They again matched the reference hashes above byte-for-byte.

Profiling the baseline on Acer (instrumented copy, not the submission) showed the dominant full-run phase clearly: query parse 9-12 ms, setup 4-7 ms, index build 605-625 ms, query wall 86-88 ms, output 5-6 ms. Inside index build, corpus scan/tokenize/index insertion was 527-544 ms, phrase-position layout/merge 10-12 ms, and sparse-posting layout/merge 68-74 ms. Summed worker CPU time during the query wall was roughly 456-463 ms for BOOL and 224-228 ms for PHRASE. This made corpus scan the first optimization target.
