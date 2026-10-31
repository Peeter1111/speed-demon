#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using U32 = uint32_t;
using U64 = uint64_t;
constexpr U64 MASK = (U64{1} << 53) - 1;
constexpr U64 INF = MASK;
constexpr U32 NODE_MASK = (1U << 21) - 1, START_MASK = (1U << 23) - 1, BIG_FLAG = 1U << 18;

struct Mapping {
    int fd = -1;
    const char* data = nullptr;
    size_t size = 0;
    explicit Mapping(const std::string& name) {
        fd = open(name.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("open: " + name);
        struct stat st{};
        if (fstat(fd, &st) || st.st_size == 0) throw std::runtime_error("stat: " + name);
        size = static_cast<size_t>(st.st_size);
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) throw std::runtime_error("mmap: " + name);
        data = static_cast<const char*>(p);
    }
    ~Mapping() {
        if (data) munmap(const_cast<char*>(data), size);
        if (fd >= 0) close(fd);
    }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
};

// Input format is fixed ASCII decimal with one space between fields, final LF.
static inline U32 number(const char*& p) {
    U32 result = 0;
    while (static_cast<unsigned>(*p - '0') <= 9) {
        result = result * 10 + static_cast<U32>(*p - '0');
        ++p;
    }
    ++p;  // space or newline
    return result;
}

struct __attribute__((packed)) Edge { U32 low; uint8_t high; };
struct Graph {
    U32 n;
    std::vector<U32> start[2];
    std::vector<Edge> edges[2];
    std::vector<U32> big[2];

    explicit Graph(const std::string& path) {
        Mapping file(path);
        const char* p = file.data;
        n = number(p);
        U32 m = number(p);
        const char* end = file.data + file.size;
        unsigned parts = m >= 500000 ? 12 : 1;
        std::vector<const char*> cuts(parts + 1);
        cuts[0] = p;
        cuts[parts] = end;
        for (unsigned i = 1; i < parts; ++i) {
            auto cursor = p + (end - p) * i / parts;
            while (cursor[-1] != '\n') ++cursor;
            cuts[i] = cursor;
        }
        constexpr U32 WIDE = (1U << 22) - 1;
        std::vector<std::vector<U64>> chunks(parts);
        std::vector<std::vector<U32>> wide(parts);
        auto parse = [&](unsigned i) {
            const char* at = cuts[i];
            auto& dst = chunks[i];
            dst.reserve(static_cast<size_t>(m) / parts + 8192);
            while (at < cuts[i + 1]) {
                U32 u = number(at), v = number(at), w = number(at);
                U32 small = w < WIDE ? w : WIDE;
                dst.push_back(U64(u) | (U64(v) << 21) | (U64(small) << 42));
                if (small == WIDE) wide[i].push_back(w);
            }
        };
        if (parts == 1) parse(0);
        else {
            std::vector<std::thread> readers;
            for (unsigned i = 1; i < parts; ++i) readers.emplace_back(parse, i);
            parse(0);
            for (auto& t : readers) t.join();
        }
        size_t count = 0;
        for (auto& x : chunks) count += x.size();
        if (count != m) throw std::runtime_error("bad edge count");
        for (auto& x : start) x.assign(static_cast<size_t>(n) + 1, 0);
        for (const auto& chunk : chunks) for (U64 e : chunk)
            ++start[0][(static_cast<U32>(e) & NODE_MASK) + 1];
        for (const auto& chunk : chunks) for (U64 e : chunk)
            ++start[1][(static_cast<U32>(e >> 21) & NODE_MASK) + 1];
        for (auto& x : start)
            for (U32 i = 1; i <= n; ++i) x[i] += x[i - 1];
        auto next_f = start[0], next_b = start[1];
        auto edge = [&](int side, U32 to, U32 w) {
            U32 field = w < BIG_FLAG ? w : BIG_FLAG | static_cast<U32>(big[side].size());
            if (w >= BIG_FLAG) big[side].push_back(w);
            U64 code = U64(to) | (U64(field) << 21);
            return Edge{static_cast<U32>(code), static_cast<uint8_t>(code >> 32)};
        };
        edges[0].resize(static_cast<size_t>(m) + 1);
        for (unsigned i = 0; i < parts; ++i) {
            size_t bi = 0;
            for (U64 e : chunks[i]) {
                U32 u = static_cast<U32>(e) & NODE_MASK, v = static_cast<U32>(e >> 21) & NODE_MASK;
                U32 w = static_cast<U32>(e >> 42); if (w == WIDE) w = wide[i][bi++];
                edges[0][next_f[u]++] = edge(0, v, w);
            }
        }
        edges[1].resize(static_cast<size_t>(m) + 1);
        for (unsigned i = 0; i < parts; ++i) {
            size_t bi = 0;
            for (U64 e : chunks[i]) {
                U32 u = static_cast<U32>(e) & NODE_MASK, v = static_cast<U32>(e >> 21) & NODE_MASK;
                U32 w = static_cast<U32>(e >> 42); if (w == WIDE) w = wide[i][bi++];
                edges[1][next_b[v]++] = edge(1, u, w);
            }
        }
        for (auto& x : start) {
            for (U32 u = 0; u < n; ++u) x[u] |= (x[u + 1] - x[u]) << 23;
            x.resize(n);
        }
    }
};

struct Query { U32 s, t; };
struct __attribute__((packed)) Item { U64 d; U32 v; };
// Compact binary heap with stale entries removed on pop.
struct Heap {
    std::vector<Item> a;
    Heap() { a.reserve(256); }
    bool empty() const { return a.empty(); }
    U64 min() const { return a.front().d; }
    void clear() { a.clear(); }
    void push(U64 d, U32 v) {
        size_t i = a.size();
        a.push_back({d, v});
        while (i) {
            size_t parent = (i - 1) / 2;
            if (a[parent].d <= d) break;
            a[i] = a[parent];
            i = parent;
        }
        a[i] = {d, v};
    }
    Item pop() {
        Item top = a.front();
        Item tail = a.back();
        a.pop_back();
        if (a.empty()) return top;
        size_t i = 0, size = a.size();
        while (2 * i + 1 < size) {
            size_t first = 2 * i + 1, best = first;
            if (first + 1 < size && a[first + 1].d < a[first].d) best = first + 1;
            if (tail.d <= a[best].d) break;
            a[i] = a[best];
            i = best;
        }
        a[i] = tail;
        return top;
    }
};

struct SparseDist {
    static constexpr U32 BITS = 16, SIZE = 1U << BITS, MASKI = SIZE - 1;
    uint16_t* value = static_cast<uint16_t*>(malloc(SIZE * sizeof(uint16_t)));
    U64* large = static_cast<U64*>(malloc(SIZE * sizeof(U64)));
    U32* tag = static_cast<U32*>(calloc(SIZE, sizeof(U32)));
    SparseDist() { if (!value || !large || !tag) throw std::bad_alloc(); }
    ~SparseDist() { free(value); free(large); free(tag); }
    SparseDist(const SparseDist&) = delete;
    SparseDist& operator=(const SparseDist&) = delete;
    static U32 hash(U32 node) { return node & MASKI; }
    U64 get(U32 node, U32 epoch) const {
        U32 want = (epoch << 21) | node, i = hash(node);
        for (;;) {
            U32 seen = tag[i];
            if (seen == want) return value[i] == UINT16_MAX ? large[i] : value[i];
            if ((seen >> 21) != epoch) return INF;
            i = (i + 1) & MASKI;
        }
    }
    void put(U32 node, U32 epoch, U64 d) {
        U32 want = (epoch << 21) | node, i = hash(node);
        while ((tag[i] >> 21) == epoch && tag[i] != want) i = (i + 1) & MASKI;
        tag[i] = want;
        if (d < UINT16_MAX) value[i] = static_cast<uint16_t>(d);
        else { value[i] = UINT16_MAX; large[i] = d; }
    }
    bool improve(U32 node, U32 epoch, U64 d) {
        U32 want = (epoch << 21) | node, i = hash(node);
        for (;;) {
            U32 seen = tag[i];
            if ((seen >> 21) != epoch) {
                tag[i] = want;
                if (d < UINT16_MAX) value[i] = static_cast<uint16_t>(d);
                else { value[i] = UINT16_MAX; large[i] = d; }
                return true;
            }
            if (seen == want) {
                U64 old = value[i] == UINT16_MAX ? large[i] : value[i];
                if (d >= old) return false;
                if (d < UINT16_MAX) value[i] = static_cast<uint16_t>(d);
                else { value[i] = UINT16_MAX; large[i] = d; }
                return true;
            }
            i = (i + 1) & MASKI;
        }
    }
    void reset() { std::memset(tag, 0, SIZE * sizeof(U32)); }
};

struct Solver {
    const Graph& graph;
    SparseDist dist[2];
    Heap heap[2];
    U32 epoch = 0;
    explicit Solver(const Graph& g): graph(g) {}
    U64 get(int side, U32 node) const {
        return dist[side].get(node, epoch);
    }
    void put(int side, U32 node, U64 value) {
        dist[side].put(node, epoch, value);
    }
    int64_t run(U32 s, U32 t) {
        if (s == t) return 0;
        if (!(graph.start[0][s] >> 23) || !(graph.start[1][t] >> 23)) return -1;
        if (++epoch == 2048) {
            for (auto& d : dist) d.reset();
            epoch = 1;
        }
        heap[0].clear(); heap[1].clear();
        put(0, s, 0); put(1, t, 0);
        heap[0].push(0, s); heap[1].push(0, t);
        U64 best = INF;
        // Alternate directions rather than choosing the smaller distance:
        // a 2^32-weight source must not force a full reverse-graph scan.
        int side = 0;
        while (!heap[0].empty() && !heap[1].empty()) {
            if (best != INF && heap[0].min() + heap[1].min() >= best) break;
            Heap& q = heap[side];
            auto [d, u] = q.pop();
            if (d != get(side, u)) { side ^= 1; continue; }
            U64 other = get(side ^ 1, u);
            if (other != INF && d + other < best) best = d + other;
            U32 range = graph.start[side][u], begin = range & START_MASK, end = begin + (range >> 23);
            const auto* raw = reinterpret_cast<const unsigned char*>(graph.edges[side].data());
            for (U32 k = begin; k < end; ++k) {
                U64 code; std::memcpy(&code, raw + sizeof(Edge) * k, sizeof(code));
                code &= (U64{1} << 40) - 1;
                U32 v = static_cast<U32>(code) & NODE_MASK, w = static_cast<U32>(code >> 21);
                if (w & BIG_FLAG) w = graph.big[side][w & (BIG_FLAG - 1)];
                U64 nd = d + w;
                if (nd >= best || !dist[side].improve(v, epoch, nd)) continue;
                q.push(nd, v);
                U64 opposite = get(side ^ 1, v);
                if (opposite != INF && nd + opposite < best) best = nd + opposite;
            }
            side ^= 1;
        }
        return best == INF ? -1 : static_cast<int64_t>(best);
    }
};

static void write_answers(const std::string& path, const std::vector<int64_t>& results) {
    FILE* out = fopen(path.c_str(), "wb");
    if (!out) throw std::runtime_error("open output: " + path);
    for (auto x : results)
        if (fprintf(out, "%lld\n", static_cast<long long>(x)) < 0)
            throw std::runtime_error("write output");
    if (fclose(out) != 0) throw std::runtime_error("close output");
}

int main(int argc, char** argv) {
    try {
        const char* data = argc > 1 ? argv[1] : "/data";
        const char* output = argc > 2 ? argv[2] : "/out";
        auto start = std::chrono::steady_clock::now();
        Graph g(std::string(data) + "/graph.txt");
        auto parsed = std::chrono::steady_clock::now();
        Mapping file(std::string(data) + "/queries.txt");
        std::vector<Query> queries;
        const char* p = file.data;
        while (p < file.data + file.size) queries.push_back({number(p), number(p)});
        std::vector<int64_t> answers(queries.size());
        std::vector<U32> jobs, alias(queries.size(), ~U32{0});
        for (U32 i = 0; i < queries.size(); ++i) {
            U32 j = 0;
            for (; j < i; ++j) if (queries[i].s == queries[j].s && queries[i].t == queries[j].t) break;
            if (j == i) jobs.push_back(i); else alias[i] = j;
        }
        std::atomic<size_t> next{0};
        // Eight workers beat twelve on the available x86 benchmark (also use less RSS).
        unsigned threads = 8;
        if (const char* v = getenv("SD_THREADS")) threads = std::max(1, atoi(v));
        threads = std::min<unsigned>(threads, std::max<size_t>(1, jobs.size()));
        auto work = [&]() {
            Solver solver(g);
            while (true) {
                size_t j = next.fetch_add(1, std::memory_order_relaxed);
                if (j >= jobs.size()) break;
                U32 i = jobs[j]; answers[i] = solver.run(queries[i].s, queries[i].t);
            }
        };
        std::vector<std::thread> workers;
        for (unsigned i = 1; i < threads; ++i) workers.emplace_back(work);
        work();
        for (auto& t : workers) t.join();
        for (U32 i = 0; i < alias.size(); ++i) if (alias[i] != ~U32{0}) answers[i] = answers[alias[i]];
        auto done = std::chrono::steady_clock::now();
        write_answers(std::string(output) + "/distances.txt", answers);
        if (getenv("SD_PROFILE")) {
            using Sec = std::chrono::duration<double>;
            fprintf(stderr, "parse %.6f s, search %.6f s\n",
                Sec(parsed-start).count(), Sec(done-parsed).count());
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "traverse: %s\n", e.what());
        return 1;
    }
    return 0;
}
