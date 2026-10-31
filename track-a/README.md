# Track A: Ingest (round 3)

The program maps `/data/requests.log` read-only, splits it into newline-aligned partitions, and executes two parallel input passes. Its byte-exact output is `/out/summary.txt`. The provided Dockerfile builds with GCC C++20, `-O3 -march=x86-64-v3 -flto`, and pinned, allowlisted build/runtime images. There are **zero non-standard runtime dependencies**.

In pass 1, worker threads decode the fixed Track A UTC-hour window using digit arithmetic, skip the seven fixed HTTP method spellings using byte comparisons, find the endpoint delimiter with AVX2, and intern each *complete* endpoint byte string in one shared 8,192-slot atomic table. Two hardware CRC32C operations accelerate hashing, but exact length and byte comparisons decide identity even for collisions. Each worker keeps a dense, 16-bit per-group count array; rare 5xx counts use one shared 32-bit atomic error array. The latency tail is skipped using a 16-byte SSE newline mask, except at the last mapped bytes where a scalar fallback avoids any out-of-bounds load.

The main thread reduces the count arrays and calculates the scatter ranges. Pass 2 scans the mapping again, now doing read-only endpoint lookup and decimal latency decoding; each worker scatters latencies directly into its non-overlapping portion of the output groups. The same local count cells become its relative scatter cursors. This costs much less memory than retaining an eight-byte record for every request.

Finally, workers **select the three exact nearest-rank percentiles and format complete hours concurrently**. Small groups use `std::sort`; larger groups use three bounded `std::nth_element` calls. The main thread writes completed hours in required order, with release/acquire readiness flags and a cap of two pending hours per worker. Output stays streaming instead of retaining the full 10 MB result. The format overlaps percentile work with disk writes.

The file and output directories can be overridden by positional arguments or `DATA_DIR`/`OUT_DIR`; defaults are `/data` and `/out`. On an empty input the binary writes an empty summary. The implementation is specialized to the official generator's fixed 72-hour window, 1,842 endpoints, and traffic counts fitting 16-bit **per-worker** group cells; global counts and errors are 32-bit. It does not truncate endpoints, treat 499 as 5xx, discard duplicate rows, or use approximate percentiles.

## Evidence

The lead's Acer checker accepts seed-1 sample, seed-1 full, **independent seed-2 full**, and our separately generated seeds 7 and 11 match the official Python oracle at sample scale. All kept and reverted experiments, timings and memory peaks are in `PERF.md`; phase profiles and a separate mmap byte-sum floor are there too. Acer is a noisy eight-thread laptop and is not the competition's twelve-thread scoring box. We report the whole five-run distribution instead of presenting one fast run as a speed guarantee.

## Another 24 hours

The most promising larger change would be a **bounded in-memory chunk pipeline** that overlaps reading and group reduction without paying for 3.1 million retained 8-byte request records. Round 3's direct one-pass record materialization lost badly (0.109 s/42.5 MB). A strong alternative is to eliminate the shared hot endpoint-table probes using an exact, precomputed dictionary for *each seed* without assuming fixed endpoints; its construction cost must be included. Another useful experiment is a radix percentile selector for the handful of large groups. Tuning the endpoint-table capacity, reusing barrier-synchronized threads and adding a SWAR latency parser all regressed, as recorded.
