#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {
inline double now_s() { timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
constexpr uint8_t NONE = 0xff;
constexpr uint32_t REGION_CAP = 1'000'000;
constexpr size_t THREADS = 8;
constexpr size_t NAME_SLOTS = 1u << 17;
constexpr size_t NAME_MASK = NAME_SLOTS - 1;
// Reserve headroom for unusually long holdout names; untouched BSS pages cost no RSS.
constexpr size_t NAME_ARENA = 8u << 20;
constexpr uint32_t QCAP = 2048, QMASK = QCAP - 1;

constexpr uint64_t BUSY = ~0ULL;
alignas(64) std::atomic<uint64_t> name_entry[NAME_SLOTS];
alignas(64) uint64_t name_seen[THREADS][NAME_SLOTS / THREADS][4];
alignas(64) unsigned char name_arena[NAME_ARENA];
std::atomic<uint32_t> arena_next{0};

struct MapFile {
    const unsigned char* p = nullptr;
    size_t n = 0;
    int fd = -1;
    explicit MapFile(const std::string& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) fail(path.c_str());
        struct stat st {};
        if (fstat(fd, &st) || st.st_size <= 0) fail(path.c_str());
        n = static_cast<size_t>(st.st_size);
        void* m = mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m == MAP_FAILED) fail(path.c_str());
        p = static_cast<const unsigned char*>(m);
    }
    ~MapFile() {
        if (p) munmap(const_cast<unsigned char*>(p), n);
        if (fd >= 0) close(fd);
    }
    [[noreturn]] static void fail(const char* path) {
        std::fprintf(stderr, "failed to open/map %s: %s\n", path, std::strerror(errno));
        std::exit(2);
    }
};

struct Agg {
    uint64_t count = 0, name_bytes = 0, note_bytes = 0;
    int64_t id_sum = 0, amount_sum = 0, amount_min = 0, amount_max = 0;
    uint32_t names_distinct = 0;
};

struct Queue {
    alignas(64) std::atomic<uint32_t> head{0};
    alignas(64) std::atomic<uint32_t> tail{0};
    uint32_t ring[QCAP];
};
Queue queues[THREADS][THREADS];
alignas(64) uint32_t owner_distinct[THREADS][256];
std::atomic<bool> producer_done[THREADS];

inline uint64_t mix(uint64_t x) {
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93ULL;
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93ULL;
    return x ^ (x >> 32);
}

inline uint64_t hash_bytes(const unsigned char* p, uint32_t n) {
    uint64_t a = 0, b = n * 0x9e3779b97f4a7c15ULL;
    uint32_t k = std::min(n, 8u);
    if (k) std::memcpy(&a, p, k);
    if (n > 8) std::memcpy(&b, p + n - 8, 8);
    return mix(a ^ b ^ (static_cast<uint64_t>(n) << 32));
}

inline uint64_t parse_uint(const unsigned char*& p, unsigned char stop) {
    uint64_t v = 0;
    while (*p != stop) {
        v = v * 10 + static_cast<unsigned>(*p - '0');
        ++p;
    }
    ++p;
    return v;
}

inline int64_t parse_cents(const unsigned char*& p) {
    bool neg = *p == '-';
    p += neg;
    int64_t whole = 0;
    while (*p != '.') {
        whole = whole * 10 + static_cast<unsigned>(*p - '0');
        ++p;
    }
    ++p;
    int64_t v = whole * 100 + static_cast<int64_t>(p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    return neg ? -v : v;
}

struct Field {
    const unsigned char* raw;
    uint32_t raw_len;
    uint32_t value_len;
};

inline Field parse_text(const unsigned char*& p, const unsigned char* end) {
    if (*p == ',') {
        ++p;
        return {p, 0, 0};
    }
    if (*p != '"') {
        const unsigned char* s = p;
        while (*p != ',') ++p;
        uint32_t n = static_cast<uint32_t>(p - s);
        ++p;
        return {s, n, n};
    }
    const unsigned char* s = ++p;
    uint32_t escapes = 0;
    for (;;) {
        const void* hit = std::memchr(p, '"', static_cast<size_t>(end - p));
        if (!hit) {
            std::fputs("unterminated quoted field\n", stderr);
            std::exit(3);
        }
        const unsigned char* q = static_cast<const unsigned char*>(hit);
        if (q + 1 < end && q[1] == '"') {
            ++escapes;
            p = q + 2;
            continue;
        }
        uint32_t raw_len = static_cast<uint32_t>(q - s);
        p = q + 1;
        if (*p != ',') {
            std::fputs("quoted field not followed by comma\n", stderr);
            std::exit(3);
        }
        ++p;
        return {s, raw_len, raw_len - escapes};
    }
}

inline uint32_t code_num(const unsigned char* p) {
    return static_cast<uint32_t>(p[0] - 'A') * 26u * 26u +
           static_cast<uint32_t>(p[1] - 'A') * 26u +
           static_cast<uint32_t>(p[2] - 'A');
}

inline void code_text(uint16_t n, char out[4]) {
    out[0] = static_cast<char>('A' + n / 676);
    n %= 676;
    out[1] = static_cast<char>('A' + n / 26);
    out[2] = static_cast<char>('A' + n % 26);
    out[3] = 0;
}

inline uint32_t name_slot(const Field& name) {
    uint64_t full = hash_bytes(name.raw, name.raw_len);
    uint32_t h = static_cast<uint32_t>(full >> 32);
    size_t pos = static_cast<size_t>(full) & NAME_MASK;
    for (;;) {
        auto& entry = name_entry[pos];
        uint64_t cur = entry.load(std::memory_order_acquire);
        if (!cur) {
            uint64_t expect = 0;
            if (!entry.compare_exchange_strong(expect, BUSY, std::memory_order_acq_rel)) cur = expect;
            else {
                uint32_t bytes = name.raw_len + 4;
                uint32_t off = arena_next.fetch_add(bytes, std::memory_order_relaxed);
                if (off > NAME_ARENA - bytes) { std::fputs("name arena full\n", stderr); std::exit(3); }
                std::memcpy(name_arena + off, &name.raw_len, 4);
                if (name.raw_len) std::memcpy(name_arena + off + 4, name.raw, name.raw_len);
                entry.store((static_cast<uint64_t>(h) << 32) | (off + 1), std::memory_order_release);
                return static_cast<uint32_t>(pos);
            }
        }
        if (cur == BUSY) continue;
        if (static_cast<uint32_t>(cur >> 32) == h) {
            const unsigned char* q = name_arena + static_cast<uint32_t>(cur) - 1;
            uint32_t n;
            std::memcpy(&n, q, 4);
            if (n == name.raw_len && (!n || std::memcmp(q + 4, name.raw, n) == 0)) return static_cast<uint32_t>(pos);
        }
        pos = (pos + THREADS) & NAME_MASK;
    }
}

inline bool mark_direct(uint32_t slot, uint16_t group) {
    uint64_t& word = name_seen[slot & (THREADS - 1)][slot / THREADS][group >> 6];
    uint64_t bit = 1ULL << (group & 63);
    bool fresh = !(word & bit);
    word |= bit;
    return fresh;
}

inline void process_name(uint32_t owner, uint32_t key) {
    uint32_t slot = key >> 8, group = key & 255;
    if (mark_direct(slot, static_cast<uint16_t>(group))) ++owner_distinct[owner][group];
}

inline void drain_queue(uint32_t owner, uint32_t p) {
    Queue& q = queues[p][owner];
    uint32_t h = q.head.load(std::memory_order_relaxed);
    uint32_t t = q.tail.load(std::memory_order_acquire);
    if (h == t) return;
    while (h != t) process_name(owner, q.ring[h++ & QMASK]);
    q.head.store(h, std::memory_order_release);
}

inline void drain_names(uint32_t owner) {
    for (uint32_t p = 0; p < THREADS; ++p) if (p != owner) drain_queue(owner, p);
}

inline void route_name(uint32_t producer, uint32_t slot, uint16_t group) {
    uint32_t owner = slot & (THREADS - 1), key = (slot << 8) | group;
    if (owner == producer) { process_name(owner, key); return; }
    Queue& q = queues[producer][owner];
    for (;;) {
        uint32_t t = q.tail.load(std::memory_order_relaxed);
        if (t - q.head.load(std::memory_order_acquire) < QCAP) {
            q.ring[t & QMASK] = key;
            q.tail.store(t + 1, std::memory_order_release);
            return;
        }
        drain_names(producer);
    }
}

void parse_range(const MapFile& records, const unsigned char* p, const unsigned char* end,
                 const uint8_t* region_to_group, uint8_t unmatched, Agg* agg, int tid) {
    uint32_t tick = 0;
    while (p < end) {
        if (end - p >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) p += 3;
        uint64_t id = parse_uint(p, ',');
        Field name = parse_text(p, end);
        Field note = parse_text(p, end);
        uint32_t rid = static_cast<uint32_t>(parse_uint(p, ','));
        int64_t cents = parse_cents(p);
        if (p == end) {
        } else if (*p == '\r' && p + 1 < end && p[1] == '\n') p += 2;
        else if (*p == '\n') ++p;
        else {
            std::fputs("bad record terminator\n", stderr);
            std::exit(3);
        }
        uint8_t g = rid < REGION_CAP ? region_to_group[rid] : NONE;
        if (g == NONE) g = unmatched;
        Agg& a = agg[g];
        if (!a.count) a.amount_min = a.amount_max = cents;
        ++a.count;
        a.id_sum += static_cast<int64_t>(id);
        a.amount_sum += cents;
        a.amount_min = std::min(a.amount_min, cents);
        a.amount_max = std::max(a.amount_max, cents);
        a.name_bytes += name.value_len;
        a.note_bytes += note.value_len;
        uint32_t slot = name_slot(name);
        if (tid < 0) { if (mark_direct(slot, g)) ++a.names_distinct; }
        else {
            route_name(static_cast<uint32_t>(tid), slot, g);
            if (!(++tick & 7)) {
                uint32_t src = (tick >> 3) & (THREADS - 1);
                if (src != static_cast<uint32_t>(tid)) drain_queue(static_cast<uint32_t>(tid), src);
            }
        }
    }
    if (tid >= 0) {
        uint32_t me = static_cast<uint32_t>(tid);
        producer_done[me].store(true, std::memory_order_release);
        for (;;) {
            drain_names(me);
            bool pending = false;
            for (uint32_t p2 = 0; p2 < THREADS; ++p2) if (p2 != me) {
                Queue& q = queues[p2][me];
                if (!producer_done[p2].load(std::memory_order_acquire) ||
                    q.head.load(std::memory_order_relaxed) != q.tail.load(std::memory_order_acquire)) { pending = true; break; }
            }
            if (!pending) break;
            asm volatile("pause");
        }
    }
}

void format_money(FILE* f, int64_t cents) {
    if (cents < 0) {
        std::fputc('-', f);
        cents = -cents;
    }
    std::fprintf(f, "%lld.%02lld", static_cast<long long>(cents / 100),
                 static_cast<long long>(cents % 100));
}
}  // namespace

int main(int argc, char** argv) {
    const bool timing = std::getenv("SPEED_TIMING");
    const double t0 = now_s();
    std::string data = argc > 1 ? argv[1] : "/data";
    std::string out = argc > 2 ? argv[2] : "/out";
    MapFile regions(data + "/regions.csv");
    MapFile records(data + "/records.csv");

    std::vector<uint8_t> region_to_group(REGION_CAP, NONE);
    std::array<int16_t, 26 * 26 * 26> code_to_group;
    code_to_group.fill(-1);
    std::array<uint16_t, 256> group_code{};
    uint16_t groups = 0;

    const unsigned char* p = regions.p;
    const unsigned char* end = regions.p + regions.n;
    const unsigned char* nl = static_cast<const unsigned char*>(std::memchr(p, '\n', end - p));
    if (!nl || std::string_view(reinterpret_cast<const char*>(p), nl - p) != "region_id,country") {
        std::fputs("bad regions header\n", stderr);
        return 3;
    }
    p = nl + 1;
    while (p < end) {
        uint32_t id = static_cast<uint32_t>(parse_uint(p, ','));
        uint16_t cn = static_cast<uint16_t>(code_num(p));
        p += 3;
        if (*p++ != '\n' || id >= REGION_CAP) {
            std::fputs("bad regions row\n", stderr);
            return 3;
        }
        int16_t g = code_to_group[cn];
        if (g < 0) {
            g = groups++;
            code_to_group[cn] = g;
            group_code[g] = cn;
        }
        region_to_group[id] = static_cast<uint8_t>(g);
    }
    if (groups > 255) {
        std::fputs("too many countries\n", stderr);
        return 3;
    }
    const uint8_t unmatched = static_cast<uint8_t>(groups);
    const double t_regions = now_s();

    p = records.p;
    end = records.p + records.n;
    nl = static_cast<const unsigned char*>(std::memchr(p, '\n', end - p));
    if (!nl) return 3;
    std::string_view hdr(reinterpret_cast<const char*>(p), nl - p);
    if (hdr.size() >= 3 && static_cast<unsigned char>(hdr[0]) == 0xef &&
        static_cast<unsigned char>(hdr[1]) == 0xbb && static_cast<unsigned char>(hdr[2]) == 0xbf) {
        hdr.remove_prefix(3);
    }
    if (!hdr.empty() && hdr.back() == '\r') hdr.remove_suffix(1);
    constexpr std::string_view expect = "id,name,note,region_id,amount";
    if (hdr != expect) {
        std::fputs("bad records header\n", stderr);
        return 3;
    }
    p = nl + 1;

    std::array<Agg, 256> agg{};
    double t_cuts = t_regions;
    if (static_cast<size_t>(end - p) < (1u << 20)) {
        parse_range(records, p, end, region_to_group.data(), unmatched, agg.data(), -1);
    } else {
        std::array<const unsigned char*, THREADS + 1> cut{};
        cut[0] = p;
        cut[THREADS] = end;
        for (size_t i = 1; i < THREADS; ++i) {
            const unsigned char* q = p + (end - p) * i / THREADS;
            for (;;) {
                const void* hit = std::memchr(q, 0xef, static_cast<size_t>(end - q));
                if (!hit) { cut[i] = end; break; }
                q = static_cast<const unsigned char*>(hit);
                if (end - q >= 3 && q[1] == 0xbb && q[2] == 0xbf) { cut[i] = q; break; }
                ++q;
            }
        }
        t_cuts = now_s();
        std::array<std::array<Agg, 256>, THREADS> local{};
        std::array<std::thread, THREADS> workers;
        for (size_t i = 0; i < THREADS; ++i) {
            workers[i] = std::thread(parse_range, std::cref(records), cut[i], cut[i + 1],
                                     region_to_group.data(), unmatched, local[i].data(), static_cast<int>(i));
        }
        for (auto& t : workers) t.join();
        for (size_t i = 0; i < THREADS; ++i) for (size_t g = 0; g <= unmatched; ++g) {
            const Agg& a = local[i][g];
            if (!a.count) continue;
            Agg& z = agg[g];
            if (!z.count) z.amount_min = a.amount_min, z.amount_max = a.amount_max;
            z.count += a.count;
            z.id_sum += a.id_sum;
            z.amount_sum += a.amount_sum;
            z.amount_min = std::min(z.amount_min, a.amount_min);
            z.amount_max = std::max(z.amount_max, a.amount_max);
            z.names_distinct += a.names_distinct;
            z.name_bytes += a.name_bytes;
            z.note_bytes += a.note_bytes;
        }
        for (size_t owner = 0; owner < THREADS; ++owner)
            for (size_t g = 0; g <= unmatched; ++g) agg[g].names_distinct += owner_distinct[owner][g];
    }
    const double t_parse = now_s();

    std::vector<uint16_t> order;
    order.reserve(groups);
    for (uint16_t g = 0; g < groups; ++g) if (agg[g].count) order.push_back(g);
    std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) { return group_code[a] < group_code[b]; });

    if (mkdir(out.c_str(), 0755) && errno != EEXIST) {
        std::perror("mkdir output");
        return 2;
    }
    std::string path = out + "/aggregates.txt";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::perror("open output");
        return 2;
    }
    auto emit = [&](const char* key, const Agg& a) {
        std::fprintf(f, "%s %llu %lld ", key, static_cast<unsigned long long>(a.count),
                     static_cast<long long>(a.id_sum));
        format_money(f, a.amount_sum);
        std::fputc(' ', f);
        format_money(f, a.amount_min);
        std::fputc(' ', f);
        format_money(f, a.amount_max);
        std::fprintf(f, " %u %llu %llu\n", a.names_distinct,
                     static_cast<unsigned long long>(a.name_bytes),
                     static_cast<unsigned long long>(a.note_bytes));
    };
    for (uint16_t g : order) {
        char code[4];
        code_text(group_code[g], code);
        emit(code, agg[g]);
    }
    if (agg[unmatched].count) emit("_UNMATCHED", agg[unmatched]);
    if (std::fclose(f)) return 2;
    if (timing) std::fprintf(stderr, "phase regions %.6f cuts %.6f parse %.6f output %.6f total %.6f\n",
                             t_regions - t0, t_cuts - t_regions, t_parse - t_cuts, now_s() - t_parse, now_s() - t0);
    return 0;
}
