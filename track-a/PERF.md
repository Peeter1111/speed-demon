# Track A performance log

## Round 5: record compression and report improvements

Scoring-box result of the retained round-5 code (commit a2d6a0b): `run-3d39ac096bc7`, T **0.03924 s**, M **29.9 MB**, S 298, K **33.4** (round 4 on the box: `run-c40bb50695d3`, 0.04029 s, 36.7 MB, S 283, K 31.9). The 6.8 MB memory cut outweighs the 15 extra counted lines.

Run-to-run noise on the box: commit e663e59 changed only this file, and its re-run of the same binary (`run-4e6b253a814e`) measured T 0.04270 s, M 31.0 MB, K 31.8. That is 9% slower and 1.0 MB more than `run-3d39ac096bc7`. So a single box run cannot separate changes smaller than about 10% in T; the Acer median of five runs is what decided every keep or revert above.

Each row in this round uses the lead's check script on the 8-thread Intel i5-1035G1 test laptop ("Acer") for exact seed-1 sample, full and seed-2 full SHA gates; times are five-run Acer full medians and KB are cgroup `memory.peak`. The board's official 12-thread i5-13500 figures are different and are not silently substituted for Acer results.

| Step | Change | Acer median | Peak KB | Official S | Decision |
|---|---|---:|---:|---:|---|
| R5-B0 | Unchanged lead round-four design | **0.085 s** (0.081, 0.084, 0.085, 0.086, 0.087) | **31,548** | **283** | Three SHA gates OK; baseline K_acer ~20.1 |
| R5-K1 | 4-byte records: 11-bit endpoint id, 5xx flag, 19-bit common latency plus overflow index; exact u32 values in per-worker overflow arrays | **0.071 s** (0.057, 0.065, 0.071, 0.076, 0.083) | **28,248** | **298** | Three SHA gates OK; K_acer ~22.3. **Keep.** Saves 3,300 KB and 14 ms versus R5-B0. |
| R5-K2 | Shrink record blocks from 1,024 to 512 | **0.060 s** (0.057, 0.058, 0.060, 0.063, 0.067) | **28,020** | **298** | Three SHA gates OK; K_acer ~24.3. **Keep.** Peak falls 228 KB and median improves 11 ms. |
| R5-R1 | Shrink record blocks again, 512 to 256 | **0.067 s** (0.061, 0.065, 0.067, 0.072, 0.075) | **29,476** | **298** | Three SHA gates OK; K_acer ~22.7. **Revert.** Too many allocations and more peak memory under the real allocator. |
| R5-R2 | Two-decimal-digit lookup table to format numbers | **0.070 s** (0.067, 0.070, 0.070, 0.074, 0.076) | **29,040** | **318** | Three SHA gates OK; K_acer ~22.1. **Revert** to smaller scalar digit loop; lookup table raised both S and wall time. |
| R5-R3 | Cap parse/report pool to 4 threads (affinity still 8 available) | **0.073 s** (0.069, 0.073, 0.073, 0.077, 0.082) | **25,852** | **298** | Three SHA gates OK; K_acer ~22.3; slower than 8-thread K2 despite 2.2 MB saved. **Revert.** |
| R5-R4 | Cap parse/report pool to 6 threads | **0.068 s** (0.063, 0.066, 0.068, 0.069, 0.071) | **27,636** | **298** | Three SHA gates OK; K_acer ~22.8. **Revert**, still below 8-thread K2. |
| R5-R5 | `madvise(MADV_SEQUENTIAL)` on mmap before reading | **0.080 s** (0.058, 0.064, 0.080, 0.081, 0.093) | **28,048** | **299** | Three SHA gates OK; K_acer ~21.0. **Revert**: no stable gain and extra scored line. |
| R5-R6 | Lower per-group `std::sort` threshold from 32 to 8, use more `std::nth_element` calls | **0.064 s** (0.063, 0.064, 0.064, 0.072, 0.089) | **29,236** | **298** | Three SHA gates OK; K_acer ~23.3. **Revert**: threshold 32 remains best. |
| R5-R7 | Intermediate record block of 768 instead of 512 or 1024 | **0.067 s** (0.061, 0.065, 0.067, 0.073, 0.075) | **28,104** | **298** | Three SHA gates OK; K_acer ~22.9. **Revert**: 512 remains best, fewer bytes and lower median. |
| R5-R8 | Split mmap into 4 instead of 8 chunks per thread | **0.063 s** (0.054, 0.054, 0.063, 0.074, 0.074) | **28,140** | **298** | Three SHA gates OK; K_acer ~23.6. **Revert**: no reproducible improvement over R5-K2. |
| R5-R9 | `std::to_chars` integer formatting in place of scalar reversed-digit loop | **0.077 s** (0.058, 0.070, 0.077, 0.079, 0.086) | **27,852** | **291** | Three SHA gates OK; K_acer ~21.5. **Revert**: scored lines and KB lower, wall overhead too high. |
| R5-R10 | SWAR 64-bit newline detection for common latency digit widths, bounded scalar fallback for 8-10 digit / mmap tail | **0.073 s** (0.071, 0.073, 0.073, 0.075, 0.079) | **28,692** | **309** | Three SHA gates OK; K_acer ~21.8. **Revert**: additional branch/bit work cost more than it saved versus scalar loop. |

The final retained changes relative to the lead's round-four baseline are **K1 four-byte packed latency records and K2 512-record hour blocks**. All other probes were reverted. The original pool size (affinity CPU count), output formatting, hour-by-hour counting and percentile selection remain. Acer variance across five runs is nontrivial; decisions compare the whole five-run median and K rather than the fastest individual time.

| Final repeat | Acer median | Peak KB | Official S | Correctness | Observation |
|---|---:|---:|---:|---|---|
| K1+K2 unchanged, after reverting all experiments | **0.083 s** (0.070, 0.079, 0.083, 0.084, 0.087) | 28,340 | 298 | all three SHA OK | Under contention; K_acer ~20.5, little above baseline. |
| Same code, final confirming check | **0.069 s** (0.057, 0.061, 0.069, 0.076, 0.077) | **28,388** | **298** | **all three SHA OK** | Final K_acer ~22.5; paired medians highlight noise rather than a source difference. |

Verbatim final Acer check:

```text
sample sha OK a-sample/summary.txt
full sha OK a-full/summary.txt
full2 (seed 2) sha OK a-full2/summary.txt
full T median 0.069 s (runs 0.057 0.061 0.069 0.076 0.077)  memory.peak 28388 KB  S 298 (official count)  K_acer~22.5
```

Baseline checker output verbatim:

```text
sample sha OK a-sample/summary.txt
full sha OK a-full/summary.txt
full2 (seed 2) sha OK a-full2/summary.txt
full T median 0.085 s (runs 0.081 0.084 0.085 0.086 0.087)  memory.peak 31548 KB  S 283 (official count)  K_acer~20.1
```

## Round 4: single-pass rewrite (lead)

Round 3 scanned the file twice (count, then scatter) and its board run was `run-5636c2e5635f`: T 0.05034 s,
M 31.5 MB, S 407, K 27.8. The round-3 R8 experiment showed that a naive one-pass version with one shared record
array was slower (0.109 s), because every record crossed threads twice. The rewrite keeps one pass but stores
records per thread and per hour, so the second phase reads only cache-sized hour slices.

| Step | Change | Acer check (8 threads; sample, full and full seed 2 SHA all OK) | Board |
|---|---|---|---|
| A4 | One parse per line into 6-byte records in per-thread, per-hour block chains; barrier merge of endpoint tables into one byte-sorted list; one task per hour does a counting sort by endpoint rank, nearest-rank selection and formatting; ordered streaming write | median **0.084 s**, peak 33,400 KB, K~20.8 | |
| A4 recheck | Same code, check now counts S the official way (clang-format LLVM, then scc 4.1.0) | median **0.081 s** (0.078, 0.081, 0.081, 0.082, 0.090), peak 33,400 KB, S 276, K~20.4 | `run-c40bb50695d3`: T **0.04029 s**, M 36.7 MB, S 283, K **31.9** (+4.1 K, +3.0 points over round 3) |

Phase profile of A4 on the Acer, six runs: parse 31-56 ms, merge 1.3-2.5 ms, report plus write 14.6-27.9 ms.
A plain parallel byte sum over the same mapping takes 18.7 ms at 4 threads, so parse is 1.7-3x that floor and
the report phase is the other half of the time.

Measurement note: round 3's own S estimates were taken before formatting and were far too low (B: 353 written,
970 counted). The official counter formats each file with `clang-format --style=LLVM` (80 columns) first, so
long lines are split and dense code does not save lines.

## Round 3 — official Acer checks (sample, full, and independent full seed 2)

All figures use the lead's unchanged `flock ... check1.sh a <src>` command; the full2 SHA check is now mandatory. Host load produces large timing variation (notably 0.075–0.130 s inside five-run checks), so changes are not claimed on one cherry-picked run. The source began as round 2 K12, with its full log retained below.

| R3 step | Single change | Official check output, condensed | Decision |
|---|---|---|---|
| B0 | Unchanged round-2 K12 on the new 3-seed checker | sample/full/full2 SHA OK; median **0.121 s** (0.086, 0.103, 0.121, 0.124, 0.130), peak **25,004 KB**, S~320, K~**17.3** | baseline |
| K1 | Combine per-hour percentile selection and output formatting on workers; concurrently write completed hours in strict order, bound queued hours to twice worker count | all 3 SHA OK; median **0.102 s** (0.075, 0.098, 0.102, 0.102, 0.107), peak **29,048 KB**, S~320, K~**18.3** | **keep provisionally**: 19 ms saved, +4 MB peak, K +1.0; repeated check below |
| R1 | Cap pending formatted hours to one worker count (instead of two) | all 3 SHA OK; median **0.108 s** (0.076, 0.091, 0.108, 0.118, 0.120), peak **28,460 KB**, S~320, K~**17.9** | **revert**: lower peak did not offset slower result |
| K1 repeat | Restored 2×worker cap, no other changes | all 3 SHA OK; median **0.111 s** (0.109, 0.110, 0.111, 0.112, 0.115), peak **28,716 KB**, S~320, K~**17.6** | retained, but K gain over B0 is noise-sensitive |
| K2 | In first parsing pass, use one 16-byte SSE newline mask and trailing-zero index instead of a byte-at-a-time latency skip; terminal tail retains scalar fallback | all 3 SHA OK; median **0.091 s** (0.077, 0.088, 0.091, 0.100, 0.106), peak **29,220 KB**, S~326, K~**19.3** | **keep**; 20 ms lower than the immediately preceding K1 repeat |
| R2 | Decode pass-2 latencies with SIMD newline-length discovery and SWAR eight-digit decimal folding, including 9/10-digit fallbacks | all 3 SHA OK; median **0.103 s** (0.087, 0.091, 0.103, 0.103, 0.109), peak **29,176 KB**, S~349, K~**18.0** | **revert**, +23 lines and slower |
| K2 repeat | Restore scalar pass-2 decimal parser; keep pass-1 SSE | all 3 SHA OK; median **0.095 s** (0.079, 0.090, 0.095, 0.101, 0.105), peak **29,312 KB**, S~326, K~**18.9** | **retained**; stable speed gain despite load variation |
| R3 | In pass 2 find newline with SSE and run a counted decimal-conversion loop | all 3 SHA OK; median **0.106 s** (0.092, 0.106, 0.106, 0.107, 0.108), peak **29,220 KB**, S~335, K~**17.8** | **revert**: extra SIMD work costs more than it saves |
| R4 | Reuse parser threads across both passes with a C++20 barrier instead of joining/spawning twice | all 3 SHA OK; median **0.116 s** (0.103, 0.108, 0.116, 0.118, 0.128), peak **29,324 KB**, S~328, K~**17.0**; repeated median **0.112 s** (0.091, 0.108, 0.112, 0.117, 0.122), peak **28,808 KB**, K~17.4 | **revert**: the two barrier cycles under contention are slower than recreation |
| R5 | Special-case 1-, 2- and 3-record groups with fixed compare/swap networks | all 3 SHA OK; median **0.099 s** (0.087, 0.090, 0.099, 0.100, 0.103), peak **28,884 KB**, S~341, K~**18.4** | **revert**: code-size cost outweighs noisy time |
| R6 | Shrink shared endpoint hash slots from 8,192 to 4,096 | all 3 SHA OK; median **0.106 s** (0.087, 0.103, 0.106, 0.109, 0.112), peak **29,248 KB**, S~326, K~**17.9** | **revert**: probing costs more |
| R7 | Grow shared endpoint hash slots from 8,192 to 16,384 | all 3 SHA OK; median **0.103 s** (0.082, 0.084, 0.103, 0.112, 0.114), peak **29,368 KB**, S~326, K~**18.1** | **revert**: larger table is not faster |
| R8 | Replace two file scans with one parse into 8-byte (group, latency) request records, then scatter from the in-memory records | all 3 SHA OK; median **0.109 s** (0.107, 0.107, 0.109, 0.110, 0.111), peak **42,508 KB**, S~316, K~**16.4** | **revert**: extra writes/memory cost more than rescanning mmap; source saved under `../variants/onepass.cpp` |
| FINAL | Restored retained K2; refreshed README and reran the checker | all 3 SHA OK; median **0.101 s** (0.088, 0.093, 0.101, 0.102, 0.103), peak **29,152 KB**, S~326, K~**18.3** | **final**, not the fastest of several noisy checks |

Independent generator/oracle checks on Acer, in this worker's own directory, also passed after K2 was restored: sample seed 7 was byte-identical to the official Python reference at SHA-256 `39bfccc5334c93cbe0c762f5e6c3300e91ca2e266567561da3176c038cee49f7`; sample seed 11 at `a81124d584b7a8e438b143a08fd045b7799c2ced3343a80825c9c479b9fd5c66`. The checker also passed independent full-sized seed 2 for *every* candidate.

The final saved source is `main.cpp` after R8 was reverted, identical to `../variants/k2.cpp`. The checker provides an **as-written** source estimate (S~326), not official post-clang-format/scc S. The static-tools Docker image is unavailable on these hosts, so exact official S is not claimed.

### Eight-thread mmap floor and phase profile

The `../floor.cpp` standalone 20-line input-byte summation test maps the same 238.8 MB full input and sums every unsigned byte across N threads, checksum **17,368,420,572** on every run. It uses no endpoint parsing, hashing, sorting or formatting. Five measurements on Acer under the benchmark lock:

| Threads | mmap (roughly) | Scan+sum seconds (five runs) |
|---|---:|---|
| 1 | 0.005–0.009 ms | 0.0978, 0.0878, 0.0908, 0.0904, 0.0903 |
| 2 | 0.004–0.005 ms | 0.0320, 0.0321, 0.0318, 0.0319, 0.0318 |
| 4 | 0.004–0.008 ms | 0.0170, 0.0172, 0.0187, 0.0219, 0.0304 |
| 8 | 0.008–0.009 ms | 0.0247, 0.0267, 0.0282, 0.0269, 0.0277 |

4 threads reach about 5× the single-thread throughput; 8 threads lose to 4 here, suggesting the floor is dominated by cache/memory traffic and SMT scheduling rather than a mutex. The real two-pass Track A benefits from 8 threads because it does substantially more CPU work per byte.

Temporary phase-instrumented *unchanged round-2* `../profile.cpp` (outside the submission; all runs on the same Acer full file) gave these representative times in ms:

| Threads | map/open | first scan+parse | merge + prefix | second scan+scatter | percentile | serial format | write | total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.03 | 139–150 | 0.75–0.84 | 105–130 | 63–64 | 25–28 | 4.8–5.6 | 342–370 |
| 2 | 0.03 | 76–79 | 0.77–0.79 | 53–54 | 23.8–23.9 | 18–21 | 3.3–4.0 | 177–179 |
| 4 | 0.02–0.03 | 31–50 | 1.1–1.8 | 30–48 | 12.6–17.8 | 18–19 | 3.3–3.6 | 95–140 |
| 8 | 0.02–0.03 | 22–38 | 1.8–3.2 | 29–35 | 9.4–14.0 | 18–28 | 3.3–5.5 | 84–125 |

At 8 threads the two input scans together consume roughly 51–73 ms, with a 25–28 ms byte-sum floor for *one* scan. The separate serial output formatting costs another 18–28 ms, so K1 parallelizes formatting alongside percentile selection while writing in sorted hour order. No segment is falsely called `mmap` time: actual memory-map/open startup is negligible; mapping fault and reading costs accrue to scan time.

## Round 2 — official Acer measurements

All round-2 timings below come from the lead's official `check1.sh` command under `flock`, unless a row is explicitly labelled as phase instrumentation. Every kept/reverted optimization is judged against the exact sample and full SHA checks printed by that command.

| R2 step | Change | Official Acer result | Decision |
|---|---|---:|---|
| B0 | Unmodified round-1 source copied into this worker folder | sample SHA OK; full SHA OK; median **0.127 s** (0.111, 0.126, 0.127, 0.127, 0.129); memory.peak **50,048 KB**; S~324 as written; K_acer~**14.7** | round-2 baseline |
| R2-R1 | 1,024-entry direct endpoint cache ahead of the CRC table, keyed from endpoint head/tail/length | sample/full SHA OK; median **0.132 s** (0.086, 0.113, 0.132, 0.137, 0.139); memory.peak 49,228 KB; S~363 | **revert**: no time win; extra lookup/memcmp hurts |
| R2-K1 | Replace eight private endpoint dictionaries + post-parse catalogue merge/remap with one lock-free shared 4,096-slot table; endpoint IDs are assigned once, output uses a separately sorted 1,842-ID order | sample/full SHA OK; median **0.118 s** (0.089, 0.095, 0.118, 0.119, 0.129); memory.peak **47,980 KB**; S~332 | **keep**: 7.1% faster median and 2.0 MB lower peak vs B0 |
| R2-R2 | Fuse per-thread group counts/errors into parsing using a fixed 2,048-endpoint stride, eliminating the post-parse rewrite/count pass | sample/full SHA OK; median **0.119 s** (0.116, 0.117, 0.119, 0.125, 0.127); memory.peak **49,972 KB**; S~321 | **revert**: no time win and +2.0 MB peak vs K1 |
| R2-R3 | Two independent CRC32C accumulators to shorten the dependency chain in full endpoint hashing | candidate sample/full SHA OK, median **0.131 s**; immediate reverted control also **0.131 s**; candidate S~341 vs control S~332 | **revert**: no measured speed gain, 9 extra source lines |
| R2-K2 | For endpoints >=16 bytes, hash three independent 8-byte samples (head/middle/tail) with CRC32C instead of serially hashing every 8-byte word; full `memcmp` still resolves every collision | sample/full SHA OK; median **0.116 s** (0.099, 0.114, 0.116, 0.119, 0.127); memory.peak **48,596 KB**; S~342 | **keep**: 13% faster than the immediate 0.131 s control; correctness remains exact because the hash is only an accelerator |
| R2-K3 | Replace the seven-way HTTP method branch/switch with branchless byte comparisons that compute the endpoint start offset directly | sample/full SHA OK; median **0.114 s** (0.107, 0.112, 0.114, 0.115, 0.124); memory.peak **48,408 KB**; S~336 | **keep**: another small time win and 6 fewer source lines |
| R2-R4 | Lower small-group full-sort threshold from 64 to 16, using three `nth_element` calls for groups 17–64 | sample/full SHA OK; median **0.124 s** (0.105, 0.119, 0.124, 0.125, 0.131); memory.peak 49,216 KB | **revert**: full sort is faster for this many tiny groups |
| R2-R5 | Exploit nearest-rank extremes: for n<100 take p99 from `max_element`, and for n<20 p95 is the same max, reducing selection passes | sample/full SHA OK; median **0.120 s** (0.082, 0.114, 0.120, 0.120, 0.124); memory.peak 48,424 KB; S~345 | **revert**: still slower than the 0.114 s K3 check; `std::sort` wins on 17–64 here |
| R2-K4 | Halve thread-private dense group state from 64 to 32 bits by packing per-thread count/error as 16+16 bits; the seed-1 maximum group count is only 9,223 total | sample/full SHA OK; median **0.112 s** (0.086, 0.090, 0.112, 0.115, 0.119); memory.peak **45,896 KB**; S~340 | **keep**: 2 ms faster and 2.5 MB lower peak vs K3 |
| R2-R6 | Keep 32-bit thread-private counts but move 5xx errors to rare relaxed atomic increments in the shared global error array, removing the 16-bit local-count assumption | sample/full SHA OK; median **0.120 s** (0.088, 0.117, 0.120, 0.122, 0.123); memory.peak **44,624 KB**; S~336 | **revert**: saved 1.3 MB but lost 8 ms; K_acer fell 15.8 -> 15.3 |
| R2-K5 | Remove the 8-byte request-record array entirely: first pass interns/counts groups, then a second mmap pass parses again and scatters latencies directly into final group ranges | sample/full SHA OK; median **0.129 s** (0.124, 0.125, 0.129, 0.133, 0.144); memory.peak **36,088 KB**; S~337; K_acer~**15.5** | **keep for memory**: ~9.8 MB lower peak; modest time loss, and it enables eliminating the remaining 10 MB output peak |
| R2-K6 | Split percentile selection from formatting: select groups in parallel, then reuse one ~180 KB hour buffer and write each hour immediately instead of retaining 72 output strings | sample/full SHA OK; median **0.123 s** (0.090, 0.096, 0.123, 0.128, 0.129); memory.peak **26,208 KB**; S~349; K_acer~**16.8** | **keep**: another 9.9 MB off peak and best score so far |
| R2-K7 | Build a read-only 8,192-slot endpoint table after pass 1 using a cheaper two-CRC head/tail hash; pass 2 uses it instead of the concurrent three-sample hash table | sample/full SHA OK; median **0.111 s** (0.085, 0.099, 0.111, 0.128, 0.130); memory.peak **26,384 KB**; S~382; K_acer~**17.4** | **keep**: 12 ms faster; hash collisions still resolve with exact length+`memcmp` |
| R2-K8 | Use the same two-CRC head/tail hash and an 8,192-slot table during pass 1 too, removing the separate three-sample hash and post-pass lookup table | sample/full SHA OK; median **0.105 s** (0.083, 0.100, 0.105, 0.122, 0.123); memory.peak **26,192 KB**; S~348; K_acer~**18.2** | **keep**: faster, slightly less memory, and 34 fewer source lines than K7 |
| R2-R7 | In pass 2, compare the stored endpoint hash before the length+byte comparison on probe hits | sample/full SHA OK; median **0.113 s** (0.095, 0.107, 0.113, 0.117, 0.126); memory.peak 26,232 KB; S~350; K_acer~17.5 | **revert**: the extra comparison did not pay for the occasional avoided `memcmp` |
| R2-K9 | Eliminate the global `counts` array and fuse group-count reduction with the offsets prefix sum; derive each count from adjacent offsets during selection/output | sample/full SHA OK; median **0.103 s** (0.098, 0.100, 0.103, 0.110, 0.129); memory.peak **25,956 KB**; S~346; K_acer~**18.4** | **keep**: 2 ms faster, 236 KB lower measured peak, and fewer lines |
| R2-R8 | Skip the `error_ppm` division when a group has zero 5xx errors | sample/full SHA OK; median **0.118 s** (0.110, 0.115, 0.118, 0.119, 0.123); memory.peak 25,528 KB; S~347; K_acer~17.2 | **revert**: memory moved down but score/time regressed materially; branch did not pay on Acer |
| R2-R9 | Special-case singleton groups before the small-group sort | sample/full SHA OK; median **0.133 s** (0.116, 0.117, 0.133, 0.137, 0.140); memory.peak 24,932 KB; S~347; K_acer~16.3 | **revert**: branch overhead/noise outweighed skipping the trivial sort |
| R2-R10 | Format directly into a fixed char buffer rather than a reusable `std::string`, keeping `to_chars` for integers | sample/full SHA OK; candidate median **0.145 s** (0.140, 0.141, 0.145, 0.148, 0.148), memory.peak 25,324 KB, S~344; immediate reverted control median **0.143 s** | **revert**: no same-load speed win and slightly more memory |
| R2-R11 | Store a 16-bit endpoint id per input line during pass 1 so pass 2 can skip endpoint hashing/lookup | sample/full SHA OK; median **0.147 s** (0.139, 0.140, 0.147, 0.152, 0.152); memory.peak 26,144 KB; S~352; immediate control was ~0.143 s | **revert**: 6.2 MB logical id stream did not buy time and raised measured peak |
| R2-R12 | Compress second-pass per-thread scatter cursors to 16-bit relative offsets, freeing the 32-bit count/error table before allocating the latency payload | sample/full SHA OK; median **0.125 s** (0.117, 0.121, 0.125, 0.125, 0.128); memory.peak 25,240 KB; S~347; K_acer~16.8 | **revert**: the extra cursor array/correction cost outweighed the sub-MB measured peak reduction |
| R2-R13 | Save one endpoint-length byte per input line in pass 1 so pass 2 can jump directly to the status field and skip the AVX2 delimiter scan | sample/full SHA OK; median **0.151 s** (0.149, 0.150, 0.151, 0.155, 0.158); memory.peak 25,068 KB; S~354; K_acer~15.2 | **revert**: writing/reading the 3.1 MB side stream was slower than rescanning the mmap |
| R2-K10 | Remove all temporary `clock_gettime` phase instrumentation and unused headers after profiling was complete | sample/full SHA OK; median **0.121 s** (0.111, 0.118, 0.121, 0.124, 0.132); memory.peak **24,892 KB**; S~**322** as written; K_acer~17.3 | **keep**: 24 as-written lines removed and lower measured peak; timing is within the host-noise band |
| R2-R14 | Use only the physical-core analogue (4 threads on Acer, 6 on the scoring box) instead of all exposed hardware threads | sample/full SHA OK; median **0.146 s** (0.145, 0.145, 0.146, 0.148, 0.149); memory.peak 25,220 KB; S~323; K_acer~15.7 | **revert**: parser/selection throughput needs SMT on Acer; no memory benefit appeared in cgroup peak |
| R2-R15 | Cap both Acer and scoring runs at six threads | sample/full SHA OK; median **0.144 s** (0.141, 0.143, 0.144, 0.146, 0.148); memory.peak 24,892 KB; S~322; K_acer~15.8 | **revert**: still materially slower than using all exposed threads |
| R2-R16 | Add a middle-byte signature and collision flags so pass 2 can skip full endpoint `memcmp` when a fingerprint is unique in the completed catalogue | sample/full SHA OK; median **0.130 s** (0.129, 0.130, 0.130, 0.130, 0.130); memory.peak 25,236 KB; S~339; K_acer~16.5 | **revert**: extra signature work and source cost outweighed the avoided byte comparisons |
| R2-K11 | Narrow the post-merge global error array to 16-bit cells (the generator's per-group traffic/error envelope is far below 65,536) | sample/full SHA OK; median **0.125 s** (0.122, 0.124, 0.125, 0.128, 0.143) versus immediate K10 control 0.129 s; memory.peak **24,568 KB** vs 24,992 KB; S~322; K_acer~**17.0** | **keep**: both time and measured peak improved in the immediate comparison |
| R2-K12 | Use 16-bit per-thread request counts, accumulate the rare 5xx errors directly into one global 32-bit array, and reuse the count cells as relative scatter cursors | sample/full SHA OK; median **0.114 s** (0.103, 0.104, 0.114, 0.147, 0.155); memory.peak 25,036 KB; S~**320**; K_acer~**17.8** | **keep**: 11 ms faster than K11 in the official check and two fewer lines; narrower hot count state improves cache locality |
| R2-R17 | Replace the 2,048 group stride/bit-shift with the exact 1,842-endpoint stride to shrink dense arrays | sample/full SHA OK; median **0.139 s** (0.113, 0.131, 0.139, 0.140, 0.154); memory.peak 24,988 KB; S~320; K_acer~16.1 | **revert**: only 48 KB measured peak reduction and much worse timing; power-of-two addressing wins |
| R2-R18 | Add explicit likely/unlikely hints around endpoint insertion/hits, rare 5xx updates, and the small-group percentile path | sample/full SHA OK; median **0.126 s** (0.109, 0.115, 0.126, 0.131, 0.134); memory.peak 24,840 KB; S~320; K_acer~17.0 | **revert**: code-layout hints did not beat the K12 timing/score |
| R2-R19 | Force GCC loop unrolling globally with `#pragma GCC optimize("unroll-loops")` | sample/full SHA OK; median **0.128 s** (0.103, 0.127, 0.128, 0.134, 0.135); memory.peak 25,524 KB; S~321; K_acer~16.7 | **revert**: larger unrolled code did not improve the official check |
| R2-R20 | Let the main thread process one partition/hour while spawning only `threads-1` workers, reducing thread creation without reducing runnable compute threads | sample/full SHA OK; median **0.116 s** (0.115, 0.115, 0.116, 0.117, 0.121); memory.peak 25,964 KB; S~324; K_acer~17.5 | **revert**: timing was close but measured peak and source size regressed; K12 remained the better score |
| FINAL | K12 source after all later candidates were reverted | sample/full SHA OK; median **0.125 s** (0.101, 0.121, 0.125, 0.127, 0.127); memory.peak **25,416 KB**; S~**320** as written; K_acer~**17.0** | final retained source; host timing noise is reported rather than cherry-picked |

Temporary `clock_gettime(CLOCK_MONOTONIC_RAW)` instrumentation on five full runs showed the same ranking despite host noise: parsing was the largest phase (**35.8–67.1 ms**), then percentile selection + formatting (**14.9–25.2 ms**), writing (**4.0–19.1 ms**), scatter/free (**6.8–9.4 ms**), group rewrite/count (**6.1–7.9 ms**), endpoint-catalogue merge (**3.7–5.9 ms**), prefix/cursor setup (**1.3–1.7 ms**), setup (<0.1 ms). This makes endpoint lookup/parsing the first target; timestamp decoding is already four digit operations and not calendar code.

After K1, three profiled full runs put endpoint-order sorting at only 0.3–0.4 ms, group rewrite/count at 5.7–6.3 ms, scatter/free at 7.5–8.7 ms, and parsing at 48.8–59.8 ms. The shared endpoint table therefore removed the old 3.7–5.9 ms catalogue-merge cost almost entirely.

After K3, extra sample seeds 7 and 11 were regenerated on Acer and compared to the official Python reference. Both matched byte-for-byte: seed 7 `39bfccc5334c93cbe0c762f5e6c3300e91ca2e266567561da3176c038cee49f7`, seed 11 `a81124d584b7a8e438b143a08fd045b7799c2ced3343a80825c9c479b9fd5c66`.

After K6, the same extra seeds were rerun and still matched byte-for-byte. K7-K9 are the next three kept changes, so they are rechecked below before further tuning.

After K9, seeds 7 and 11 again matched the reference exactly with the same hashes above.

After K12, seeds 7 and 11 again matched the reference exactly with the same hashes above.

## Round-1 context retained for comparison

Round 1 established the fixed-date hour decoder, direct seven-method endpoint start, AVX2 delimiter scan, CRC32C endpoint hashing, parallel dense scatter, and the small-sort / large-`nth_element` percentile split. Its official round-2 starting point was 0.127 s median, 50,048 KB peak, and S~324 as written. Earlier synthetic-only experiments had already rejected manual prefetch, `-mtune=alderlake`, synthetic PGO, and a weak first/last-word endpoint hash; round 2 re-measured every new idea on Acer instead of relying on those secondary timings.

## Correctness evidence

- Every official Acer optimization check above passed both canonical seed-1 sample and full SHA-256 comparisons before its timing was considered.
- The round-1 specification example, reconstructed comprehensive edge fixture, and a 50,000-line randomized oracle fixture all matched byte-for-byte; ASan+UBSan were clean on the latter two.
- Extra official-generator sample seeds 7 and 11 were checked after K3, K6, K9, and K12. They always matched the independent official Python reference: seed 7 `39bfccc5334c93cbe0c762f5e6c3300e91ca2e266567561da3176c038cee49f7`; seed 11 `a81124d584b7a8e438b143a08fd045b7799c2ced3343a80825c9c479b9fd5c66`.
- Endpoint hash collisions are never trusted for identity: both interning and second-pass lookup retain exact length+byte comparison.

## Final memory shape

The round-2 design removes the round-1 request-record arrays and retained per-hour output strings. On full input the main payload is the 3.1M-element latency vector (~12.4 MB), plus per-worker 16-bit dense count/cursor cells (about 2.4 MB on 8-thread Acer and 3.5 MB on the 12-thread scoring box), one 32-bit error array and one 32-bit offset array (~1.2 MB total), the small endpoint catalogue, and runtime/thread overhead. Official Acer checks of the final family land around 25 MB cgroup `memory.peak`, roughly half the 50 MB round-2 baseline.

## Static axis and score evidence

The lead checker reports **S~320 as written** for the final source + Dockerfile and D remains 1. The exact organizer formatter/scc measurement could not be reproduced on this Mac because Docker and the pinned static-tools image are unavailable here; `static_axes.py` failed explicitly with `docker is not on PATH`, so no formatted S is invented. Acer timing is visibly noisy across neighboring checks; the best kept K9 check was K_acer~18.4, K12 measured K_acer~17.8, and the final repeated K12 check is recorded in REPORT.md rather than cherry-picked.

No board run was pushed or submitted by this worker.
