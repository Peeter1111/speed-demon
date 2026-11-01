# Track D performance and optimisation record

**Environment:** Acer x86-64 Intel i5-1035G1, eight hardware threads,
Debian GCC 14.2, `-O3 -march=x86-64-v3 -pthread -DNDEBUG`,
static libstdc++/libgcc. Each number below is **MEASURED** as the median of
five independent full-process runs, with exclusive `flock` on
`~/speeddemon/bench.lock`. Time uses `time.perf_counter()` around
`fork/exec/wait4`; memory is child `wait4().ru_maxrss` in Linux KB.
The reference full-size Acer time/memory of **74.65 s / 872,796 KB** was
measured by the existing preparation job and saved in `ref-times.txt`
(not rerun by this worker). The sample reference was 4.98 s / 92,160 KB.

All listed versions were checked against the official full-size seed-1
reference SHA-256 before their timings were accepted. Bench JSON, including
all five individual runs, is preserved in the worker's `measurements/`
folder, outside this submission directory.

| Change / variant | Full median seconds | Median peak KB | Result |
|---|---:|---:|---|
| Serial parser, dual CSR, binary heap, 12 search workers | 1.279327 | 421,612 | Correct baseline |
| Same, 4 search workers | 1.404965 | 255,184 | Slower |
| Same, 6 search workers | 1.309867 | 257,436 | Less memory |
| Same, 8 search workers | 1.244235 | 312,012 | Faster and leaner than 12 |
| Same, 10 search workers | 1.276585 | 366,960 | Slower |
| Same, 8 workers with a 4-way heap | 1.249242 | 311,976 | Marginally slower |
| Split input into 4 parallel parse chunks, 8 workers | 1.172385 | 311,992 | Faster |
| Split into 8 parse chunks, 8 workers | 1.151917 | 311,956 | Faster |
| Split into 12 parse chunks, 8 workers | 1.149146 | 312,008 | Faster |
| Same 12 chunks with `-flto` | 1.149414 | 312,024 | No material benefit |
| **Final: 12 chunks, 8 workers, binary heap** | **1.144664** | **312,128** | **Correct** |

On the Apple M1 Pro (not comparable to the Acer hardware), a 4-way heap
measured 0.4002 s, a binary heap 0.3939 s, an 8-way heap 0.4105 s,
and a radix heap 0.4584 s, each median of five runs. All four versions
produced the correct full hash. The radix heap was discarded.

**Final correctness (Acer, five successful runs each):**

| Dataset | Median seconds | Median peak KB | Expected = actual SHA-256 |
|---|---:|---:|---|
| Seed 1 sample | 0.124005 | 33,184 | `a4c76ed030bc7e365db33191f6cf74708fba7f25eb48d64c8c4279f2e57046a0` |
| Seed 1 full | 1.144664 | 312,128 | `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` |
| Seed 7 sample | 0.162269 | 33,632 | `88bf36c94620505d1beafe7cd56a08dc2d261b71f9824d2f764d5ce6e5d7872e` |
| Seed 8 sample | 0.105778 | 32,956 | `b75df2f1321b10f3b6da1f786615a262dcebb6f3132fb285514b3c95dc7f6805` |

All four expected hashes came from the reference, including direct comparison
with Acer's existing reference output for seed 1 and new Python reference
runs for seeds 7 and 8. Independent Mac checks on 100 small random graphs
(some above 2,047 queries, exercising epoch rollover) also matched.

The final solution is **65.2x faster** than the measured full Acer Python
reference, with **2.80x less peak resident memory**. Before contest formatting,
a local nonblank/noncomment line count for C++ source, Dockerfile and TOML is
**256** (245 + 7 + 4); this is an **estimate**, not an official
`clang-format`+`scc` count. With that provisional S and `D=1`,
`K = (74.65/1.144664)^0.5 (872796/312128)^0.2 (43/256)^0.15`
is approximately **7.59** on Acer-derived ratios. The actual K is unknown
until the scoring-box CPU, formatter, and cgroup memory measurement run.
The 0.27 s board record was made on another CPU and is not a comparable
benchmark.

**Open items:** Docker offline build and actual official scoring-box run have
not been completed; only the lead can submit/install the board app. The
official `scc` and LLVM formatting tools were not installed locally.

## Round 2, official Acer check1.sh measurements

Round 2 uses the lead's Oct 31 `check1.sh`, which rebuilds the submitted
folder, verifies seed-1 sample and full SHA-256, takes five full timings, and
reads cgroup `memory.peak`. Only rows below produced by that check are used for
round-2 keep/revert decisions.

| Change / variant | Full median seconds | memory.peak KB | S~ | Result |
|---|---:|---:|---:|---|
| Copied round-1 source, new official check baseline | 1.207 | 313,456 | 252 | sample/full SHA OK; baseline |
| Per-thread dense degree arrays + parallel CSR scatter | 1.456 | 313,000 | 283 | SHA OK; **reverted** (20.6% slower, negligible memory change) |
| Uninitialized raw edge arrays instead of zero-filled vectors | 1.397 | 313,196 | 252 | SHA OK; **reverted** (slower; losing sequential first-touch hurt more than avoiding zero fill helped) |
| Parallel CSR scatter using relaxed atomic cursors | 1.199, retest 1.238 | 313,300 / 313,360 | 263 | SHA OK; **reverted** (first run barely faster, retest slower; extra source lines also hurt S) |
| Count forward/reverse degrees during parallel parse with relaxed atomics | 1.357 | 313,300 | 250 | SHA OK; **reverted** (atomics slowed the parser more than the serial count pass they replaced) |
| 65-bucket monotone radix heap | 1.278 | 313,676 | 258 | SHA OK; **reverted** (slower than binary heap, matching round-1's earlier radix result) |
| 4096-slot Dial ring for small weights + binary fallback for large edges | 1.262 | 330,748 | 306 | SHA OK; **reverted** (slower, +17 MB peak and large S penalty) |
| **Sparse open-addressed per-query distances (2^17 slots/side)** | **1.273** | **165,160** | **270** | SHA OK; **kept**. Time +5.5% vs new baseline, but peak memory -47.3% and K estimate rose 7.4 -> 8.1. Seed-1 profiling showed max discovered vertices only 39,323/39,236 per direction, so the table stays below 31% load. |
| **Fuse sparse-table lookup + decrease-key into one `improve` probe** | **1.258** | **165,200** | **281** | SHA OK; **kept**. Saved a second hash/probe on each successful relaxation; 1.2% faster than the first sparse version with unchanged peak memory. |
| **Use randomized node id directly as sparse-table hash index** | **1.262** | **165,108** | **281** | SHA OK; **kept**. Runtime is effectively flat within run noise, peak dropped 92 KB, and the multiply/shift vanished. |

After these three kept changes, fresh sample seeds 9 and 10 were generated on
Acer and compared to new Python-reference outputs. Both matched exactly:
`c5c972553116d7174716c1b7fc39f8804f7fd022cf6dd8a40763e1005c6fd4a0`
(seed 9) and `a6c0bbe658da821e540e9c7e4dc7582d1f9fe1d8d00c8f5da8bab8c5ecdbe172`
(seed 10).

| 16-bit weight SoA with sparse side table for the 50 large edges | 1.621 | 145,696 | 293 | SHA OK; **reverted**. It saved 19.4 MB but was 28.4% slower, so K fell despite the memory win. |
| Packed 12-byte AoS entries for sparse distance table | 1.363 | 165,252 | 281 | SHA OK; **reverted**. Co-locating tag/value hurt lookup speed versus split tag/value arrays. |
| Update bidirectional best only when a node is popped, not on relax | 1.293 | 166,084 | 279 | SHA OK; **reverted**. Fewer opposite-table probes were outweighed by later meeting detection and extra search. |
| Choose the smaller frontier key except for no-in-degree sources | >65 s on sample check; killed | n/a | 282 | **reverted** before full timing. A large-weight special case outside the no-in-source pattern still drove a near-full reverse scan; the alternating policy is necessary for this generator. |
| Sparse-search thread sweep: 6 / 8 / 10 / 12 / 14 workers | 1.339 / 1.354 / 1.297 / 1.337 / 1.292 | 165,920 / 165,976 / 166,204 / 166,476 / 166,024 | 281 | SHA OK on every official check. 10 retested at 1.279. **Default remains 8**: its earlier kept check was 1.262 / 165,108 and extra workers did not reproducibly beat that while slightly raising memory. |
| Node-range parallel CSR scatter (8 workers, no atomics) | 1.258 | 166,200 | 295 | SHA OK; **reverted**. Timing tied the best sparse run but peak rose 1.1 MB and S rose 14 lines, reducing K. |
| **Scatter forward CSR and reverse CSR in separate passes** | **1.230** | **166,084** | **281** | SHA OK; **kept**. Reading the 60 MB triple buffer twice reduced cache thrash from interleaving two random cursor arrays and two random 40 MB edge arrays. K rose to ~8.2. |
| **Count forward and reverse degrees in separate passes** | **1.197** | **166,316** | **281** | SHA OK; **kept**. The extra sequential triple-buffer read again beat interleaved random writes to both degree arrays. K rose to ~8.3. |
| **Allocate reverse CSR only after forward scatter finishes** | **1.192** | **166,340** | **282** | SHA OK; **kept**. A small 0.4% timing win; reverse-array first-touch no longer pollutes caches immediately before the forward scatter. |

After these three CSR-build keeps, fresh sample seeds 11 and 12 again matched
new Python-reference outputs exactly: `0db605f567d4135aba7c30b77cccda299a3487aa35887eac7b2937fa6d4d1db5`
and `3789d656689d660cd6e391b48bb2cc249107bc4147b07865fa6a8ce18429a274`.

| **Shrink sparse distance table from 2^17 to 2^16 slots/side** | **1.103** | **165,936** | **282** | SHA OK; **kept**. Seed-1 max discovered was 39,323 per side, so this is ~60% worst observed load. Better cache locality cut 7.5% from the previous kept check; K rose to ~8.6. |
| True binary-heap decrease-key using sparse-table slots as heap handles | 1.291 | 166,240 | 303 | SHA OK; **reverted**. Removing stale heap entries did not pay for maintaining heap positions and decrease-key updates. |
| **Pack parsed triples into 64 bits (21-bit u, 21-bit v, 22-bit weight/sentinel)** | **1.080** | **145,724** | **297** | SHA OK; **kept**. Only 50 seed-1 weights need the tiny side vectors. Triple scratch fell from 60 MB to 40 MB, which also sped the separate count/scatter passes. K rose to ~8.9. |
| **Pack final CSR edges into 5 bytes (21-bit target + 18-bit small weight/large-index flag)** | **1.121** | **116,560** | **308** | SHA OK; **kept**. Runtime regressed 3.8%, but peak memory fell another 29.2 MB and K improved to ~9.1. The 50 large weights are indexed through tiny side vectors, so uint32 edge weights remain exact. |

Fresh sample seeds 13 and 14 matched their newly run Python references after
the table/triple/CSR packing changes: `1cb151f0b26a1c88a50e8dd8fecf99470117c34a0cce3cf661f786b2f7c3f795`
and `1c43924b2ac07fb824db7a20060f3aba853b203c294bb2425d1b33cacbb6a660`.

| **6-byte packed CSR edge (uint32 target + uint16 small weight/index)** | **1.104** | **126,932** | **306** | SHA OK; **kept as the speed branch**. It recovered 17 ms versus 5-byte edges at the cost of 10.4 MB; K was ~9.0 versus ~9.1 for 5-byte. Final branch choice remains based on later combined checks. |
| **Pack each CSR node's 23-bit start offset + degree into one uint32** | **1.051** | **126,572** | **310** | SHA OK; **kept**. Seed-1 max in/out degree is only 10. Search now needs one random range load per settled node instead of `start[u]` and `start[u+1]`; total time improved 4.8% and K rose to ~9.2. |
| **Replace binary-heap child loop with one explicit right-child comparison** | **1.053** | **126,412** | **309** | SHA OK; **kept**. Time was flat within noise, peak improved 160 KB and S dropped one line. |

Fresh sample seeds 15 and 16 matched newly run Python references after the
packed-range and heap changes: `3f36ed798b12d3b5354a704878802b13894a3090dcc693ef5ab8d706c33abfdf`
and `a81262b0564a1cb2b230b52645f8512e29cd66ed76f57817b6c64f3deb04c609`.

| **Return to 5-byte CSR edges, retaining packed ranges + heap simplification** | **1.051** | **117,060** | **311** | SHA OK; **kept**. It tied the 6-byte branch's best time while saving 9.4 MB; K rose from ~9.2 to **~9.4**. This supersedes the earlier provisional 6-byte branch. |
| Sparse distance table 2^16 -> 2^17 slots/side | 1.166 | 116,628 | 311 | SHA OK; **reverted**. Extra capacity cost ~11% wall time from worse cache locality. The 2^16 table has passed seed 1 full plus eight extra sample seeds (9-16), with seed-1 full max discovered 39,323 per direction versus 65,536 slots. |
| 8-byte native CSR edges with packed ranges | 1.019 | 146,212 | 301 | SHA OK; **reverted**. It was 3.0% faster than the 5-byte branch but cost 29.2 MB; K fell from ~9.4 to ~9.1. |
| **Pack heap items from 16 to 12 bytes** | **1.040** | **117,084** | **311** | SHA OK; **kept**. Unaligned uint64 heap keys are cheap on x86-64-v3 and the smaller heap improved locality; 1.0% faster than the prior 5-byte-edge branch. |
| **Decode 5-byte CSR edges with one safe 8-byte unaligned load** | **1.024** | **116,876** | **312** | SHA OK; **kept**. Each CSR vector has one padding edge so the 8-byte `memcpy` load may cross the next packed edge without reading past storage. This replaced a 32-bit + 8-bit load/merge and lifted K to ~9.5. |
| Prefer smaller frontier key but cap same-side streak at two expansions | 1.055 | 116,804 | 314 | SHA OK; **reverted**. Strict alternation remained faster on this local-road workload. |
| **Store common sparse distances in uint32, spill >=2^32-1 to lazy uint64 slots** | **0.990** | **117,104** | **323** | SHA OK; **kept**. Normal road distances now use a 256 KB value array per direction; only the injected huge-weight queries touch the parallel 64-bit spill pages. Wall time fell 3.3%, K rose to ~9.6. |
| **Store common sparse distances in uint16, spill >=65,535 to lazy uint64 slots** | **0.941** | **117,004** | **323** | SHA OK; **kept**. Only six seed-1 final distances exceed 65,535; ordinary searches use 128 KB values per direction. Wall time fell another 4.9%, K rose to ~9.8. |
| **Deduplicate identical query pairs before search** | **0.931** | **117,064** | **330** | SHA OK; **kept**. Seed 1 has 20 deliberate duplicate queries; an O(q²) 2,000-row prepass removes their searches and copies answers back in input order. K reached ~9.9. |

| Grow the sparse table at 65,536 discovered vertices | 0.949 | 116,912 | 356 | SHA OK, **reverted**: adding a dynamic-table resizing guard cost ~0.5% time and 26 scored lines on the official Acer check; K dropped from ~9.8 to ~9.7. The chosen 2^16 capacity is empirically validated on seed 1 and extra seeds, but is not a mathematical bound for arbitrary inputs. |
| Final 16-bit, packed-CSR version: query workers 6 / 10 / 12 / 4 | 0.946 / 0.952 / 0.973 / 1.009 | 116,732 / 116,520 / 116,816 / 116,832 | 330 | All sample/full SHA OK with the official Acer check. **Default remains 8**: 0.944 s / 116,792 KB on the adjacent baseline check, with equal/better measured speed than 6 and lower thread overhead than 10/12. |

Two further independent generated sample seeds were checked on the Mac against the
official Python reference, with exact byte-identical SHA-256 results: seed 17
`1c03f6046d75ef9a4e1f5d4cac506904424c56082b559706effeb639b2eac387`,
seed 18
`1b5e023631559af75b48e14bc3ed7258f603070ad20d61d9b518fb6b3c170293`.
These are correctness checks, **not** Acer timings or official-scoring measurements.

The final unchanged eight-worker source was rechecked with the official Acer
command after the thread sweep: sample SHA OK, full SHA OK, full-process
five-run median **0.938 s** (0.935, 0.935, 0.938, 0.949, 0.950),
**117,136 KB** cgroup memory.peak, **S~330**, **K_acer~9.8**.
The nearest previous official check of the same source gave 0.944 s /
116,792 KB; treat the ~6 ms difference as measurement noise.
Additionally, 100 newly generated small random directed graphs with 124
queries each passed an independent Mac Dijkstra oracle, including large
uint32 weights, zeroes, loops, unreachable pairs and repeated queries.
No Docker image, official scoring-machine result, or contest submission
has been checked by this worker.

## Round 3: exact overflow fallback and local verification (Oct 31)

The round-two source above is the unchanged starting point, including the
failed 2^17 sparse-table, Dial, radix, parallel-CSR and adaptive-frontier
experiments: **none of those experiments was repeated**. The round-two
official Acer best, **0.938 s / 117,136 KB**, is historical and must not be
reported as a round-three measurement. In this session SSH to any server was
prohibited by the controlling user instruction, so there is **no round-three
Acer check or cgroup memory reading**. All numbers below are measured on
the local Apple Silicon Mac, not on the scoring CPU, and cannot be used to
calculate the official contest K.

### Baseline phase split, first task

Instrumented the *unchanged search/CSR algorithm* with timestamps around
the mmap/newline-aligned parser, degree count and CSR scatter, and query
processing. On seed-17 **sample**, the initial local run measured:
**parse 0.007377 s; CSR build 0.003610 s; search/queries 0.018951 s**.
After generating the exact published seed-1 full inputs locally (verified
their graph and query SHA-256 against the spec), the first full run of the
new source measured **parse 0.053019 s, CSR 0.075828 s, search/queries
0.446797 s**. Repeated warmed full-size runs were much faster; the medians
below are the more comparable local readings. The search/queries phase
includes query parsing, deduplication, worker startup and searching.

### Change log (paired, 8 full runs per implementation and seed on Mac)

| Trial | Full seed 1 median wall | Full seed 2 median wall | Result |
|---|---:|---:|---|
| Round-2 original code, compiled with local Apple Clang -O3 | 0.263816 s | 0.263751 s | Comparison baseline, all published/ref SHA OK |
| **Round-3 dense fallback at 65,535 entries**, 16-bit compact fast path | **0.274408 s** | **0.279224 s** | **Kept for correctness**. Full reference hashes match; Mac wall +4.0% / +5.9%, no Acer time. Worst-case sparse-table hang removed. |
| Branch prediction hints for unlikely dense path vs exact same round-3 source without hints | 0.276022 vs 0.283844 s | 0.287334 vs 0.278387 s | **Reverted**. Opposite seed-level results, no reproducible win. |

For the final fallback source, an isolated warmed full seed-1 run printed
parse **0.023820 s**, CSR **0.061638 s**, search/queries **0.187581 s**;
full seed 2 printed **0.024331 / 0.064405 / 0.177770 s**. In the paired
eight-run seed-1 comparison, the measured median Mac process RSS was
approximately 217.7 MB for round two versus 217.9 MB for round three;
seed 2 was 215.0 MB versus 212.4 MB. This is *Mac peak RSS*, not the
cgroup memory.peak used for scoring.

The fallback moves to epoch-tagged dense distance storage only when a
direction has inserted 65,535 unique nodes, and migrates the current
sparse records. The sparse table never fills up and lookups never spin
indefinitely. Dense memory is lazily allocated per search worker/direction.
No new runtime dependency was added. Source length including Dockerfile and
TOML is about **368 nonblank/noncomment lines**, not an official formatted
SCC line count (the round-two estimate was ~330).

### Correctness checks, all measured locally

| Input | SHA-256 of distances.txt | Comparison |
|---|---|---|
| Seed 1 full, 5,000,000 edges, 2,000 queries | `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` | Published official reference, exact |
| Seed 2 full, 5,000,000 edges, 2,000 queries | `23d60c06b8e2c6086f765e4a9a3640306c3bf8199149fd39b20e1ef56b3b8a4b` | Fresh local Python reference, exact |
| Seed 17 sample | `1c03f6046d75ef9a4e1f5d4cac506904424c56082b559706effeb639b2eac387` | Existing round-two Python reference, exact |
| Seed 18 sample | `1b5e023631559af75b48e14bc3ed7258f603070ad20d61d9b518fb6b3c170293` | Existing round-two Python reference, exact |
| Two disjoint 90k directed rings, 2,051 queries; dense fallback before/after epoch rollover | `319139fc9f0772edbcf88ae907aea054001cf4eb3ef27243f9340fe6091bea63` | Analytic `-1`/1 expected output, exact |
| 160k directed chain, 17 queries, eight workers overflowing simultaneously | `a28f46fe929dbe5833787155f339977d9591bc7891f69d0ee9956da4b3364a73` | Exact path lengths, exact |
| 80-node weighted random graph, 2,300 unique queries, 32-bit max weights | `18512b83146b406b9755cb963a99c34fbcf4f46af74b808da4cc5d2f69e82ded` | Independent Python Dijkstra oracle, exact |

Code changes after the last hint experiment were reverted to the measured
fallback. Rebuilt and reran both full references successfully. The official
two-stage Dockerfile, digest pins, C++20 compiler flags and packaging were
preserved unchanged. No Docker or Linux test was possible locally, and no
official scoring submission was made. The BFS/ID-locality reorder was not
implemented: source IDs are deliberately randomized, a reindex requires
full forward/reverse CSR reconstruction, and it would add both peak memory
and an O(n+m) preprocessing pass, which cannot be justified without a
Linux score measurement.

## Round 4: reverse-CSR redesign, measurements

All checks below use the lead's check script on the Acer Linux test laptop (i5-1035G1, 8 threads): five timed full runs, sample/full/full-seed-2 SHA-256 checks, and cgroup `memory.peak`. Mac timings are identified separately. `S` is the check's formatted line count, not source `wc -l`.

| Trial | Acer median (s) | Peak KB | S | Correctness | Decision |
|---|---:|---:|---:|---|---|
| Round-three baseline copied unchanged, eight workers | 1.022 (1.007, 1.008, 1.022, 1.038, 1.063) | 116,340 | 469 | sample/full/seed-2 full SHA OK | baseline; retained separately for comparison |
| New undirected BFS relabel + packed CSR, original identity hash | 1.643 (1.625, 1.630, 1.643, 1.671, 1.882) | 123,128 | 560 | all three SHA OK | superseded: identity hash clusters all nearby relabelled nodes into same hash buckets |
| Same BFS relabel + multiplicative distance hash | 1.599 (1.588, 1.592, 1.599, 1.600, 1.645) | 123,092 | 560 | all three SHA OK | faster by 44 ms; kept for further trials, still slower than baseline |
| Directed/forward-only BFS instead of undirected BFS | 1.420 (1.419, 1.419, 1.420, 1.432, 1.446) | 123,268 | 558 | all three SHA OK | 179 ms faster, still slower than baseline; superseded |
| Remap each packed edge once before CSR passes | 1.160 (1.145, 1.158, 1.160, 1.161, 1.175) | 123,296 | 553 | all three SHA OK | 260 ms faster, but baseline still wins; discarded for final |
| Four-ary heap, starting from unchanged round-three source | 1.054 (1.050, 1.051, 1.054, 1.055, 1.067) | 116,296 | 473 | all three SHA OK | 32 ms slower than baseline, reverted |
| Build reverse CSR from completed forward CSR, release parsed edge chunks first | 1.036 (1.030, 1.030, 1.036, 1.049, 1.056) | 92,384 | 475 | all three SHA OK | lower peak by 23,956 KB; stronger K ~9.3, kept as memory branch |
| **Build forward CSR from completed reverse CSR, release parsed edge chunks first** | **0.956 (0.953, 0.955, 0.956, 0.959, 0.965)** | **92,556** | **475** | **all three SHA OK** | 80 ms faster than reverse-first alternative, kept |
| **Compact both CSR sides to 4-byte edges**: 21-bit destination, 10-bit inline weight, exception bit and sorted `(edge-index, full-weight)` side vectors | **0.946 (0.934, 0.943, 0.946, 0.946, 0.966)** | **88,180** | **475** | **all three SHA OK** | saves another 4,376 KB and 10 ms; **current best, K ~9.8** |
| 256-edge blocks for O(1)-size exceptional-weight scans | 0.996 (0.979, 0.984, 0.996, 0.998, 0.999) | 87,608 | 487 | all three SHA OK | 50 ms slower despite 572 KB savings; **reverted** |
| Extend inline weight sentinel from 1024 to 2047 | 0.984 (0.976, 0.978, 0.984, 0.990, 1.018) | 87,088 | 475 | all three SHA OK | 38 ms slower despite 1,092 KB saved and fewer exception lookups; **reverted** |
| **Final identical 1024-sentinel 4-byte source recheck** | **1.007 (1.002, 1.007, 1.007, 1.010, 1.017)** | **87,600** | **475** | **all three SHA OK** | final pinned revision; same source as the 0.946 s Acer trial, showing ~6% run-to-run/session variability; K_acer ~9.5 on final check |

Additional targeted Mac-only tests: unidirectional Dijkstra *with forward-BFS reordering* used 0.3937 s in the search phase, versus 0.1069 s for bidirectional with the identical remapped CSR. Both produced the exact seed-1 full reference SHA. The single-direction alternative was rejected before spending an Acer check because its search work alone cost 3.7x as much.

The earlier unidirectional **without** relabeling likewise measured 0.8744 s in the Mac search phase versus 0.1678 s with bidirectional. Neither alternative is used in the final source. Full-graph data has only 3,478 weights above 2047, versus 52,012 above 1023; surprisingly, extending the direct range up to 2046 was slower on Acer despite fewer exception lookups. The original 1024-sentinel 4-byte design remains the fastest tested configuration.

Final source, Apple Silicon warm runs on seed-1 full: parse 0.019838/0.022254/0.019971 s (median **0.019971 s**), CSR 0.066547/0.067031/0.062097 s (median **0.066547 s**), query processing 0.185274/0.225048/0.180185 s (median **0.185274 s**). Seed-2 full also matched its published expected SHA. These are *not* Acer/scoring-machine phase figures; the standard Acer checker does not expose its internal phase split. A measured `parse <30 ms` result on Acer cannot honestly be claimed.

Final Acer check, pasted verbatim:

```text
sample sha OK d-sample/distances.txt
full sha OK d-full/distances.txt
full2 (seed 2) sha OK d-full2/distances.txt
full T median 1.007 s (runs 1.002 1.007 1.007 1.010 1.017)  memory.peak 87600 KB  S 475 (official count)  K_acer~9.5
```

Mac seed-1 phases, `SD_PROFILE=1`, Clang `-O3` on Apple Silicon (not Acer scoring): original new design with undirected BFS and direct hash was parse 0.0559 s, BFS 0.1486 s, final CSR 0.1337 s, search 0.2012 s (first/cold read); with BFS disabled on the same binary 0.0195/0/0.0595/0.1678 s. Multiplicative hashing cut the relabeled search to 0.1038 s, but did not repay the extra BFS plus CSR construction. A forward-only BFS (temporary `m` instead of `2m` arcs) measured 0.0541/0.0874/0.1339/0.0970 s on Mac and matched seed-1 full SHA; Acer check pending.

With packed edges remapped once, Mac full seed-1 phases were parse 0.0190 s, relabel/remap 0.1057 s, CSR 0.0721 s, search 0.1095 s; the preprocessing still costs more than it saves. The relabeling/hash interaction was real: using `node & 65535` for BFS-ordered IDs is very collision-prone, while the multiplicative hash restores uniformly distributed slots. But overall Acer wall time and K decreased, so BFS renumbering is not retained as default.

Reconstructing the second CSR direction from the first instead of rescanning the 64-bit parsed triples permits those buffers to be freed *before* the second 5M-edge CSR allocation. The reverse-to-forward order beat forward-to-reverse on Acer and in local two-seed phase tests, despite exact same graph and SHA outputs. With 4-byte edges, weights above 1023 (52,012 of seed-1's 5,000,000 arcs) live in sorted exceptional-weight lookup vectors keyed by *CSR edge index*. These vectors are consulted only for flagged arcs, so full 32-bit weights and zero-cost arcs are preserved. The block-index alternative was correctly hashed but slower, so binary search remains.

Baseline check output:

```text
sample sha OK d-sample/distances.txt
full sha OK d-full/distances.txt
full2 (seed 2) sha OK d-full2/distances.txt
full T median 1.022 s (runs 1.007 1.008 1.022 1.038 1.063)  memory.peak 116340 KB  S 469 (official count)  K_acer~8.9
```

## Round 5: Acer phase-led optimization

All measurements below use the lead's check script (`check1.sh d`) on the Acer test laptop. Every checker run validates sample/full/seed-2-full hashes, runs five timed full trials, measures `memory.peak`, and counts formatted source lines. Source changes are retained only when all hashes remain correct and the checker's printed K increases.

| Trial | Acer median (five times) | Peak KB | Official S | K_acer | Decision |
|---|---|---:|---:|---:|---|
| r5-baseline, copied Round 4 | 0.951 s (0.945 0.947 0.951 0.956 0.960) | 88,328 | 475 | 9.8 | Baseline; all SHA OK |
| r5-smaller-frontier, expand whichever Dijkstra queue has fewer heap entries | 0.929 s (0.925 0.927 0.929 0.936 0.936) | 88,080 | 473 | 9.9 | **Kept**; all SHA OK, +0.1 K |
| r5-smaller-distance, expand minimum-distance queue except for enormous frontier distances | 0.970 s (0.967 0.968 0.970 0.977 1.010) | 88,228 | 475 | 9.7 | **Reverted**; all SHA OK but lower K |
| r5-32k-sparse-dist, halve each per-worker sparse-distance hash table | 0.857 s (0.851 0.855 0.857 0.862 0.866) | 223,888 | 473 | 8.5 | **Reverted**; all SHA OK, but a query triggered dense fallback and sharply increased memory peak, overwhelming the speed gain |
| r5-6-workers, reduce query threads from eight to six | 0.948 s (0.945 0.946 0.948 0.955 0.958) | 87,808 | 473 | 9.8 | **Reverted**; fewer threads lowered K |
| r5-forward-exception-scan, replace binary search with monotone exception iterator when reconstructing CSR | 0.928 s (0.926 0.927 0.928 0.936 0.943) | 87,712 | 472 | 9.9 | **Reverted by strict rule**; SHA OK but printed K tied incumbent 9.9 |
| r5-unpacked-heap, use 16-byte aligned heap elements instead of packed 12-byte elements | 0.984 s (0.941 0.948 0.984 1.083 1.145) | 87,716 | 473 | 9.6 | **Reverted**; slower and lower K, with visible Acer timing variability |
| r5-parallel-reverse-scatter, 12 concurrent atomic reverse-CSR cursors with thread-local exceptional-weight lists | 0.916 s (0.913 0.915 0.916 0.922 0.929) | 90,048 | 491 | 9.9 | **Reverted by strict rule**; SHA OK and time improved, but memory/source costs kept K tied |
| r5-128k-sparse-dist, double per-worker hash-table entries to reduce collision chains | 1.057 s (1.049 1.051 1.057 1.062 1.075) | 88,088 | 473 | 9.3 | **Reverted**; source remained correct, larger hot-table footprint hurt cache performance |
| r5-fused-degree-count, count both CSR in/out degrees in one pass over parsed arcs | 0.959 s (0.954 0.956 0.959 0.959 0.963) | 88,320 | 472 | 9.7 | **Reverted**; coupling two random degree arrays was slower |

Final clean checker on the restored best candidate:

```text
sample sha OK d-sample/distances.txt
full sha OK d-full/distances.txt
full2 (seed 2) sha OK d-full2/distances.txt
full T median 0.931 s (runs 0.927 0.931 0.931 0.933 0.946)  memory.peak 88384 KB  S 473 (official count)  K_acer~9.9
```

The final source differs from Round 4 only by using the smaller bidirectional heap frontier to select the side to expand. The original's special-case correctness is preserved; every official Acer SHA check across all variants matched the reference. Relative to the first clean Round 5 measurement, 0.951 s / 88,328 KB / S 475 / K 9.8, the final scored 0.931 s / 88,384 KB / S 473 / K 9.9. These are measurements on Acer's eight-thread laptop, not the twelve-thread official scoring box, and the run-to-run difference is small enough that the original Round 4 source's score cannot be distinguished without more trials.

The baseline was profiled using a temporary print-to-stderr build via a one-run profiling script (not used as a timing score): parse **0.039010 s**, CSR construction **0.288704 s**, search/query handling **0.599055 s**, combined single-run wall **0.934013 s**. Hence search dominates (roughly 64% of the wall), followed by CSR (31%); parse is already ~4%. Profiling output was removed from the official candidate before the next checker. The smaller-frontier change still uses the standard valid bidirectional Dijkstra stopping condition and handles all special paths/weights correctly on the three test sets.
