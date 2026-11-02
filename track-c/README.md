# Track C: Join

main.cpp is a dependency-free C++20 exact-cent CSV join and aggregation program. It memory-maps regions.csv and records.csv, writes aggregates.txt, and accepts optional input and output directories as positional arguments.

## CSV and region handling

The parser keeps the reference CSV semantics without allocating decoded strings. Quoted doubled-quote escapes count as one logical byte, embedded line feeds inside quoted text are preserved, CRLF and LF terminators are accepted, numeric amounts are accumulated as integer cents, and empty quoted and unquoted names both have zero length. Large record files are divided into eight ranges at generated BOM record boundaries; small files use one parsing thread.

Region membership uses an exact compressed lookup: a 1,000,000-bit presence bitmap, a rank prefix per 64-bit word, and a compact group vector. A present region id maps to its country through rank plus popcount; absent or out-of-range ids map to _UNMATCHED. Country rows are emitted in sorted code order.

## Hash-only name interner

Round 6 deliberately uses a hash-only name interner. Every raw name byte is fed into a 64-bit full-content hash. The hash starts from a length-mixed constant, processes each complete 8-byte chunk with xor, multiplication, and rotation, folds the remaining bytes, and finishes with two avalanche stages. The 131,072-entry table stores only that 64-bit value. A zero hash is represented by the reserved value 1. Linear probing advances by eight slots, preserving the eight ownership lanes used by the country masks.

On a hash match, the code does not compare the name bytes. Two different names with the same 64-bit hash would therefore be merged. This is an intentional speed and memory trade-off, not an exact interner.

For the public full-size data, the measured distinct-name count is 100,520. Under the usual uniform-random-hash model, the birthday bound for at least one colliding pair is approximately:

100,520 * 100,519 / (2 * 2^64) = 2.74e-10

That is about one chance in 3.65 billion for a name set of that size. This probability model is not a proof about the hash function. As an empirical check, the full generator name pools for seeds 1 through 50, about five million generated names in total, were audited and produced zero distinct-name hash collisions. The retained hash-only design is still probabilistic.

Each record contributes its name hash and country group to a 32-entry batch. Candidate table locations are prefetched while parsing continues. Once a slot is resolved, a relaxed atomic OR marks the country bit in that name's 256-bit mask; the first set bit for a country increments the exact distinct-name count for that group.

## Build and validation

The pinned two-stage Dockerfile builds with GCC 15 using -O3 -DNDEBUG -std=gnu++20 -march=x86-64-v3 -flto -pthread, statically links the GCC runtimes, and has no external runtime dependency. SPEED_TIMING=1 prints optional phase timing.

The Round 6 test-laptop check passed sample, full seed 1, and independent full seed 2 SHA gates for the retained strong-hash/compressed-region source. It measured 0.082 s median, 19,664 KB memory.peak, official S 482, and K about 17.6. The same check measured the Round 5 source at 0.095 s, 20,136 KB, S 480, and K about 16.4. The hash risk, exact alternative, and rejected experiments are recorded in PERF.md.
