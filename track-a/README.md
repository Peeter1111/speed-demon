# Track A: Ingest

The solution memory-maps `requests.log`, splits the file on line boundaries across the CPUs available to the container (up to 12), and makes two parallel passes over the already-warm mapping. The fixed timestamp window is decoded with digit arithmetic, the seven HTTP methods give the endpoint start directly, and AVX2 finds the endpoint terminator.

Pass 1 interns the 1,842 endpoint byte strings in one shared 8,192-slot open-addressed table. The table uses a two-CRC32C head/tail hash only as an accelerator; endpoint equality always includes the exact length and `memcmp`, so hash collisions cannot change output. Each worker keeps 16-bit dense request counts for the fixed `(hour, endpoint-id)` grid, while rare 5xx counts go directly into one shared 32-bit array. The count grids are reduced into global prefix offsets and then reused as per-worker relative scatter cursors.

Pass 2 rescans the mmap, looks up the endpoint in the now-read-only catalogue, parses latency, and scatters each latency directly into its final contiguous group range. This avoids the round-1 8-byte-per-request intermediate record array. After the pass, the worker count grids are released; the dominant payload left resident is the 3.1 million-element `uint32_t` latency array.

Percentile work is parallelised by hour. Groups of at most 64 values are fully sorted; larger groups use three narrowed `std::nth_element` selections for exact nearest-rank p50/p95/p99. Output is then streamed one hour at a time through a single reusable string buffer in byte-sorted endpoint order, avoiding the old ~10 MB set of retained hour strings.

## Dependencies and invocation

There are no non-standard runtime dependencies. The build uses GCC C++20 and Linux/POSIX interfaces from the allowlisted images; `.speed-demon.toml` declares zero direct dependencies (contest D = 1). The binary defaults to `/data/requests.log` and `/out/summary.txt`; positional arguments override the data and output directories, and `DATA_DIR` / `OUT_DIR` are also accepted.

The implementation is tuned for the official Track A generator: exactly 1,842 endpoints, the fixed 72-hour span, and generated group sizes that fit each worker's 16-bit local count cell. Aggregate offsets and error counts remain 32-bit. Endpoint bytes, output ordering, 5xx classification, duplicate preservation, and nearest-rank percentile semantics remain exact.

## With another 24 hours

The next serious experiment would be a persistent worker/barrier design that removes two thread-create/join cycles without retaining extra payload memory. I would also prototype a compact exact scatter-counter representation and a radix-selection kernel for the few very large groups. The round-2 log is a useful warning: endpoint side streams, extra fingerprinting, thread caps, branch hints, and apparently cheaper output paths all lost on the official Acer check.
