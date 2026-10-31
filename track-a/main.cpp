// Speed Demon 2026, Track A (Ingest).
//
// Two phases, both on every hardware thread:
//   1. Parse. The mapped log is cut into newline-aligned chunks that threads take from a counter. Each line is
//      read once: the hour comes from fixed byte offsets, the endpoint is found with one AVX2 compare and interned
//      in the thread's own table, and a 6-byte record (endpoint id + 5xx flag, latency) goes into that thread's
//      block chain for the hour.
//   2. Report. After a barrier merges the endpoint tables into one byte-sorted list, threads take hours from a
//      counter. An hour's records (about 43,000, cache resident) are counting-sorted by endpoint, the three
//      nearest-rank values are selected per group, and the hour's lines are formatted into one buffer. The main
//      thread writes the buffers in hour order as they complete.
#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <immintrin.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int kHours = 72;        // the spec fixes the span: 2026-10-30T18:00Z to 2026-11-02T17:59Z
constexpr int kSlots = 8192;      // endpoint hash table size; each seed has 1,842 endpoints
constexpr int kMaxEndpoints = 6000;
constexpr int kBlock = 1024;      // records per block
constexpr int kMaxLine = 220;     // longest possible line (125-byte endpoint); below this distance from the end
                                  // of the mapping, wide loads are replaced by bounded scans

[[noreturn]] void die(const char* what) { fprintf(stderr, "track a: %s\n", what); exit(1); }

uint64_t load8(const char* p) { uint64_t v; memcpy(&v, p, 8); return v; }

// Hashes the first, middle and last 8 bytes plus the length; the table always confirms a match with memcmp.
uint64_t hash_bytes(const char* p, uint32_t n) {
    uint64_t a = n >= 8 ? load8(p) : load8(p) & (~0ull >> (64 - 8 * n));
    uint64_t b = n >= 8 ? load8(p + n - 8) : 0, c = n > 16 ? load8(p + n / 2 - 4) : 0;
    uint64_t h = a * 0x9E3779B97F4A7C15ull ^ b * 0xC2B2AE3D27D4EB4Full ^ c * 0x165667B19E3779F9ull ^ n;
    return h ^ h >> 29;
}

struct Endpoint { const char* p; uint32_t n; };

// Open-addressing intern table: a slot holds the hash with the low 16 bits replaced by id + 1.
struct Table {
    std::vector<uint64_t> tag = std::vector<uint64_t>(kSlots);
    std::vector<Endpoint> ep;
    uint32_t id(const char* p, uint32_t n) {
        uint64_t h = hash_bytes(p, n);
        for (uint32_t s = h >> 51;; s = (s + 1) & (kSlots - 1)) {
            uint64_t t = tag[s];
            if (!t) {
                if (ep.size() == kMaxEndpoints) die("more distinct endpoints than the table holds");
                ep.push_back({p, n});
                tag[s] = (h & ~0xFFFFull) | ep.size();
                return ep.size() - 1;
            }
            const Endpoint& e = ep[(t & 0xFFFF) - 1];
            if (((t ^ h) >> 16) == 0 && e.n == n && memcmp(e.p, p, n) == 0) return (t & 0xFFFF) - 1;
        }
    }
};

struct Block { Block* next; uint32_t n; uint16_t key[kBlock]; uint32_t lat[kBlock]; };

struct Worker {
    Table names;                  // this thread's endpoints, in first-seen order
    Block* head[kHours] = {};     // newest block of each hour's chain
    std::vector<uint16_t> global; // local endpoint id -> rank in the byte-sorted global list
};

void parse(Worker& w, const char* p, const char* end, const char* map_end) {
    const __m256i space = _mm256_set1_epi8(' ');
    while (p < end) {
        // Timestamps are exactly 27 bytes: day at offset 8, hour at 11. Days 30, 31, 01, 02 are 0..3.
        int day = (p[8] - '0') * 10 + (p[9] - '0');
        int hour = (day >= 30 ? day - 30 : day + 1) * 24 + (p[11] - '0') * 10 + (p[12] - '0') - 18;
        if (unsigned(hour) >= kHours) die("timestamp outside the 72-hour span");
        // The method (3 to 7 letters) starts at offset 28; its end is the first zero byte of word ^ "        ".
        uint64_t m = load8(p + 28) ^ 0x2020202020202020ull;
        const char* e = p + 29 + (__builtin_ctzll((m - 0x0101010101010101ull) & ~m & 0x8080808080808080ull) >> 3);
        const char* q = e;
        if (map_end - e > kMaxLine) {
            uint32_t mask;
            while (!(mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i*)q), space))))
                q += 32;
            q += __builtin_ctz(mask);
        } else {
            while (*q != ' ') ++q;
        }
        uint32_t id = w.names.id(e, q - e);
        uint16_t key = id | (q[1] == '5') << 15;        // status is three digits; 5xx is an error
        const char* d = q + 5;
        uint32_t lat = *d++ - '0';
        while (*d != '\n') lat = lat * 10 + (*d++ - '0');
        p = d + 1;
        Block* b = w.head[hour];
        if (!b || b->n == kBlock) {
            Block* fresh = static_cast<Block*>(malloc(sizeof(Block)));
            fresh->next = b, fresh->n = 0, w.head[hour] = b = fresh;
        }
        b->key[b->n] = key, b->lat[b->n++] = lat;
    }
}

char* put(char* o, uint64_t v) {
    char tmp[20];
    int i = 20;
    do tmp[--i] = char('0' + v % 10); while (v /= 10);
    memcpy(o, tmp + i, 20 - i);
    return o + 20 - i;
}

struct Output { std::atomic<bool> ready{false}; char* buf = nullptr; size_t len = 0; };

// Builds hour h's lines from every worker's records for that hour, freeing the blocks as it goes.
void report(int h, std::vector<std::unique_ptr<Worker>>& ws, const std::vector<Endpoint>& sorted,
            std::vector<uint32_t>& count, std::vector<uint32_t>& errs, std::vector<uint32_t>& lat, Output& out) {
    std::fill(count.begin(), count.end(), 0);
    std::fill(errs.begin(), errs.end(), 0);
    size_t total = 0;
    for (auto& w : ws)
        for (Block* b = w->head[h]; b; b = b->next)
            for (uint32_t i = 0; i < b->n; ++i) {
                uint32_t g = w->global[b->key[i] & 0x7FFF];
                count[g]++, errs[g] += b->key[i] >> 15;
            }
    // count becomes each group's end offset after the scatter below; begin = end - size.
    std::vector<uint32_t> size(count);
    uint32_t groups = 0;
    for (size_t g = 0; g < count.size(); ++g) total += count[g], count[g] = total - count[g], groups += size[g] > 0;
    if (lat.size() < total) lat.resize(total);
    for (auto& w : ws)
        for (Block* b = w->head[h], *next; b; b = next) {
            for (uint32_t i = 0; i < b->n; ++i) lat[count[w->global[b->key[i] & 0x7FFF]]++] = b->lat[i];
            next = b->next;
            free(b);
        }
    int t = h + 18, day = t / 24;
    char stamp[22];
    snprintf(stamp, sizeof stamp, "2026-%s-%02dT%02d:00:00Z ", day < 2 ? "10" : "11", day < 2 ? 30 + day : day - 1,
             t % 24);
    char* o = out.buf = static_cast<char*>(malloc(size_t(groups) * (21 + 125 + 6 * 21) + 1));
    for (size_t g = 0; g < size.size(); ++g) {
        uint32_t n = size[g];
        if (!n) continue;
        uint32_t* v = lat.data() + count[g] - n;
        uint32_t r50 = (50 * n + 99) / 100, r95 = (95 * n + 99) / 100, r99 = (99 * n + 99) / 100;
        if (n <= 32) {
            std::sort(v, v + n);
        } else {  // each selection only needs the part left of the previous one
            std::nth_element(v, v + r99 - 1, v + n);
            std::nth_element(v, v + r95 - 1, v + r99 - 1);
            std::nth_element(v, v + r50 - 1, v + r95 - 1);
        }
        memcpy(o, stamp, 21), o += 21;
        memcpy(o, sorted[g].p, sorted[g].n), o += sorted[g].n;
        *o++ = ' ', o = put(o, n), *o++ = ' ', o = put(o, v[r50 - 1]), *o++ = ' ', o = put(o, v[r95 - 1]);
        *o++ = ' ', o = put(o, v[r99 - 1]), *o++ = ' ', o = put(o, errs[g]);
        *o++ = ' ', o = put(o, uint64_t(errs[g]) * 1000000 / n), *o++ = '\n';
    }
    out.len = o - out.buf;
    out.ready.store(true, std::memory_order_release);
    out.ready.notify_one();
}

}  // namespace

int main(int argc, char** argv) {
    const char* env_data = getenv("DATA_DIR");
    const char* env_out = getenv("OUT_DIR");
    std::string data = argc > 1 ? argv[1] : env_data ? env_data : "/data";
    std::string outdir = argc > 2 ? argv[2] : env_out ? env_out : "/out";
    int in = open((data + "/requests.log").c_str(), O_RDONLY);
    int fd = open((outdir + "/summary.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in < 0 || fd < 0) die("cannot open requests.log or summary.txt");
    struct stat st;
    if (fstat(in, &st) != 0) die("cannot stat requests.log");
    size_t size = st.st_size;
    if (size == 0) return close(fd) == 0 ? 0 : 1;  // an empty log gives an empty summary
    const char* map = static_cast<const char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, in, 0));
    if (map == MAP_FAILED) die("cannot map requests.log");
    if (map[size - 1] != '\n') die("requests.log does not end with a newline");

    cpu_set_t cpus;
    int threads = sched_getaffinity(0, sizeof cpus, &cpus) == 0 ? CPU_COUNT(&cpus) : 1;
    threads = std::clamp(threads, 1, 64);
    int chunks = threads * 8;
    std::vector<const char*> cut(chunks + 1, map + size);
    cut[0] = map;
    for (int i = 1; i < chunks; ++i) {
        const char* c = std::max(cut[i - 1], map + size * i / chunks);
        cut[i] = c == map + size ? c : static_cast<const char*>(memchr(c, '\n', map + size - c)) + 1;
    }

    std::vector<std::unique_ptr<Worker>> ws;
    for (int i = 0; i < threads; ++i) ws.push_back(std::make_unique<Worker>());
    std::vector<Endpoint> sorted;
    // Runs once, between the phases: one byte-sorted endpoint list, and each worker's ids mapped to its ranks.
    auto merge = [&]() noexcept {
        Table all;
        for (auto& w : ws)
            for (const Endpoint& e : w->names.ep) all.id(e.p, e.n);
        sorted = all.ep;
        std::sort(sorted.begin(), sorted.end(), [](const Endpoint& a, const Endpoint& b) {
            int c = memcmp(a.p, b.p, std::min(a.n, b.n));
            return c ? c < 0 : a.n < b.n;  // a proper prefix sorts first
        });
        Table order;
        for (size_t r = 0; r < sorted.size(); ++r) order.id(sorted[r].p, sorted[r].n);  // id == r
        for (auto& w : ws)
            for (const Endpoint& e : w->names.ep) w->global.push_back(order.id(e.p, e.n));
    };
    std::barrier sync(threads, merge);
    std::atomic<int> next_chunk{0}, next_hour{0};
    std::vector<Output> outs(kHours);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (int c; (c = next_chunk.fetch_add(1)) < chunks;) parse(*ws[t], cut[c], cut[c + 1], map + size);
            sync.arrive_and_wait();
            std::vector<uint32_t> count(sorted.size()), errs(sorted.size()), lat;
            for (int h; (h = next_hour.fetch_add(1)) < kHours;) report(h, ws, sorted, count, errs, lat, outs[h]);
        });
    for (Output& o : outs) {
        o.ready.wait(false, std::memory_order_acquire);
        for (size_t done = 0; done < o.len;) {
            ssize_t k = write(fd, o.buf + done, o.len - done);
            if (k <= 0) die("write to summary.txt failed");
            done += k;
        }
        free(o.buf);
    }
    for (auto& t : pool) t.join();
    return close(fd) == 0 ? 0 : 1;
}
