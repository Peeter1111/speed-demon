#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <nmmintrin.h>
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
constexpr size_t REGION_WORDS = (REGION_CAP + 63) / 64;
constexpr size_t THREADS = 8;
constexpr size_t NAME_SLOTS = 1u << 17;
constexpr size_t NAME_MASK = NAME_SLOTS - 1;
constexpr uint32_t QCAP = 2048, QMASK = QCAP - 1;

alignas(64) std::atomic<uint64_t> name_entry[NAME_SLOTS];
alignas(64) uint64_t name_seen[THREADS][NAME_SLOTS / THREADS][4];
alignas(64) uint64_t region_bits[REGION_WORDS];
alignas(64) uint32_t region_rank[REGION_WORDS];
alignas(64) uint8_t region_group[200'000];

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

inline uint64_t hash_bytes(const unsigned char* p, uint32_t n) {
    uint64_t h = 0x9e3779b97f4a7c15ULL ^ static_cast<uint64_t>(n) * 0xd6e8feb86659fd93ULL;
    uint32_t left = n;
    while (left >= 8) {
        uint64_t x;
        std::memcpy(&x, p, 8);
        h ^= x;
        h *= 0xa0761d6478bd642fULL;
        h = (h << 27) | (h >> 37);
        p += 8;
        left -= 8;
    }
    uint64_t tail = 0;
    if (left) std::memcpy(&tail, p, left);
    h ^= tail ^ (static_cast<uint64_t>(left) << 56);
    h *= 0xe7037ed1a0b428dbULL;
    h ^= h >> 32;
    h *= 0x8ebc6af09c88c6e3ULL;
    h ^= h >> 29;
    return h;
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

inline uint32_t name_slot(uint64_t full) {
    uint64_t key = full ? full : 1;
    size_t pos = static_cast<size_t>(full) & NAME_MASK;
    for (;;) {
        auto& entry = name_entry[pos];
        uint64_t cur = entry.load(std::memory_order_acquire);
        if (!cur) {
            uint64_t expect = 0;
            if (entry.compare_exchange_strong(expect, key, std::memory_order_acq_rel))
                return static_cast<uint32_t>(pos);
            cur = expect;
        }
        if (cur == key) return static_cast<uint32_t>(pos);
        pos = (pos + THREADS) & NAME_MASK;
    }
}

// Atomic OR makes the distinct-country flag independent of parsing thread.
inline bool mark_direct(uint32_t slot, uint16_t group) {
    auto& word = name_seen[slot & (THREADS - 1)][slot / THREADS][group >> 6];
    uint64_t bit = 1ULL << (group & 63);
    return !(__atomic_fetch_or(&word, bit, __ATOMIC_RELAXED) & bit);
}

void parse_range(const MapFile& records, const unsigned char* p, const unsigned char* end,
                 uint8_t unmatched, Agg* agg, int tid) {
    // Work a little ahead to hide random interner-table cache misses.
    struct Pending { uint64_t hash; uint8_t group; };
    Pending batch[32];
    uint32_t used = 0;
    auto drain = [&] {
        for (uint32_t i = 0; i < used; ++i) {
            uint32_t slot = name_slot(batch[i].hash);
            if (mark_direct(slot, batch[i].group)) ++agg[batch[i].group].names_distinct;
        }
        used = 0;
    };
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
        uint8_t g = NONE;
        if (rid < REGION_CAP) {
            const uint32_t wi = rid >> 6;
            const uint64_t word = region_bits[wi];
            const uint64_t bit = 1ULL << (rid & 63);
            if (word & bit)
                g = region_group[region_rank[wi] + __builtin_popcountll(word & (bit - 1))];
        }
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
        uint64_t full = hash_bytes(name.raw, name.raw_len);
        __builtin_prefetch(&name_entry[static_cast<size_t>(full) & NAME_MASK], 0, 1);
        batch[used++] = {full, g};
        if (used == 32) drain();
    }
    drain();
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

    std::array<int16_t, 26 * 26 * 26> code_to_group;
    code_to_group.fill(-1);
    std::array<uint16_t, 256> group_code{};
    uint16_t groups = 0;
    uint32_t regions_used = 0;

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
        region_bits[id >> 6] |= 1ULL << (id & 63);
        region_group[regions_used++] = static_cast<uint8_t>(g);
    }
    if (groups > 255) {
        std::fputs("too many countries\n", stderr);
        return 3;
    }
    uint32_t rank = 0;
    for (size_t i = 0; i < REGION_WORDS; ++i) {
        region_rank[i] = rank;
        rank += static_cast<uint32_t>(__builtin_popcountll(region_bits[i]));
    }
    if (rank != regions_used) return 3;
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
        parse_range(records, p, end, unmatched, agg.data(), -1);
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
                                     unmatched, local[i].data(), static_cast<int>(i));
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
