#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <immintrin.h>
#include <sched.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct Endpoint {
    const char *p;
    uint16_t len;
    uint32_t hash;
};

struct EndpointTable {
    std::array<std::atomic<uint16_t>, 8192> slots{};
    std::atomic<uint16_t> next{0};
    std::array<Endpoint, 2048> endpoints{};
};

static constexpr uint32_t ENDPOINT_STRIDE = 2048;
static constexpr uint32_t GROUPS = 72 * ENDPOINT_STRIDE;

static inline uint32_t fast_hash(const char *p, uint16_t n) {
    uint64_t a = 0, b = 0;
    if (n >= 8) {
        __builtin_memcpy(&a, p, 8);
        __builtin_memcpy(&b, p + n - 8, 8);
    } else {
        __builtin_memcpy(&a, p, n);
        b = a;
    }
    const uint32_t x = static_cast<uint32_t>(_mm_crc32_u64(n, a));
    const uint32_t y = static_cast<uint32_t>(_mm_crc32_u64(0, b));
    return x ^ ((y << 13) | (y >> 19));
}

static inline uint16_t intern_endpoint(EndpointTable &t, const char *p, uint16_t len) {
    const uint32_t h = fast_hash(p, len);
    uint32_t slot = h & (t.slots.size() - 1);
    for (;;) {
        uint16_t v = t.slots[slot].load(std::memory_order_acquire);
        if (!v) {
            uint16_t empty = 0;
            if (t.slots[slot].compare_exchange_strong(empty, 0xffffu, std::memory_order_acq_rel)) {
                const uint16_t id = t.next.fetch_add(1, std::memory_order_relaxed);
                t.endpoints[id] = {p, len, h};
                t.slots[slot].store(static_cast<uint16_t>(id + 1), std::memory_order_release);
                return id;
            }
            v = empty;
        }
        while (v == 0xffffu) v = t.slots[slot].load(std::memory_order_acquire);
        const uint16_t id = static_cast<uint16_t>(v - 1);
        const Endpoint &e = t.endpoints[id];
        if (e.hash == h && e.len == len && __builtin_memcmp(e.p, p, len) == 0) return id;
        slot = (slot + 1) & (t.slots.size() - 1);
    }
}

static inline uint16_t fast_lookup(const EndpointTable &t, const char *p, uint16_t len) {
    uint32_t slot = fast_hash(p, len) & (t.slots.size() - 1);
    for (;;) {
        const uint16_t v = t.slots[slot].load(std::memory_order_relaxed);
        const Endpoint &e = t.endpoints[v - 1];
        if (e.len == len && __builtin_memcmp(e.p, p, len) == 0) return static_cast<uint16_t>(v - 1);
        slot = (slot + 1) & (t.slots.size() - 1);
    }
}

static inline uint8_t hour_index(const char *p) {
    const unsigned hour = static_cast<unsigned>(p[11] - '0') * 10u + static_cast<unsigned>(p[12] - '0');
    if (p[6] == '0') return static_cast<uint8_t>(hour + (p[9] == '0' ? -18 : 6));
    return static_cast<uint8_t>(hour + (p[9] == '1' ? 30 : 54));
}

static inline const char *endpoint_end(const char *p, const char *end, const char *&ep) {
    ep = p + 32 + (p[28] == 'H') + 3 * (p[28] == 'D') + 4 * (p[28] == 'O') +
         (p[29] == 'O') + 2 * (p[29] == 'A');
    const char *q = ep;
    while (q + 32 <= end) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(q));
        const uint32_t m = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(v, _mm256_set1_epi8(' '))));
        if (m) { q += __builtin_ctz(m); return q; }
        q += 32;
    }
    while (*q != ' ') ++q;
    return q;
}

static void first_pass(const char *begin, const char *end, uint16_t *counts,
                       uint32_t *errors, EndpointTable &table) {
    const char *p = begin;
    while (p < end) {
        const uint8_t hour = hour_index(p);
        const char *ep;
        const char *q = endpoint_end(p, end, ep);
        const uint16_t len = static_cast<uint16_t>(q - ep);
        const uint16_t eid = intern_endpoint(table, ep, len);
        const bool error = q[1] == '5';
        q += 5; // SP + 3 status digits + SP
        while (*q != '\n') ++q;
        const uint32_t group = (static_cast<uint32_t>(hour) << 11) | eid;
        ++counts[group];
        if (error) __atomic_fetch_add(errors + group, 1u, __ATOMIC_RELAXED);
        p = q + 1;
    }
}

static void second_pass(const char *begin, const char *end, uint16_t *cur,
                        const uint32_t *offsets, uint32_t *latencies, const EndpointTable &table) {
    const char *p = begin;
    while (p < end) {
        const uint8_t hour = hour_index(p);
        const char *ep;
        const char *q = endpoint_end(p, end, ep);
        const uint16_t eid = fast_lookup(table, ep, static_cast<uint16_t>(q - ep));
        q += 5;
        uint32_t latency = 0;
        while (*q != '\n') latency = latency * 10u + static_cast<unsigned>(*q++ - '0');
        const uint32_t group = (static_cast<uint32_t>(hour) << 11) | eid;
        latencies[offsets[group] + cur[group]++] = latency;
        p = q + 1;
    }
}

static inline int endpoint_cmp(const char *ap, uint16_t an, const char *bp, uint16_t bn) {
    const size_t n = std::min<size_t>(an, bn);
    const int c = __builtin_memcmp(ap, bp, n);
    if (c) return c;
    return (an > bn) - (an < bn);
}

static inline void select3(uint32_t *a, uint32_t n, uint32_t &p50, uint32_t &p95, uint32_t &p99) {
    if (n <= 64) {
        std::sort(a, a + n);
        p50 = a[(50u * n + 99u) / 100u - 1u];
        p95 = a[(95u * n + 99u) / 100u - 1u];
        p99 = a[(99u * n + 99u) / 100u - 1u];
        return;
    }
    const uint32_t k50 = (50u * n + 99u) / 100u - 1u;
    const uint32_t k95 = (95u * n + 99u) / 100u - 1u;
    const uint32_t k99 = (99u * n + 99u) / 100u - 1u;
    std::nth_element(a, a + k50, a + n);
    p50 = a[k50];
    std::nth_element(a + k50 + 1, a + k95, a + n);
    p95 = a[k95];
    std::nth_element(a + k95 + 1, a + k99, a + n);
    p99 = a[k99];
}

static inline void append_u32(std::string &s, uint32_t v) {
    char b[10];
    const auto r = std::to_chars(b, b + sizeof b, v);
    s.append(b, r.ptr);
}

static int write_all(int fd, const char *p, size_t n) {
    while (n) {
        const ssize_t k = ::write(fd, p, n);
        if (k > 0) {
            p += k;
            n -= static_cast<size_t>(k);
        } else if (k < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *data_env = std::getenv("DATA_DIR");
    const char *out_env = std::getenv("OUT_DIR");
    const std::string data_dir = argc > 1 ? argv[1] : (data_env ? data_env : "/data");
    const std::string out_dir = argc > 2 ? argv[2] : (out_env ? out_env : "/out");
    const std::string input = data_dir + "/requests.log";
    const std::string output = out_dir + "/summary.txt";

    const int fd = ::open(input.c_str(), O_RDONLY);
    if (fd < 0) { std::perror("open requests.log"); return 2; }
    struct stat st {};
    if (::fstat(fd, &st) != 0) { std::perror("fstat"); ::close(fd); return 2; }
    const size_t size = static_cast<size_t>(st.st_size);
    const char *data = nullptr;
    if (size) {
        void *m = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m == MAP_FAILED) { std::perror("mmap"); ::close(fd); return 2; }
        data = static_cast<const char *>(m);
    }

    ::mkdir(out_dir.c_str(), 0755);
    if (!size) {
        const int ofd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (ofd < 0) { std::perror("open summary.txt"); ::close(fd); return 2; }
        ::close(ofd); ::close(fd); return 0;
    }

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    unsigned threads = 1;
    if (::sched_getaffinity(0, sizeof cpus, &cpus) == 0)
        threads = std::max(1, CPU_COUNT(&cpus));
    threads = std::min(threads, 12u);
    if (size < (1u << 20)) threads = 1;

    std::vector<const char *> bounds(threads + 1);
    bounds[0] = data;
    bounds[threads] = data + size;
    for (unsigned i = 1; i < threads; ++i) {
        const char *p = data + size * i / threads;
        while (p < data + size && *p != '\n') ++p;
        bounds[i] = p < data + size ? p + 1 : data + size;
    }

    EndpointTable endpoint_table;
    std::vector<uint16_t> local_stats(static_cast<size_t>(threads) * GROUPS);
    std::vector<uint32_t> errors(GROUPS);
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned i = 0; i < threads; ++i) {
        uint16_t *counts = local_stats.data() + static_cast<size_t>(i) * GROUPS;
        pool.emplace_back(first_pass, bounds[i], bounds[i + 1], counts,
                          errors.data(), std::ref(endpoint_table));
    }
    for (auto &t : pool) t.join();
    pool.clear();

    const uint32_t E = endpoint_table.next.load(std::memory_order_relaxed);
    std::vector<uint16_t> endpoint_order(E);
    for (uint16_t i = 0; i < E; ++i) endpoint_order[i] = i;
    std::sort(endpoint_order.begin(), endpoint_order.end(), [&](uint16_t a, uint16_t b) {
        const Endpoint &x = endpoint_table.endpoints[a], &y = endpoint_table.endpoints[b];
        return endpoint_cmp(x.p, x.len, y.p, y.len) < 0;
    });
    const uint32_t G = GROUPS;
    std::vector<uint32_t> offsets(G + 1);
    for (uint32_t g = 0; g < G; ++g) {
        uint32_t n = 0;
        for (unsigned t = 0; t < threads; ++t) n += local_stats[static_cast<size_t>(t) * G + g];
        offsets[g + 1] = offsets[g] + n;
    }

    for (uint32_t g = 0; g < G; ++g) {
        uint16_t pos = 0;
        for (unsigned t = 0; t < threads; ++t) {
            uint16_t &x = local_stats[static_cast<size_t>(t) * G + g];
            const uint16_t n = x;
            x = pos;
            pos = static_cast<uint16_t>(pos + n);
        }
    }
    std::vector<uint32_t> latencies(offsets[G]);
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back(second_pass, bounds[t], bounds[t + 1],
                          local_stats.data() + static_cast<size_t>(t) * G,
                          offsets.data(), latencies.data(), std::cref(endpoint_table));
    for (auto &t : pool) t.join();
    pool.clear();
    std::vector<uint16_t>().swap(local_stats);

    char hours[72][21];
    for (unsigned h = 0; h < 72; ++h) {
        unsigned month, day, hour;
        if (h < 6) { month = 10; day = 30; hour = h + 18; }
        else if (h < 30) { month = 10; day = 31; hour = h - 6; }
        else if (h < 54) { month = 11; day = 1; hour = h - 30; }
        else { month = 11; day = 2; hour = h - 54; }
        std::snprintf(hours[h], sizeof hours[h], "2026-%02u-%02uT%02u:00:00Z", month, day, hour);
    }

    std::atomic<uint32_t> next_hour{0};
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back([&] {
        for (;;) {
            const uint32_t h = next_hour.fetch_add(1, std::memory_order_relaxed);
            if (h >= 72) break;
            for (uint32_t k = 0; k < E; ++k) {
                const uint16_t e = endpoint_order[k];
                const uint32_t g = (h << 11) | e;
                const uint32_t n = offsets[g + 1] - offsets[g];
                if (!n) continue;
                uint32_t a50, a95, a99;
                select3(latencies.data() + offsets[g], n, a50, a95, a99);
            }
        }
    });
    for (auto &t : pool) t.join();

    const int ofd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) { std::perror("open summary.txt"); return 2; }
    int rc = 0;
    std::string out;
    out.reserve(180000);
    for (uint32_t h = 0; h < 72 && !rc; ++h) {
        out.clear();
        for (uint32_t k = 0; k < E; ++k) {
            const uint16_t e = endpoint_order[k];
            const uint32_t g = (h << 11) | e;
            const uint32_t n = offsets[g + 1] - offsets[g];
            if (!n) continue;
            const uint32_t *a = latencies.data() + offsets[g];
            const uint32_t a50 = a[(50u * n + 99u) / 100u - 1u];
            const uint32_t a95 = a[(95u * n + 99u) / 100u - 1u];
            const uint32_t a99 = a[(99u * n + 99u) / 100u - 1u];
            out.append(hours[h], 20); out.push_back(' ');
            out.append(endpoint_table.endpoints[e].p, endpoint_table.endpoints[e].len); out.push_back(' ');
            append_u32(out, n); out.push_back(' ');
            append_u32(out, a50); out.push_back(' ');
            append_u32(out, a95); out.push_back(' ');
            append_u32(out, a99); out.push_back(' ');
            append_u32(out, errors[g]); out.push_back(' ');
            append_u32(out, static_cast<uint32_t>(static_cast<uint64_t>(errors[g]) * 1000000u / n));
            out.push_back('\n');
        }
        if (write_all(ofd, out.data(), out.size())) rc = -1;
    }
    if (rc) std::perror("write summary.txt");
    ::close(ofd);
    ::munmap(const_cast<char *>(data), size);
    ::close(fd);
    return rc ? 2 : 0;
}
