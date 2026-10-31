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
