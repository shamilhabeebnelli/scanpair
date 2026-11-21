#include "scanpair.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace sp {
static std::string token(std::istream& in) {
    std::string s;
    while (in >> std::ws && in.peek() == '#') { std::getline(in, s); }
    if (!(in >> s)) throw std::runtime_error("Truncated PGM header");
    return s;
}
Page read_pgm(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in || token(in) != "P5") throw std::runtime_error("Expected binary P5 PGM");
    Page p;
    p.width = std::stoi(token(in)); p.height = std::stoi(token(in));
    if (p.width < 1 || p.height < 1 || p.width > 1024 || p.height > 1024 || token(in) != "255")
        throw std::runtime_error("Unsupported PGM dimensions or depth");
    char sep = 0; in.get(sep);
    if (sep == '\r' && in.peek() == '\n') in.get();
    else if (sep != '\n' && sep != ' ' && sep != '\t') throw std::runtime_error("Invalid PGM separator");
    p.pixels.resize(static_cast<size_t>(p.width) * p.height);
    in.read(reinterpret_cast<char*>(p.pixels.data()), static_cast<std::streamsize>(p.pixels.size()));
    if (in.gcount() != static_cast<std::streamsize>(p.pixels.size())) throw std::runtime_error("Truncated PGM pixels");
    describe(p); return p;
}
void describe(Page& p) {
    if (p.width < 1 || p.height < 1 || p.pixels.size() != static_cast<size_t>(p.width) * p.height)
        throw std::runtime_error("Invalid page buffer");
    p.ink.fill(0); std::array<int,1024> count{}; double total = 0;
    for (int y=0; y<p.height; ++y) for (int x=0; x<p.width; ++x) {
        const int cell = std::min(31, y*32/p.height)*32 + std::min(31, x*32/p.width);
        const double v = (255.0-p.pixels[static_cast<size_t>(y)*p.width+x])/255.0;
        p.ink[cell] += v; ++count[cell]; total += v;
    }
    for (size_t i=0;i<1024;++i) if(count[i]) p.ink[i] /= count[i];
    p.coverage = total / p.pixels.size();
}
double similarity(const Page& a, const Page& b) {
    // Small registration tolerance, not rotation, crop or deskew correction.
    const double aspect_a = double(a.width)/a.height, aspect_b = double(b.width)/b.height;
    if (std::abs(std::log(aspect_a/aspect_b)) > .15) return 0;
    if (a.coverage < .003 || b.coverage < .003)
        return (a.coverage < .003 && b.coverage < .003) ? 1 : 0;
    double best=0;
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        double intersection=0, uni=0;
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            const double av=a.ink[y*32+x];
            const int xx=x+dx, yy=y+dy;
            const double bv=(xx>=0&&xx<32&&yy>=0&&yy<32)?b.ink[yy*32+xx]:0;
            intersection+=std::min(av,bv); uni+=std::max(av,bv);
        }
        if(uni>0) best=std::max(best,intersection/uni);
    }
    return best;
}
std::vector<Pair> align(const std::vector<Page>& a, const std::vector<Page>& b) {
    if(a.empty() || b.empty() || a.size()>1000 || b.size()>1000)
        throw std::runtime_error("Each input must contain 1 to 1000 pages");
    const size_t n=a.size(), m=b.size(), cols=m+1;
    std::vector<float> scores(n*m);
    for(size_t i=0;i<n;++i) for(size_t j=0;j<m;++j) scores[i*m+j]=static_cast<float>(similarity(a[i],b[j]));
    std::vector<double> prev(cols), curr(cols);
    std::vector<unsigned char> route((n+1)*cols);
    constexpr double gap=.30;
    for(size_t j=1;j<=m;++j) {prev[j]=j*gap; route[j]=2;}
    for(size_t i=1;i<=n;++i) {
        curr[0]=i*gap; route[i*cols]=1;
        for(size_t j=1;j<=m;++j) {
            const double s=scores[(i-1)*m+j-1];
            const double match=prev[j-1]+(s>=.78?1-s:1.0);
            const double del=prev[j]+gap, ins=curr[j-1]+gap;
            if(match<=del && match<=ins) {curr[j]=match;route[i*cols+j]=0;}
            else if(del<=ins) {curr[j]=del;route[i*cols+j]=1;}
            else {curr[j]=ins;route[i*cols+j]=2;}
        }
        prev.swap(curr);
    }
    std::vector<Pair> result;
    size_t i=n,j=m;
    while(i||j) {
        auto dir=route[i*cols+j];
        if(i&&j&&dir==0) {
            --i;--j; const double s=scores[i*m+j]; bool ambiguous=a[i].coverage<.008||b[j].coverage<.008||s<.90;
            // Competing counterpart with similar score means the pair needs extra care.
            for(size_t k=0;k<m;++k) if(k!=j && scores[i*m+k]>=s-.03) ambiguous=true;
            for(size_t k=0;k<n;++k) if(k!=i && scores[k*m+j]>=s-.03) ambiguous=true;
            result.push_back({int(i),int(j),s,ambiguous});
        } else if(i && (dir==1||!j)) {--i;result.push_back({int(i),-1,0,true});}
        else {--j;result.push_back({-1,int(j),0,true});}
    }
    std::reverse(result.begin(),result.end()); return result;
}
std::string escape(const std::string& s) {
    std::string o;
    for(unsigned char c:s) switch(c) {
        case '&':o+="&amp;";break;case '<':o+="&lt;";break;case '>':o+="&gt;";break;
        case '"':o+="&quot;";break;case '\'':o+="&#39;";break;
        default: if(c>=32) o+=static_cast<char>(c);
    }
    return o;
}
static std::string base64(const std::vector<unsigned char>& in) {
    static const char* t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(size_t i=0;i<in.size();i+=3) {
        uint32_t v=uint32_t(in[i])<<16;
        if(i+1<in.size())v|=uint32_t(in[i+1])<<8;
        if(i+2<in.size())v|=in[i+2];
        out+=t[(v>>18)&63];out+=t[(v>>12)&63];
        out+=(i+1<in.size()?t[(v>>6)&63]:'=');out+=(i+2<in.size()?t[v&63]:'=');
    }
    return out;
}
std::string thumbnail(const Page& p) {
    // 24-bit BMP: a browser-readable format without a graphics-library dependency.
    const int stride=(p.width*3+3)&~3;
    std::vector<unsigned char> bytes(54+static_cast<size_t>(stride)*p.height,0);
    auto put=[&](size_t at,uint32_t v,int len){for(int z=0;z<len;++z)bytes[at+z]=static_cast<unsigned char>((v>>(z*8))&255);};
    bytes[0]='B';bytes[1]='M';put(2,static_cast<uint32_t>(bytes.size()),4);put(10,54,4);put(14,40,4);
    put(18,p.width,4);put(22,p.height,4);put(26,1,2);put(28,24,2);
    for(int y=0;y<p.height;++y)for(int x=0;x<p.width;++x) {
        size_t at=54+static_cast<size_t>(p.height-1-y)*stride+x*3;
        bytes[at]=bytes[at+1]=bytes[at+2]=p.pixels[static_cast<size_t>(y)*p.width+x];
    }
    return "data:image/bmp;base64,"+base64(bytes);
}
void report(const std::filesystem::path& out,const std::string& an,const std::string& bn,
            const std::vector<Page>& a,const std::vector<Page>& b,const std::vector<Pair>& pairs) {
    std::ofstream f(out,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot open report output");
    size_t unmatched=0,review=0,matched=0;
    for(auto& p:pairs) {if(p.a<0||p.b<0)++unmatched;else {++matched;if(p.ambiguous)++review;}}
    f<<R"(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'"><title>ScanPair review</title><style>
:root{font-family:system-ui,sans-serif;color:#e8edf7;background:#101827}*{box-sizing:border-box}body{max-width:1120px;margin:auto;padding:32px 20px}header{border-bottom:1px solid #38455a;padding-bottom:24px}h1{font-size:42px;margin:8px 0}h2{margin:0 0 14px;font-size:21px}p{line-height:1.6;color:#c5cedd}.eyebrow{color:#6fe1c3;font-size:13px;letter-spacing:2px;font-weight:700}.notice{border-left:4px solid #ffc778;background:#302b25;padding:16px;color:#ffdeb0}.stats{display:flex;flex-wrap:wrap;gap:12px;margin:24px 0}.stat{background:#1c293d;padding:16px 22px;border-radius:12px;flex:1;min-width:140px}.stat strong{display:block;font-size:28px;color:white}.stat span{color:#b8c7dc;font-size:13px}.card{border:1px solid #3a4d68;background:#182337;border-radius:16px;margin:16px 0;padding:20px;break-inside:avoid}.card.flag{border-color:#b18951}.tag{font-size:12px;display:inline-block;color:#101827;background:#6fe1c3;padding:5px 9px;border-radius:30px;font-weight:700}.flag .tag{background:#ffd28b}.pages{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:16px}.page{background:#101827;border-radius:10px;text-align:center;padding:12px;min-width:0}.page img{height:200px;max-width:100%;object-fit:contain;background:white;image-rendering:auto}.page p{margin:8px 0;font-size:13px}.empty{height:200px;display:grid;place-items:center;color:#d6bc96;border:1px dashed #706047;padding:12px}.files{overflow-wrap:anywhere;font-size:14px}summary{cursor:pointer;color:#c8d7ef;padding:14px 0}footer{margin-top:30px;border-top:1px solid #38455a;padding-top:16px;font-size:13px}@media(max-width:520px){body{padding:22px 12px}h1{font-size:34px}.card{padding:12px}.pages{gap:8px}.page{padding:6px}.page img,.empty{height:155px}}
</style><header><div class="eyebrow">LOCAL PDF COMPARISON</div><h1>ScanPair</h1><p>Two scans. One review queue.</p><div class="files"><p>Scan A: )"<<escape(an)<<"<br>Scan B: "<<escape(bn)<<R"(</p></div><p class="notice">Review aid, not a completeness certificate. A page missing from both scans is invisible here. Keep the originals. Every suggested match still needs human review.</p></header><div class="stats"><div class="stat"><strong>)"<<a.size()<<" / "<<b.size()<<R"(</strong><span>pages in A / B</span></div><div class="stat"><strong>)"<<unmatched<<R"(</strong><span>unmatched page candidates</span></div><div class="stat"><strong>)"<<review<<R"(</strong><span>uncertain suggested pairs</span></div><div class="stat"><strong>)"<<matched<<R"(</strong><span>suggested page pairs</span></div></div><h2>Review first</h2><p>Unmatched means no aligned counterpart was found, not proof of a missing sheet. Reordering, cropping, rotation, low ink or scan quality can cause this.</p>)";
    auto card=[&](const Pair& p) {
        bool flag=p.ambiguous||p.a<0||p.b<0;
        f<<"<article class=\"card"<<(flag?" flag":"")<<"\"><span class=\"tag\">";
        if(p.a<0||p.b<0)f<<"Unmatched candidate";
        else if(p.ambiguous)f<<"Uncertain pair";else f<<"Suggested pair";
        f<<"</span>";
        if(p.a>=0&&p.b>=0)f<<"<p>Visual similarity "<<std::fixed<<std::setprecision(0)<<p.similarity*100<<" / 100. This is not a probability.</p>";
        f<<"<div class=\"pages\">";
        auto side=[&](int ix,const std::vector<Page>& pages,char label){
            f<<"<div class=\"page\">";
            if(ix<0)f<<"<div class=\"empty\">No aligned counterpart in scan "<<label<<"</div>";
            else f<<"<img alt=\"Scan "<<label<<" page "<<ix+1<<" preview\" src=\""<<thumbnail(pages.at(ix))<<"\"><p>Scan "<<label<<" &middot; page "<<ix+1<<"</p>";
            f<<"</div>";
        };side(p.a,a,'A');side(p.b,b,'B');f<<"</div></article>";
    };
    if(!unmatched&&!review)f<<"<p>No priority candidates. This does not prove either scan is complete.</p>";
    for(auto& p:pairs)if(p.ambiguous||p.a<0||p.b<0)card(p);
    f<<"<details><summary>Other suggested pairs ("<<matched-review<<")</summary>";
    for(auto& p:pairs)if(!p.ambiguous&&p.a>=0&&p.b>=0)card(p);
    f<<R"(</details><footer><p>Method: 32 x 32 grayscale ink signatures, small translation tolerance and order-preserving sequence alignment. Not OCR, semantic comparison, forensic verification or a general PDF diff. Similar templates can look alike. Repeated and blank pages are uncertain. Rotation, major crops and page reordering are not corrected. Inspect full-resolution originals before deciding anything.</p><p>This standalone report contains low-resolution copies of your document pages. Treat it as sensitive. No scripts, external fonts, analytics or network assets.</p></footer></html>)";
    f.close();if(!f)throw std::runtime_error("Failed writing report");
}
}
