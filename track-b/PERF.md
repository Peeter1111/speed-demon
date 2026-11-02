# Track B: optimization and validation log

**Hardware distinction:** All timings here are **MEASURED locally on the ARM64 local OS host with Apple clang**, not on the test laptop or the official Intel scoring machine. Do not present these as leaderboard results. Local reference: Python 3.14, 38.27 s wall, 2,270,904,320 bytes maximum RSS on full seed 1 (one `/usr/bin/time -l` run). The official scoring-box reference time is 56.35 s, a separate published number.

The versions below were rebuilt with `g++` (Apple clang), `-O3 -DNDEBUG -std=c++20 -pthread` and SDK 26.5, each verified against the exact reference full-seed-1 SHA-256. Version trial timings are three runs per version, except the final which has five runs; timings are noisy at the subsecond scale. Peak RSS for intermediate versions is an independent `/usr/bin/time -l` run, not a five-run maximum.

| Step | Change | local ARM host median wall (s) | local ARM host peak RSS (MB, decimal) | Outcome |
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

The fallback code path, forced on seed-1 sample, independently reproduced the reference sample hash. The final source was compiled and rechecked with the normal path after the encoding change, including the second full-sized corpus on seed 7. Compilation with GCC 15 in Docker, the required serialized test-laptop five-run benchmark, cgroup `memory.peak`, and exact `scc` count remain **unmeasured** because that benchmark was not run. An LLVM-style `clang-format` local estimate is approximately 980 counted code lines including the Dockerfile; this is not an official `scc` result.

Using local-host reference time/RSS, the five-run final median, **estimated** S=980, D=1, and the contest formula gives **K≈12.09 as a cross-environment-invalid proxy**, **not** a test-laptop/scoring-box score. Local ARM host RSS also differs from Linux cgroup `memory.peak` for memory-mapped `/data`, so the official score cannot be derived from these figures.

Board run ID: not available. No board submission was performed in that round.

## Round 2: official test-laptop loop

Round 2 ran on the test laptop. All Round 2 timing and memory numbers below come from the supplied serialized checker (`check1.sh`), which compiles with the contest-style flags, verifies the seed-1 sample and full SHA-256 outputs, times five full runs, and reads cgroup `memory.peak`.

| Step | Change | test-laptop median wall (s) | memory.peak (KB) | S~ | K_test~ | Outcome |
|---|---|---:|---:|---:|---:|---|
| r2-baseline | Unmodified Round 1 source | 0.717 (0.699, 0.717, 0.717, 0.735, 0.754) | 98,292 | 358 | 13.2 | Sample/full SHA OK; retained as baseline |
| r2-1 | AVX2 token-boundary classification in 32-byte blocks during corpus scan | 0.673 (0.652, 0.665, 0.673, 0.686, 0.699) | 99,060 | 365 | 13.5 | Sample/full SHA OK; **kept** (6.1% lower median) |
| r2-2 | Parallelize sparse-posting merge across build threads | 0.651 (0.645, 0.651, 0.651, 0.662, 0.665) | 110,208 | 365 | 13.5 | Sample/full SHA OK; **reverted**: only 3.3% faster than r2-1 but +11,148 KB peak, no K gain |
| r2-3 | Double query-term hash table from 2^17 to 2^18 slots | 0.728 (0.718, 0.726, 0.728, 0.742, 0.754) | 100,304 | 365 | 13.0 | Sample/full SHA OK; **reverted**: 8.2% slower and +1,244 KB peak vs r2-1 |
| r2-4 | 1,048,576-bit query-term Bloom rejector before hash-table probing | 0.662 (0.659, 0.661, 0.662, 0.668, 0.673) | 100,560 | 367 | 13.6 | Sample/full SHA OK; **kept**: lower median and higher K despite small memory cost |
| r2-5 | Shrink Bloom rejector to 524,288 bits | 0.677 (0.651, 0.663, 0.677, 0.682, 0.690) | 99,500 | 367 | 13.5 | Sample/full SHA OK; **reverted**: lower memory but slower and lower K than r2-4 |
| r2-6 | Cap threads at 4 on the test laptop's 4C/8T CPU | 0.834 (0.808, 0.817, 0.834, 0.834, 0.835) | 98,332 | 367 | 12.2 | Sample/full SHA OK; **reverted**: memory saved, but wall time +26% and K fell sharply |
| r2-7 | Store only term IDs in the open-addressing table; read the fingerprint from the compact term array instead of duplicating it in every slot | 0.663 (0.640, 0.658, 0.663, 0.755, 0.822) | 89,160 | 366 | 13.9 | Sample/full SHA OK; **kept**: essentially flat median but -11,400 KB peak and higher K |
| r2-8 | Double the Bloom rejector again, from 1,048,576 to 2,097,152 bits | 0.738 (0.693, 0.736, 0.738, 0.739, 0.755) | 99,012 | 366 | 12.9 | Sample/full SHA OK; **reverted**: larger filter hurt cache locality and both time and memory |
| r2-9 | Direct base-36 lookup table for every 1-3 byte query token, bypassing hashing for short/common words | 0.802 (0.759, 0.771, 0.802, 1.081, 1.098) | 98,552 | 369 | 12.4 | Sample/full SHA OK; **reverted**: direct-key encoding/cache traffic was much slower than the compact hash path |
| r2-10 | Use a 16-bit per-thread document stamp for sparse-posting deduplication and reuse count arrays as merge cursors | 0.843 first check; 0.953 confirm | 95,996 / 97,432 | 369 | 12.1 / 11.4 | Sample/full SHA OK twice; **reverted**: both serialized checks were materially slower and memory was not lower |
| r2-11 | `#pragma GCC optimize("Ofast,unroll-loops")` | 0.971 (0.714, 0.715, 0.971, 1.004, 1.007) | 94,692 | 367 | 11.4 | Sample/full SHA OK; **reverted**: an immediate restored-baseline check was 0.979, so the pragma gave no credible speed gain and cost one source line |
| r2-12 | Per-scan-thread 1,024-entry positive term cache, with full-byte confirmation on every cache hit | 0.719 (0.703, 0.710, 0.719, 0.957, 1.005) | 97,032 | 366 | 13.1 | Sample/full SHA OK; **kept**: under the same period of the test laptop contention, the immediately preceding restored baseline was 0.979 (0.961-1.006); caching repeated Zipf-heavy terms removed most repeated dictionary probes |
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
| r2-23 | Force 12 scan/query threads instead of using the test laptop's reported 8 hardware threads | 0.733 (0.725, 0.731, 0.733, 0.734, 0.747) | 102,872 | 366 | 12.9 | Sample/full SHA OK; **reverted**: oversubscription was slower and raised memory |
| r2-24 | Merge sparse postings with two thread threads after the scan | 0.763 (0.671, 0.673, 0.763, 0.887, 0.966) | 100,400 | 366 | 12.7 | Sample/full SHA OK; **reverted**: even two-way merge parallelism did not improve the measured K over the serial merge |
| r2-25 | Tighten temporary pair/position/end vector reservations using the measured full-data occupancy | 0.825 (0.796, 0.816, 0.825, 0.830, 0.856) | 98,600 | 366 | 12.2 | Sample/full SHA OK; **reverted**: slightly lower allocation headroom caused enough extra allocation/copy work to hurt wall time materially |
| r2-26 | Sweep the dense-bitmap cutoff upward from `uses > 20` | >23: 0.692 / 100,220 KB / K 13.3; >24: 0.695 / 99,808 KB / K 13.3; >25: 0.696 / 101,156 KB / K 13.2; >22: **0.673 / 99,540 KB / K 13.5**; >21: 1.111 / 98,968 KB / K 10.5 under a later contention spike | 99,540 kept | 366 | 13.5 | Sample/full SHA OK at every cutoff; **kept `uses > 22`** (382 dense terms), the best measured K and a cleaner time/memory balance than the original 432 dense terms |

Final serialized check of the retained source: `0.681 s` median (`0.661 0.666 0.681 0.692 0.710`), `99,580 KB` memory.peak, `S~366`, `K_test~13.4`, with both official sample and full SHA checks OK. Relative to the unmodified Round 1 test-laptop baseline, median wall time improved from `0.717 s` to `0.681 s` (5.0%) and estimated K improved from `13.2` to `13.4`; the checker-reported peak moved from `98,292 KB` to `99,580 KB`.

The test laptop became substantially busier during r2-10 onward (the same r2-7 source later re-measured at 0.979 s versus 0.663 s earlier; host load was 2.70 with concurrent rsyncs). Absolute later timings therefore include host contention. Decisions in this section use adjacent A/B checks where possible; the final number will come from a fresh official check.

The fused-query experiment was useful despite being rejected: phrase matching directly in the scan produced only 1.25 million candidate verifications and may still be viable as a phrase-only optimization. The failure was entirely the boolean side, where document-local evaluation expanded to 209 million candidate queries.

After the third retained Round 2 change, extra sample seeds were regenerated and compared against the unchanged official Python reference on the test laptop. Seed 7 matched `7f41992e4446b5b88cf7a46ca6bae971f5bafb7984208164c3be3d6dda5c4139`; seed 11 matched `762883fd099a7a153931756ce3ec58050c21a411800575f8e83f807edfcc6d0e`. Both complete `results.txt` files were byte-identical.

After the next three retained changes (positive cache, two-bit Bloom rejection, and the final dense cutoff), the same seed-7 and seed-11 samples were re-run with the final source. They again matched the reference hashes above byte-for-byte.

Profiling the baseline on the test laptop (instrumented copy, not the submission) showed the dominant full-run phase clearly: query parse 9-12 ms, setup 4-7 ms, index build 605-625 ms, query wall 86-88 ms, output 5-6 ms. Inside index build, corpus scan/tokenize/index insertion was 527-544 ms, phrase-position layout/merge 10-12 ms, and sparse-posting layout/merge 68-74 ms. Summed thread CPU time during the query wall was roughly 456-463 ms for BOOL and 224-228 ms for PHRASE. This made corpus scan the first optimization target.

## Round 3: independent local-only experiment log

**Evidence limitation.** The benchmark record reported a fresher Round 2 test-laptop check of **0.694 s**, **99.4 MB** `memory.peak`, **S≈359**, **K_test≈13.4** (reported in the earlier benchmark record, not remeasured here). Round 3 was **not** sent to the test laptop or run through `check1.sh`: that benchmark was unavailable in this run, even though the contest job brief otherwise expects a test-laptop check. All Round 3 timings below are **MEASURED on the local ARM64 host using Apple clang, a scalar fallback for the AVX2 scan, and process wall time**. Neither absolute time, memory nor derived K is comparable to the official Linux/x86 numbers. Time varied with concurrent local ARM host load, so changes were compared in interleaved adjacent sequences when possible. All tested variants below compiled and passed the five existing SHA datasets unless otherwise stated.

| Experiment | Measured local full-seed-1 median (s), first five runs | Outcome |
|---|---:|---|
| R3 baseline, unmodified Round 2 with scalar build fallback | 0.31669 | Baseline; five existing seed checks OK |
| R3-1, rehash only terms actually indexed (prune 6,515 of 75,747 terms in seed 1) | 0.31074 | **Reverted**: interleaved seed-1 medians 0.29176 vs baseline 0.29200 (tie), but seed-2 full 0.31087 vs baseline 0.29073; extra code did not justify itself |
| R3-2, remove two-bit Bloom rejector | 0.33191 | **Reverted**: interleaved seed-1 median 0.30023 vs pruned 0.29176 |
| R3-3, separate exact 1-to-8-byte token key table | 0.41272 | **Reverted**: large random table traffic overwhelmed eliminated byte comparisons |
| R3-4, pack posting/phrase/dense actions into one 32-bit scan metadata lookup | 0.30012 | **Kept**: interleaved seed-1 0.29506 vs pruned 0.30697; seed-2 full 0.28104 vs Round 2 baseline 0.29073; positive but small/noisy |
| R3-5, four-slot hash buckets with overflow fallback | 0.31551 | **Reverted**: full seed 7 0.41344 vs the metadata variant 0.30606 in initial checks; more code and memory |
| R3-6, remove dictionary pruning, retain compact metadata | 0.28561 | **Kept**: 12-cycle interleaved seed 7 0.29724 vs baseline 0.30233; seed 1 0.28153 vs baseline 0.27406 (mixed). Less source than pruned version |
| R3-7, specialize to published size bounds; drop unused slow phrase fallback and temporary arrays | 0.30537 | **Final**: preserves all six SHA checks, saves 13 nonblank noncomment C++ lines vs Round 2, and reduces temporary scan-array lifetime; no demonstrated large time gain |

Final R3-7 *interleaved* local measurements, 12 full executions per variant and seed, with alternating order (seconds): **seed 1 baseline 0.298937 vs R3-7 0.300058; seed 7 baseline 0.286273 vs R3-7 0.281454; seed 2 baseline 0.280486 vs R3-7 0.285020**. These medians do **not** demonstrate a material 2x scan improvement; one seed improved modestly and two were slightly slower. Accepting the final variant is based on the simpler smaller source and reducing scattered scan metadata loads, **pending** the official check. On seed 2 full, local ARM host `/usr/bin/time -l` independently reported maximum RSS **372,654,080 bytes** for the baseline vs **372,621,312 bytes** for R3-7, effectively unchanged and not the Linux cgroup metric. Approximate source count: baseline **359** nonblank, noncomment C++ lines vs final **346**, plus seven nonblank Dockerfile lines; **scc** was unavailable.

### Round 3 correctness

The final scalar-fallback binary was compiled on local OS and output hashes checked against official reference output for all five existing datasets:

| Dataset | Expected and obtained SHA-256 | Result |
|---|---|---|
| seed 1 sample | `eeeec1e9319da98a6f3c664c78f93e7078fd603c25c2bb3027174e81e3638a8f` | PASS |
| seed 1 full | `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` | PASS |
| seed 7 sample | `7f41992e4446b5b88cf7a46ca6bae971f5bafb7984208164c3be3d6dda5c4139` | PASS |
| seed 7 full | `a5a568281ee327b54011c43ac63e971b7a344948f58be60ee57ff07ef50f9c0a` | PASS |
| seed 11 sample | `762883fd099a7a153931756ce3ec58050c21a411800575f8e83f807edfcc6d0e` | PASS |
| **seed 2 full** (newly generated) | `7f224ef65795253d56911fa8f25553a8fd0a2cef4addc7bd411e40e8da990869` | PASS vs independently run official Python reference |

Full seed 2 was generated from the unmodified official generator with `--seed 2 --scale full`; the generator printed 250,000 docs, 251,351,689 corpus bytes, 50,000 queries and 609,816 distinct tokens. The Python official reference was run separately, and the final R3-7 file was byte-identical. The final source also **compiled** for `x86_64 -march=x86-64-v3` using Apple clang to check the AVX2 branch, but that is not a Linux GCC 15 build or a performance run. No official test-laptop `memory.peak`, serialized five-run median or K score for Round 3 exists; do not promote this variant solely on these local ARM host measurements.

## Round 4: the test laptop measurement and optimization log

All Round 4 measurements use the supplied check script (`check1.sh b`) unless explicitly labelled as a one-run phase profile. The checker validates seed-1 sample, seed-1 full and seed-2 full before timing five full runs.

| Step | Change | the test laptop result | Outcome |
|---|---|---|---|
| r4-baseline | Unmodified Round 3 | **0.698 s** median (0.671, 0.686, 0.698, 0.702, 0.721), **88,376 KB**, S~353, K~13.7 | All three SHA checks OK; baseline |
| r4-profile | Temporary clock_gettime profiling passes | 1.05 s-class because profiling adds two extra corpus scans | Correct; instrumentation only, removed before final |
| r4-filter-1 | Replace the 128 KiB two-word/two-bit Bloom check with one 1 MiB single-bit bitmap | Profile filter 0.319 s, scan+write 0.512 s; checker memory ~100 MB | **Reverted:** larger random footprint was slower and larger |
| r4-filter-2 | Keep 128 KiB Bloom storage but place both rejection bits in one fetched 64-bit word | Profile filter about 0.228-0.232 s, scan+write about 0.408-0.413 s in clean-ish one-run samples | **Retained for clean check:** one random Bloom load instead of two |
| r4-filter-3 | Add a 2 MiB direct hash-to-term table before Bloom/hash-table fallback | Profile filter 0.249 s, scan+write 0.502 s; checker peak ~93 MB | **Reverted:** larger cache footprint lost |
| r4-filter-4 | CRC32 fingerprint for >=8-byte tokens | Profile filter 0.269 s, scan+write 0.442 s | **Reverted:** slower than the simple first/last-word fingerprint |
| r4-filter-5 | Rebuild the scan hash table to omit query terms not used as Boolean postings or phrase anchors | Profile filter 0.260 s, scan+write 0.430 s | **Reverted:** no measured gain; matches Round 3's earlier mixed result |

### Round 4 phase split before optimization

One full-data the test laptop run of the temporary instrumented baseline, with exactly 8 scan threads, measured: **pure tokenizer 0.081501 s; tokenizer + query-term filter 0.271817 s; tokenizer + filter + posting/position writes 0.436365 s; sparse/position merge 0.086911 s; whole build 0.528816 s; parallel query evaluation 0.090856 s; output 0.005797 s**. The filter adds roughly 0.190 s over tokenization and scan writes add roughly another 0.165 s, so those two scan components dominate. The full dataset had **75,747 query terms, 382 dense Boolean terms, 53,062 sparse Boolean terms, 11,236,807 sparse postings, 1,185,623 phrase-anchor offsets and 11,939,792 bytes of dense bitmaps**. The final sparse posting array alone is about 44.9 MB; its temporary 16-bit scan streams are about 22.5 MB before capacity overhead, explaining much of the build-time memory peak.

The one-word bserialized Bloom profile was re-run after adjacent reversions: tokenizer **0.065536 s**, filter **0.231506 s**, scan+write **0.412965 s**, merge **0.080062 s**, build **0.498749 s**, query evaluation **0.090871 s**, output **0.006014 s**. Host load is visibly noisy, so the retained/reverted decision must come from the clean five-run checker after profiling code is removed, not from these single phase runs alone.

## Round 5: filter/write experiments on the official test-laptop checker

Scoring-box result of the retained round-5 code (commit b9ecadc): `run-c2460ab05e9c`, T **0.3808 s**, M **98.7 MB**, S 986, K **15.83** (round 3 on the box: `run-46508aa44dab`, 0.4409 s, 106.5 MB, S 970, K 14.53). The box gain (-14% T) is close to the test laptop gain against the round-5 starting point (-15%).

Every row below is a complete `check1.sh b` run: seed-1 sample, seed-1 full and seed-2 full hashes all matched before the five timed full runs. Per the Round 5 rule, a change is retained only when the checker-reported `K_test` rises.

| Step | Change | the test laptop result | Outcome |
|---|---|---|---|
| r5-baseline | Unmodified Round 4 retained one-word/two-bit Bloom source | **0.598 s** median (0.578, 0.589, 0.598, 0.599, 0.605), **97,296 KB**, S 963, K 12.5 | Baseline; all three SHA checks OK |
| r5-hash8 | Hash >=8-byte tokens from masked first/last 8-byte loads instead of first/last 4-byte loads | 0.617 s, 97,696 KB, S 971, K 12.2 | **Reverted:** slower, larger source, lower K |
| r5-restored | Exact baseline after reverting hash8 | 0.596 s, 96,876 KB, S 963, K 12.5 | Confirms the revert |
| r5-batch16 | Batch 16 token hashes, test Bloom words together, prefetch initial hash-table slots, then resolve in order | 0.650 s, 97,216 KB, S 1000, K 11.9 | **Reverted:** batching/prefetch overhead outweighed hidden cache latency |
| r5-parallel-scatter | Reuse thread threads to scatter each thread's sparse postings into its disjoint final ranges in parallel | 0.614 s, 107,316 KB, S 967, K 12.0 | **Reverted:** thread-stack/page peak rose about 10 MB and K fell |
| r5-u32-seen | Pack each scan thread's local-document stamp and posting count into one 32-bit word (16 bits each), then reuse that word as the merge cursor | **0.589 s**, **95,112 KB**, S 964, **K 12.6** | **Kept:** lower wall time and ~2.2 MB lower peak; full scale has only ~31k docs per the test laptop scan slice and ~21k on the 12-thread scoring box, so both fields fit |
| r5-cache-sig | Pack the positive-cache entry as hash signature + term id, mirroring the dictionary table, so cache collisions are rejected without a random `Term` metadata read | **0.583 s**, **94,996 KB**, S 965, **K 12.7** | **Kept:** faster and higher K with no extra cache storage |
| r5-packed-term | Remove the now-unused `Term.sig` field and pack `Term` to 12 bytes | 0.582 s, 94,776 KB, S 965, K 12.7 | **Reverted:** tiny memory/time movement but no printed K increase, so it fails the strict keep rule |
| r5-deferred-sparse-dedup | During corpus tokenization append sparse term ids sequentially without touching their random per-term stamp/count state; after each thread finishes its slice, compact that stream in place by document and build counts in a separate pass. Dense terms still deduplicate inline in a tiny 16-bit stamp array. | **0.510 s** median (0.479, 0.507, 0.510, 0.510, 0.510), **94,368 KB**, S 985, **K 13.5** | **Kept:** moved the sparse random-access work out of the tokenizer/filter loop and raised K substantially despite 20 extra source lines |
| r5-parallel-zero | Delay allocation/zeroing of each sparse count table until its scan thread starts the post-scan compaction pass, so the ~1.7 MB initialization is parallel rather than serialized before scanning | **0.479 s** median (0.475, 0.478, 0.479, 0.499, 0.511), **94,992 KB**, S 986, **K 13.9** | **Kept:** materially lower median and higher K |
| r5-lower-or | Since callers pass only token bytes, replace `lower_ascii`'s uppercase range test with `c | 32` | 0.482 s, 95,092 KB, S 984, K 13.9 | **Reverted:** hashes remained correct but K did not rise |
| r5-compact-term | Replace each 16-byte `Term` pointer/length/signature record with a query-file-relative offset and 16-bit length | 0.499 s (0.483, 0.498, 0.499, 0.502, 0.502), 94,664 KB, S 989, K 13.7 | **Reverted:** metadata shrank, but K fell |
| r5-short-table | Direct base-36 lookup table for one- and two-byte query terms, bypassing hashing/Bloom for those tokens | 0.486 s (0.463, 0.475, 0.486, 0.498, 0.500), 94,596 KB, S 1003, K 13.8 | **Reverted:** no K improvement and source grew |
| r5-list-template | Merge intermediate sparse set operations and final sparse summarization into one templated `list_op` implementation | 0.502 s (0.496, 0.500, 0.502, 0.509, 0.517), 95,144 KB, S 919, K 13.8 | **Reverted:** excellent source reduction, but the generic path slowed enough to lower K |
| r5-set-op | Keep final summarization specialized, but merge the three intermediate sparse set helpers into one branched function | 0.509 s (0.476, 0.502, 0.509, 0.510, 0.514), 95,164 KB, S 978, K 13.5 | **Reverted:** shorter source did not compensate for slower queries |
| r5-cache-2048 | Double the direct-mapped positive cache from 1,024 to 2,048 entries | 0.489 s (0.480, 0.481, 0.489, 0.512, 0.522), 95,196 KB, S 986, K 13.8 | **Reverted:** larger hot footprint did not improve K |
| r5-cache-offset | Use a 64-bit positive-cache entry containing signature, query-file offset, length and term id so cache hits avoid loading `Term` metadata | 0.533 s (0.498, 0.504, 0.533, 0.534, 0.561), 95,008 KB, S 996, K 13.2 | **Reverted:** larger cache entries lost badly |
| r5-raw-pairs | Replace the growing sparse pair vector with a preallocated raw 16-bit array sized from the slice byte upper bound | 0.554 s (0.550, 0.552, 0.554, 0.555, 0.557), 100,576 KB, S 986, K 12.8 | **Reverted:** over-allocation raised peak memory and slowed the run |
| r5-singleton-bucket | Add a direct table that marks each original hash bucket empty/singleton/colliding, resolving or rejecting singleton buckets before Bloom/probing | 0.528 s (0.508, 0.527, 0.528, 0.529, 0.538), 95,612 KB, S 998, K 13.2 | **Reverted:** extra table traffic and memory outweighed saved probes |

The retained Round 5 profile after deferred sparse deduplication showed the new split clearly: thread scan about **0.310 s**, post-scan sparse compaction only **0.0045 s**, phrase-position layout about **0.010 s**, final sparse scatter about **0.060 s**, query evaluation about **0.088 s**, and output about **0.005 s** in one instrumented the test laptop run. A separate counter-only profile saw **36,276,657 tokens**, with **14,324,246 positive-cache hits**, **14,392,451 dictionary hits after the cache**, and **7,559,960 true misses**. Counter instrumentation perturbed timing, so only the lookup counts, not that run's wall time, are used here.

Final clean Round 5 checker run after all reversions and documentation/config cleanup: **0.509 s** median (0.506, 0.509, 0.509, 0.512, 0.515), **94,648 KB** `memory.peak`, **S 986**, **K_test 13.5**; seed-1 sample, seed-1 full and seed-2 full SHA checks all passed. The initial Round 5 baseline was 0.598 s / 97,296 KB / S 963 / K 12.5, so the final retained source is about 15% faster, uses about 2.6 MB less measured peak memory, and raises the checker estimate by 1.0 K despite the source-size increase. The best adjacent retained measurement was 0.479 s / K 13.9; repeated checks show enough the test laptop load noise that the final clean run is the number used for delivery.
## Round 6

Round 6 combines the exact one-to-four-byte short-token table with partition-local 16-bit sparse posting chunks. The short-token table removes general dictionary work for common tiny terms. The chunk layout removes the global 32-bit sparse posting array and releases each partition's temporary sparse stream after that partition is materialized. The goal was to cut the sparse-index peak without giving back scan speed.

The official Round 6 check on the test laptop used the same three SHA gates for every row. Memory is the minimum of three memory.peak readings from that check.

| Source | Full T median | Five timed runs | memory.peak KB | Official S | K | Result |
|---|---:|---|---:|---:|---:|---|
| Round 5 | 0.509 s | 0.507, 0.509, 0.509, 0.510, 0.511 | 94,824 | 1025 | 13.4 | all three SHA gates passed |
| short-exact only | 0.652 s | 0.589, 0.639, 0.652, 0.688, 0.698 | 94,888 | 1063 | 11.8 | correct but rejected |
| chunks-short | **0.483 s** | 0.479, 0.480, 0.483, 0.504, 0.513 | **60,712** | 1094 | **14.9** | retained |

The Round 6 check supersedes older Round 5 measurements when comparing these candidates.

### Rejected Round 6 experiments

The development-host screens below were used only to decide what deserved the official check. They are not test-laptop scores.

| Experiment | Measured result | Decision |
|---|---|---|
| packed16-shrink | 0.25895 s versus 0.25639 s for packed16; about 353 MB versus 351 MB RSS | rejected; shrinking caused a transient allocation and did not improve time |
| packed16 cache, threshold 8 | 0.276 s / 366.7 MB versus 0.259 s / 350.8 MB for packed16 | rejected; retained decoded copies cost more than repeated decoding |
| packed16 iterator | 0.27057 s / 346.6 MB versus 0.26493 s / 350.6 MB; query phase 0.04390 s versus 0.04132 s | rejected; less memory but slower query work and more source |
| packed16 two scratch buffers | 0.26128 s / 348.3 MB versus 0.25749 s / 350.8 MB | rejected; about 2.5 MB saved did not repay the time loss |
| packed24 | 0.27406 s / 360.4 MB versus packed16 0.26198 s / 350.4 MB and Round 5 0.25801 s / 367.9 MB | rejected; worse than packed16 on both time and memory |
| larger dictionary tables | 2x 0.25922 s / 368.42 MB, 4x 0.25627 s / 369.39 MB, baseline 0.25873 s / 367.93 MB | rejected; no reliable speed gain and larger tables |
| short6 | 0.26809 s versus 0.26318 s for short-exact and 0.27291 s baseline | rejected; extending the direct table to six-byte terms was slower |
| two-batch and three-batch chunk builds | 0.26735 s / 331.9 MB and 0.27506 s / 340.0 MB versus chunks-short 0.25837 s / 330.9 MB | rejected; extra partition metadata and retained phrase streams raised both cost and peak |
| phrase-anchor prefix sampling | 1% 0.25717 s / 328.2 MB, 2% 0.25752 s / 327.7 MB, 5% 0.26035 s / 327.4 MB versus chunks-short 0.25554 s / 330.5 MB | rejected; 2 to 3 MB saved did not justify the extra pass and source |
| chunks-short with two scratch buffers | 0.25970 s / 327.9 MB versus 0.25561 s / 330.9 MB | rejected; small memory win, clear local time loss |

Dense-bitmap cutoff sweeps from greater-than-12 through greater-than-100 uses also failed to improve both axes. The original greater-than-22 cutoff measured 0.2569 s / 367.7 MB locally; alternatives ranged from 0.2580 to 0.2812 s and 367.7 to 420.2 MB.

Board run: run-9473297e242e (commit 21366f8)


## Round 7: phrases verified during the document scan (final code)

The code in this repository is round 7 without -funroll-loops. It verifies phrases during the document scan and keeps no separate positional index. It writes query and document hit pairs, then sorts them and removes duplicates.

| Board run | Commit | Build | T s | M bytes | S | K |
|---|---|---|---|---|---|---|
| run-9473297e242e | 21366f8 | round 6 | 0.372985 | 64,495,616 | 1055 | 17.24 |
| run-45a3608e0519 | 3c1843d | round 7 with -funroll-loops | 0.445845 | 58,347,520 | 1044 | 16.11 |
| run-233e83bd4d08 | 5f6611a | round 7 without the flag | 0.354023 | 58,863,616 | 1044 | 18.05 |

The flag run read 26% slower than the same source without the flag. Each row is one sample, and identical code on the board varied by 8 to 9%. The local test laptop compiled with fixed flags, so it never measured the flag. Only the board did.
