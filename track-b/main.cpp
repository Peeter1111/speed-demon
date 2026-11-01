#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#ifdef __AVX2__
#include <immintrin.h>
#endif
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>


using u32 = uint32_t;
using u64 = uint64_t;
using u16 = uint16_t;
static constexpr u32 NIL = ~u32(0);

struct MMap {
  int fd = -1; size_t n = 0; const char *p = nullptr;
  explicit MMap(const std::string &path) {
    fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) { std::perror(path.c_str()); std::exit(2); }
    struct stat st{}; if (fstat(fd, &st)) { std::perror("fstat"); std::exit(2); }
    n = size_t(st.st_size);
    p = static_cast<const char*>(mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0));
    if (p == MAP_FAILED) { std::perror("mmap"); std::exit(2); }
  }
  ~MMap() { if (p && p != MAP_FAILED) munmap(const_cast<char*>(p), n); if (fd >= 0) close(fd); }
};

static inline bool word(unsigned char c) {
  unsigned char x = c | 32;
  return (c >= '0' && c <= '9') || (x >= 'a' && x <= 'z');
}
static inline u32 wordmask32(const char *p) {
#ifdef __AVX2__
  __m256i v=_mm256_loadu_si256((const __m256i*)p),l=_mm256_or_si256(v,_mm256_set1_epi8(32));
  __m256i a=_mm256_and_si256(_mm256_cmpgt_epi8(l,_mm256_set1_epi8('a'-1)),_mm256_cmpgt_epi8(_mm256_set1_epi8('z'+1),l));
  __m256i d=_mm256_and_si256(_mm256_cmpgt_epi8(v,_mm256_set1_epi8('0'-1)),_mm256_cmpgt_epi8(_mm256_set1_epi8('9'+1),v));
  return u32(_mm256_movemask_epi8(_mm256_or_si256(a,d)));
#else
  u32 bits=0;for(unsigned j=0;j<32;j++)if(word((unsigned char)p[j]))bits|=u32(1)<<j;return bits;
#endif
}
static inline unsigned char lower_ascii(unsigned char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}
static inline u64 hash_lower(const char *p, size_t n) {
  // Fast fingerprint: collisions are harmless because every lookup confirms full bytes.
  u32 h=u32(n)*0x9e3779b9u;
  if(n>=4){u32 a,b;std::memcpy(&a,p,4);std::memcpy(&b,p+n-4,4);a|=0x20202020u;b|=0x20202020u;h^=a^((b<<15)|(b>>17));}
  else for(size_t i=0;i<n;i++)h^=u32(lower_ascii((unsigned char)p[i]))<<(i*8);
  h^=h>>16;h*=0x7feb352du;h^=h>>15;return h?h:1;
}
struct Term { const char *p; u32 len, sig; };

struct Dict {
  std::vector<u32> tab;
  std::vector<u64> bloom;
  std::vector<Term> terms;
  explicit Dict(size_t cap = 1u<<17) : tab(cap),bloom(1u<<14) { terms.reserve(100000); }
  bool eq(u32 id, const char *p, size_t n, bool folded) const {
    const Term &t = terms[id];
    if (t.len != n) return false;
    if (!folded) return std::memcmp(t.p,p,n)==0;
    if(n<8){size_t i=0;if(n>=4){u32 a,b;std::memcpy(&a,p,4);std::memcpy(&b,t.p,4);if((a|0x20202020u)!=b)return false;i=4;}for(;i<n;i++)if((unsigned char)t.p[i]!=lower_ascii((unsigned char)p[i]))return false;return true;}
    constexpr u64 lo=0x2020202020202020ULL;size_t i=0;
    for(;i+8<=n;i+=8){u64 a,b;std::memcpy(&a,p+i,8);std::memcpy(&b,t.p+i,8);if((a|lo)!=b)return false;}
    if(i<n){u64 a,b;std::memcpy(&a,p+n-8,8);std::memcpy(&b,t.p+n-8,8);if((a|lo)!=b)return false;}
    return true;
  }
  u32 get_or_add(const char *p, size_t n) {
    u64 h=hash_lower(p,n); u32 sig=u32(h); size_t m=tab.size()-1, i=h&m;
    while (tab[i]) {
      u32 x=tab[i],id=(x&0x1ffffu)-1;if ((x>>17)==(sig>>17) && eq(id,p,n,false)) return id;
      i=(i+1)&m;
    }
    u32 id=(u32)terms.size();terms.push_back({p,(u32)n,sig});tab[i]=(sig&0xfffe0000u)|(id+1);u32 g=sig*0x9e3779b1u;bloom[(h>>6)&(bloom.size()-1)]|=(1ULL<<(h&63))|(1ULL<<(g&63));
    return id;
  }
  u32 find_folded(const char *p, size_t n, u64 h) const {
    u32 sig=u32(h),g=sig*0x9e3779b1u;u64 bm=(1ULL<<(h&63))|(1ULL<<(g&63));if((bloom[(h>>6)&(bloom.size()-1)]&bm)!=bm)return NIL;
    size_t m=tab.size()-1, i=h&m;
    while (tab[i]) {
      u32 x=tab[i],id=(x&0x1ffffu)-1;if ((x>>17)==(sig>>17) && eq(id,p,n,true)) return id;
      i=(i+1)&m;
    }
    return NIL;
  }
};

struct Query {
  u32 t[8]{};
  unsigned char op[5]{};
  unsigned char n=0, phrase=0, anchor=0;
};
struct Result {
  u32 count=0, topn=0, top[10]{};
  u64 sum=0;
};

static std::vector<Query> parse_queries(const MMap &qf, Dict &dict) {
  std::vector<Query> qs; qs.reserve(50000);
  const char *p=qf.p, *end=qf.p+qf.n;
  while (p<end) {
    const char *e=(const char*)memchr(p,'\n',size_t(end-p)); if (!e) e=end;
    Query q{};
    if (e-p>=5 && !std::memcmp(p,"BOOL ",5)) {
      const char *s=p+5;
      while (s<e) {
        const char *z=s; while (z<e && *z!=' ') ++z;
        q.t[q.n++]=dict.get_or_add(s,size_t(z-s));
        if (z==e) break;
        const char *o=z+1, *oe=o; while (oe<e && *oe!=' ') ++oe;
        q.op[q.n-1]=(unsigned char)(*o=='A'?0:*o=='O'?1:2);
        s=oe+1;
      }
    } else if (e-p>=9 && !std::memcmp(p,"PHRASE \"",8)) {
      q.phrase=1; const char *s=p+8, *limit=e-1;
      while (s<limit) {
        const char *z=s; while (z<limit && *z!=' ') ++z;
        q.t[q.n++]=dict.get_or_add(s,size_t(z-s)); s=z+1;
      }
    } else { std::fprintf(stderr,"bad query line\n"); std::exit(2); }
    qs.push_back(q); p=e+(e<end);
  }
  return qs;
}

static u32 document_count(const MMap &docs) {
  const char *p=docs.p+docs.n;
  if (p>docs.p && p[-1]=='\n') --p;
  while (p>docs.p && p[-1]!='\n') --p;
  u32 id=0; while (p<docs.p+docs.n && *p>='0'&&*p<='9') id=id*10+u32(*p++-'0');
  return id+1;
}

struct Posting { u32 off=0, len=0; };
struct List { const u32 *p=nullptr; u32 n=0; };
struct Index {
  std::unique_ptr<u32[]> data;
  std::vector<Posting> post;
  std::unique_ptr<u32[]> posdata;
  std::vector<Posting> pospost;
  std::vector<u64> dense;
  std::vector<u32> denseid;
  size_t words=0;
  List get(u32 t) const { const Posting &x=post[t]; return {data.get()+x.off,x.len}; }
  const u64*bits(u32 t)const{return dense.data()+size_t(denseid[t])*words;}
};
struct ThreadBuild {
  // Each sparse posting is a 16-bit query-term index, in document order.
  // Phrase anchor occurrences pack (term_id, byte_offset) into one 64-bit value.
  std::vector<u16> pairs;
  std::vector<u32> ends;
  std::vector<u64> pos;
  std::vector<u32> seen_count;
  std::vector<u16> dense_seen;
  u32 first=NIL;
  ThreadBuild(size_t dense,size_t reserve_pairs,size_t reserve_pos):dense_seen(dense){pairs.reserve(reserve_pairs);ends.reserve(40000);pos.reserve(reserve_pos);}
};

static void scan_slice(const MMap &docs, const Dict &dict, const char *a, const char *b,
                       ThreadBuild &tb,std::vector<u32>&lineoff,
                       const std::vector<u32>&code,std::vector<u64>&densebits,u32 sparse_count,size_t words) {
  const char *p=a;u32 cache[1024]{};
  while (p<b) {
    u32 id=0; while (p<b && *p>='0'&&*p<='9') id=id*10+u32(*p++-'0');
    if(tb.first==NIL)tb.first=id;
    if (p>=b || *p!='\t') { std::fprintf(stderr,"bad docs line\n"); std::exit(2); }
    const char *s=++p, *e=(const char*)memchr(s,'\n',size_t(b-s)); if (!e) e=b;
    lineoff[id]=u32(s-docs.p);
    auto hit=[&](const char*w,const char*z){
        size_t n=size_t(z-w);u64 h=hash_lower(w,n);u32 c=cache[u32(h)&1023],t=NIL;
        if(c&&(c>>17)==(u32(h)>>17)&&dict.eq((c&0x1ffffu)-1,w,n,true))t=(c&0x1ffffu)-1;else if((t=dict.find_folded(w,n,h))!=NIL)cache[u32(h)&1023]=(u32(h)&0xfffe0000u)|(t+1);
        if(t!=NIL){u32 meta=code[t];
          if(meta&0x80000000u)tb.pos.push_back((u64(t)<<32)|u32(w-docs.p));
          if(meta&0x40000000u){u32 index=meta&65535;
            if(meta&0x20000000u){u16 stamp=u16(tb.ends.size()+1);if(tb.dense_seen[index]!=stamp){tb.dense_seen[index]=stamp;densebits[size_t(index)*words+(id>>6)]|=1ULL<<(id&63);}}
            else tb.pairs.push_back((u16)index);
          }
        }
    };
    const char*carry=nullptr;
    while(p+32<=e){u32 m=wordmask32(p);if(carry){if(m==~u32(0)){p+=32;continue;}unsigned z=__builtin_ctz(~m);hit(carry,p+z);carry=nullptr;m&=~u32((1ULL<<(z+1))-1);}while(m){unsigned s0=__builtin_ctz(m),len;u32 y=m>>s0,inv=~y;if(!inv)len=32-s0;else len=__builtin_ctz(inv);if(s0+len==32){carry=p+s0;break;}hit(p+s0,p+s0+len);m&=~u32(((1ULL<<len)-1)<<s0);}p+=32;}
    if(carry){const char*z=p;while(z<e&&word((unsigned char)*z))++z;hit(carry,z);p=z;}
    while(p<e){while(p<e&&!word((unsigned char)*p))++p;const char*w=p;while(p<e&&word((unsigned char)*p))++p;if(p>w)hit(w,p);}
    tb.ends.push_back((u32)tb.pairs.size());
    p=e+(e<b);
  }
  tb.seen_count.resize(sparse_count);u32 beg=0,out=0;for(size_t m=0;m<tb.ends.size();m++){u32 end=tb.ends[m];u16 stamp=u16(m+1);for(u32 k=beg;k<end;k++){u32 s=tb.pairs[k];u32&sc=tb.seen_count[s];if(u16(sc)!=stamp){sc=(sc&0xffff0000u)+0x10000u+stamp;tb.pairs[out++]=(u16)s;}}tb.ends[m]=out;beg=end;}tb.pairs.resize(out);std::vector<u16>().swap(tb.dense_seen);
}

static Index build_index(const MMap &docs, const Dict &dict,std::vector<u32>&lineoff,unsigned nt,
                       const std::vector<u32>&denseid,u32 dense_count,
                       const std::vector<u32>&sparseterm,const std::vector<u32>&code,u32 ndoc) {
  size_t words=(ndoc+63)/64;std::vector<u64>densebits(size_t(dense_count)*words);
  std::vector<const char*> cut(nt+1); cut[0]=docs.p; cut[nt]=docs.p+docs.n;
  for (unsigned i=1;i<nt;i++) {
    // Round partitions to complete documents with an id divisible by 64.
    const char *p=docs.p+(docs.n*i)/nt, *e=docs.p+docs.n;
    while (p<e && p[-1]!='\n') ++p;
    while(p<e){const char*q=p;u32 id=0;while(q<e&&*q>='0'&&*q<='9')id=id*10+u32(*q++-'0');if(!(id&63))break;const char*n=(const char*)memchr(p,'\n',size_t(e-p));if(!n){p=e;break;}p=n+1;}
    cut[i]=p;
  }
  std::vector<ThreadBuild> tb; tb.reserve(nt);
  for (unsigned i=0;i<nt;i++) tb.emplace_back(dense_count,size_t(cut[i+1]-cut[i])/20+40000,size_t(cut[i+1]-cut[i])/180+2000);
  std::vector<std::thread> th; th.reserve(nt);
  for (unsigned i=0;i<nt;i++) th.emplace_back(scan_slice,std::cref(docs),std::cref(dict),cut[i],cut[i+1],std::ref(tb[i]),std::ref(lineoff),std::cref(code),std::ref(densebits),u32(sparseterm.size()),words);
  for (auto &t:th) t.join();
  Index idx;idx.post.resize(dict.terms.size());idx.denseid=denseid;idx.words=words;
  size_t total=0;
  for(u32 s=0;s<sparseterm.size();s++){
    u32 term=sparseterm[s],n=0;for(unsigned j=0;j<nt;j++)n+=tb[j].seen_count[s]>>16;
    idx.post[term]={u32(total),n};total+=n;
  }
  idx.pospost.resize(dict.terms.size());std::vector<u32> pc(dict.terms.size());
  for(auto &x:tb)for(u64 v:x.pos)++pc[u32(v>>32)];size_t ptotal=0;
  for(u32 term=0;term<idx.pospost.size();term++){u32 n=pc[term];idx.pospost[term]={u32(ptotal),n};pc[term]=u32(ptotal);ptotal+=n;}
  idx.posdata.reset(new u32[ptotal]);
  for(unsigned j=0;j<nt;j++){for(u64 x:tb[j].pos){u32 term=u32(x>>32);idx.posdata[pc[term]++]=u32(x);}std::vector<u64>().swap(tb[j].pos);}std::vector<u32>().swap(pc);
  for(u32 s=0;s<sparseterm.size();s++){u32 term=sparseterm[s],pos=idx.post[term].off;for(unsigned j=0;j<nt;j++){u32 n=tb[j].seen_count[s]>>16;tb[j].seen_count[s]=pos;pos+=n;}}
  idx.data.reset(new u32[total]);
  for(unsigned j=0;j<nt;j++){u32 beg=0;for(size_t m=0;m<tb[j].ends.size();m++){u32 end=tb[j].ends[m],doc=tb[j].first+u32(m);for(u32 k=beg;k<end;k++){u32 slot=tb[j].pairs[k];idx.data[tb[j].seen_count[slot]++]=doc;}beg=end;}std::vector<u32>().swap(tb[j].seen_count);std::vector<u16>().swap(tb[j].pairs);std::vector<u32>().swap(tb[j].ends);}
  idx.dense=std::move(densebits);
  return idx;
}

static inline List as_list(const std::vector<u32>&v){return {v.data(),u32(v.size())};}
static inline void set_inter(List a,List b,std::vector<u32> &o) {
  o.clear(); o.reserve(std::min(a.n,b.n));
  List s=a,l=b; if (s.n>l.n) std::swap(s,l);
  if (l.n>s.n*8+32) {
    const u32 *it=l.p,*le=l.p+l.n;
    for (u32 k=0;k<s.n;k++) { u32 x=s.p[k]; it=std::lower_bound(it,le,x); if (it==le) break; if (*it==x) o.push_back(x); }
  } else {
    u32 i=0,j=0; while (i<a.n&&j<b.n) { u32 x=a.p[i],y=b.p[j]; if(x<y)++i; else if(y<x)++j; else{o.push_back(x);++i;++j;} }
  }
}
static inline void set_unionv(List a,List b,std::vector<u32> &o) {
  o.clear(); o.reserve(size_t(a.n)+b.n); u32 i=0,j=0;
  while(i<a.n&&j<b.n){u32 x=a.p[i],y=b.p[j];if(x<y){o.push_back(x);++i;}else if(y<x){o.push_back(y);++j;}else{o.push_back(x);++i;++j;}}
  o.insert(o.end(),a.p+i,a.p+a.n); o.insert(o.end(),b.p+j,b.p+b.n);
}
static inline void set_diff(List a,List b,std::vector<u32> &o) {
  o.clear(); o.reserve(a.n);
  if (b.n>a.n*8+32) {
    const u32 *it=b.p,*be=b.p+b.n;
    for(u32 i=0;i<a.n;i++){u32 x=a.p[i];it=std::lower_bound(it,be,x);if(it==be||*it!=x)o.push_back(x);}
  } else {
    u32 i=0,j=0;while(i<a.n&&j<b.n){u32 x=a.p[i],y=b.p[j];if(x<y){o.push_back(x);++i;}else if(y<x)++j;else{++i;++j;}}
    o.insert(o.end(),a.p+i,a.p+a.n);
  }
}
static inline Result summarize(List v) {
  Result r; r.count=v.n; r.topn=std::min<u32>(10,r.count);
  for (u32 i=0;i<r.topn;i++) r.top[i]=v.p[i];
  for (u32 i=0;i<v.n;i++) r.sum+=v.p[i]; return r;
}
static inline void add_result(Result &r,u32 x){if(r.topn<10)r.top[r.topn++]=x;++r.count;r.sum+=x;}
static inline bool hasbit(const u64*b,u32 x){return (b[x>>6]>>(x&63))&1;}
static void filter_bits(List v,const u64*b,bool keep,std::vector<u32>&o){o.clear();o.reserve(v.n);for(u32 i=0;i<v.n;i++){u32 x=v.p[i];if(hasbit(b,x)==keep)o.push_back(x);}}
static Result summarize_filter(List v,const u64*b,bool keep){Result r;for(u32 i=0;i<v.n;i++){u32 x=v.p[i];if(hasbit(b,x)==keep)add_result(r,x);}return r;}
static Result summarize_bits(const u64*b,size_t words){Result r;for(size_t w=0;w<words;w++){u64 x=b[w];while(x){unsigned k=(unsigned)__builtin_ctzll(x);add_result(r,u32(w*64+k));x&=x-1;}}return r;}
static Result summarize_op(unsigned char op,List a,List b) {
  Result r;
  if(op==0){
    List s=a,l=b;if(s.n>l.n)std::swap(s,l);
    if(l.n>s.n*8+32){const u32*it=l.p,*le=l.p+l.n;for(u32 k=0;k<s.n;k++){u32 x=s.p[k];it=std::lower_bound(it,le,x);if(it==le)break;if(*it==x)add_result(r,x);}}
    else{u32 i=0,j=0;while(i<a.n&&j<b.n){u32 x=a.p[i],y=b.p[j];if(x<y)++i;else if(y<x)++j;else{add_result(r,x);++i;++j;}}}
  } else if(op==1){
    u32 i=0,j=0;while(i<a.n&&j<b.n){u32 x=a.p[i],y=b.p[j];if(x<y){add_result(r,x);++i;}else if(y<x){add_result(r,y);++j;}else{add_result(r,x);++i;++j;}}
    while(i<a.n)add_result(r,a.p[i++]);while(j<b.n)add_result(r,b.p[j++]);
  } else {
    if(b.n>a.n*8+32){const u32*it=b.p,*be=b.p+b.n;for(u32 i=0;i<a.n;i++){u32 x=a.p[i];it=std::lower_bound(it,be,x);if(it==be||*it!=x)add_result(r,x);}}
    else{u32 i=0,j=0;while(i<a.n&&j<b.n){u32 x=a.p[i],y=b.p[j];if(x<y){add_result(r,x);++i;}else if(y<x)++j;else{++i;++j;}}while(i<a.n)add_result(r,a.p[i++]);}
  }
  return r;
}

static bool phrase_at(u32 off,const Query&q,const MMap&docs,const Dict&dict){
  // Verify neighbors directly around an indexed phrase anchor (not merely co-occurrence).
  const char*a=docs.p+off,*p=a,*de=docs.p+docs.n;
  for(int j=int(q.anchor)-1;j>=0;j--){while(p>docs.p&&!word((unsigned char)p[-1])){if(p[-1]=='\t'||p[-1]=='\n')return false;--p;}const char*e=p;while(p>docs.p&&word((unsigned char)p[-1]))--p;if(p==e||!dict.eq(q.t[j],p,size_t(e-p),true))return false;}
  p=a+dict.terms[q.t[q.anchor]].len;
  for(unsigned j=q.anchor+1;j<q.n;j++){while(p<de&&!word((unsigned char)*p)){if(*p=='\n')return false;++p;}const char*s=p;while(p<de&&word((unsigned char)*p))++p;if(p==s||!dict.eq(q.t[j],s,size_t(p-s),true))return false;}
  return true;
}

static Result answer_bool(const Query &q,const Index &idx,std::vector<u32>&a,std::vector<u32>&b,std::vector<u64>&bits) {
  bool dense=idx.denseid[q.t[0]]!=NIL;if(q.n==1)return dense?summarize_bits(idx.bits(q.t[0]),idx.words):summarize(idx.get(q.t[0]));
  List cur{};if(dense)bits.assign(idx.bits(q.t[0]),idx.bits(q.t[0])+idx.words);else cur=idx.get(q.t[0]);bool flip=false;
  for(unsigned k=1;k<q.n;k++){
    u32 t=q.t[k];bool rd=idx.denseid[t]!=NIL,last=k+1==q.n;unsigned char op=q.op[k-1];
    if(!dense&&!rd){List rhs=idx.get(t);if(last)return summarize_op(op,cur,rhs);std::vector<u32>&out=flip?b:a;flip=!flip;if(op==0)set_inter(cur,rhs,out);else if(op==1)set_unionv(cur,rhs,out);else set_diff(cur,rhs,out);cur=as_list(out);continue;}
    if(!dense&&rd){const u64*rb=idx.bits(t);if(op!=1){bool keep=op==0;if(last)return summarize_filter(cur,rb,keep);std::vector<u32>&out=flip?b:a;flip=!flip;filter_bits(cur,rb,keep,out);cur=as_list(out);continue;}bits.assign(rb,rb+idx.words);for(u32 i=0;i<cur.n;i++)bits[cur.p[i]>>6]|=1ULL<<(cur.p[i]&63);dense=true;if(last)return summarize_bits(bits.data(),idx.words);continue;}
    if(dense&&rd){const u64*rb=idx.bits(t);for(size_t w=0;w<idx.words;w++){if(op==0)bits[w]&=rb[w];else if(op==1)bits[w]|=rb[w];else bits[w]&=~rb[w];}if(last)return summarize_bits(bits.data(),idx.words);continue;}
    List rhs=idx.get(t);if(op==0){if(last)return summarize_filter(rhs,bits.data(),true);std::vector<u32>&out=flip?b:a;flip=!flip;filter_bits(rhs,bits.data(),true,out);cur=as_list(out);dense=false;}
    else{for(u32 i=0;i<rhs.n;i++){u64 m=1ULL<<(rhs.p[i]&63);if(op==1)bits[rhs.p[i]>>6]|=m;else bits[rhs.p[i]>>6]&=~m;}if(last)return summarize_bits(bits.data(),idx.words);}
  }
  return {};
}
static Result answer_phrase_pos(const Query&q,const Index&idx,const MMap&docs,const Dict&dict,const std::vector<u32>&lineoff){
  const Posting&p=idx.pospost[q.t[q.anchor]];Result r;u32 skip=0;
  for(u32 i=0;i<p.len;i++){u32 off=idx.posdata[p.off+i];if(off<skip)continue;
    if(phrase_at(off,q,docs,dict)){u32 doc=u32(std::upper_bound(lineoff.begin(),lineoff.end(),off)-lineoff.begin()-1);add_result(r,doc);skip=lineoff[doc+1];}}
  return r;
}

static std::vector<Result> answer_all(const std::vector<Query>&qs,const Index&idx,const MMap&docs,const Dict&dict,
                                      const std::vector<u32>&lineoff,unsigned nt) {
  std::vector<Result> out(qs.size()); std::atomic<size_t> next{0}; std::vector<std::thread> th;
  for(unsigned z=0;z<nt;z++)th.emplace_back([&]{
    std::vector<u32>a,b;std::vector<u64>bits;
    for(;;){size_t base=next.fetch_add(8,std::memory_order_relaxed);if(base>=qs.size())break;size_t end=std::min(base+8,qs.size());
      for(size_t i=base;i<end;i++)out[i]=qs[i].phrase?answer_phrase_pos(qs[i],idx,docs,dict,lineoff):answer_bool(qs[i],idx,a,b,bits);}
  });
  for(auto&t:th)t.join();return out;
}

static void write_results(const std::string &dir,const std::vector<Result>&rs) {
  mkdir(dir.c_str(),0755); std::string path=dir+"/results.txt"; FILE*f=std::fopen(path.c_str(),"wb");
  if(!f){std::perror(path.c_str());std::exit(2);} char line[256];
  for(const Result&r:rs){char*p=line;auto put=[&](u64 x){char*s=p;do{*p++=char('0'+x%10);x/=10;}while(x);std::reverse(s,p);};put(r.count);*p++=' ';put(r.sum);
    for(u32 i=0;i<r.topn;i++){*p++=' ';put(r.top[i]);}*p++='\n';std::fwrite(line,1,size_t(p-line),f);}
  if(std::fclose(f)){std::perror("fclose");std::exit(2);}
}

int main(int argc,char**argv){
  const char*de=std::getenv("DATA_DIR"),*oe=std::getenv("OUT_DIR");
  std::string data=argc>1?argv[1]:(de?de:"/data"),out=argc>2?argv[2]:(oe?oe:"/out");
  MMap qf(data+"/queries.txt"); Dict dict; auto qs=parse_queries(qf,dict);
  std::vector<u32>uses(dict.terms.size());for(auto&q:qs)for(unsigned j=0;j<q.n;j++)++uses[q.t[j]];
  std::vector<unsigned char>needpost(dict.terms.size()),posflag(dict.terms.size());
  for(auto&q:qs)if(q.phrase){unsigned best=0;for(unsigned j=1;j<q.n;j++){u32 a=q.t[j],b=q.t[best];if(uses[a]<uses[b]||(uses[a]==uses[b]&&dict.terms[a].len>dict.terms[b].len))best=j;}q.anchor=(unsigned char)best;posflag[q.t[best]]=1;}
    else for(unsigned j=0;j<q.n;j++)needpost[q.t[j]]=1;
  MMap docs(data+"/docs.txt"); u32 ndoc=document_count(docs);
  // The specified full-scale corpus fits 32-bit byte offsets and document ids.
  if(docs.n>=(1ULL<<32)||ndoc>=(1u<<18)||dict.terms.size()>=(1u<<18)){
    std::fprintf(stderr,"corpus exceeds Track B index bounds\n");return 2;
  }
  std::vector<u32>lineoff(ndoc+1);lineoff[ndoc]=u32(docs.n);
  // Frequent boolean query terms use bitmaps; sparse ones use sorted postings.
  std::vector<u32>denseid(dict.terms.size(),NIL);u32 dense_count=0;for(u32 t=0;t<denseid.size();t++)if(needpost[t]&&uses[t]>22)denseid[t]=dense_count++;
  std::vector<u32>sparseid(dict.terms.size(),NIL),sparseterm;for(u32 t=0;t<sparseid.size();t++)if(needpost[t]&&denseid[t]==NIL){sparseid[t]=(u32)sparseterm.size();sparseterm.push_back(t);}
  if(sparseterm.size()>65536){std::fprintf(stderr,"too many sparse terms\n");return 2;}
  // One cache-local lookup encodes all scan actions for a recognized token.
  std::vector<u32>code(dict.terms.size());
  for(u32 t=0;t<code.size();t++){
    if(posflag[t])code[t]|=0x80000000u;
    if(needpost[t])code[t]|=0x40000000u;
    if(denseid[t]!=NIL)code[t]|=0x20000000u|denseid[t];
    else if(sparseid[t]!=NIL)code[t]|=sparseid[t];
  }
  // These temporary query flags are now represented by one packed scan word.
  std::vector<unsigned char>().swap(needpost);
  std::vector<unsigned char>().swap(posflag);
  std::vector<u32>().swap(sparseid);
  unsigned nt=std::thread::hardware_concurrency(); if(!nt)nt=1; if(nt>12)nt=12;
  Index idx=build_index(docs,dict,lineoff,nt,denseid,dense_count,sparseterm,code,ndoc);
  auto rs=answer_all(qs,idx,docs,dict,lineoff,nt);write_results(out,rs);
  return 0;
}
