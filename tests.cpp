#include "scanpair.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <random>
#include <unistd.h>
using namespace sp;
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
Page page(unsigned seed,int shift=0){
    Page p;p.width=128;p.height=128;p.pixels.assign(128*128,255);
    std::mt19937 rng(seed);
    for(int k=0;k<22;++k){int x=8+int(rng()%100),y=8+int(rng()%100);int w=3+int(rng()%15),h=3+int(rng()%12);
        for(int yy=y;yy<std::min(128,y+h);++yy)for(int xx=x+shift;xx<std::min(128,x+w+shift);++xx)if(xx>=0)p.pixels[yy*128+xx]=20;}
    describe(p);return p;
}
int main(){try{
    auto a=page(1),b=page(2),c=page(3),d=page(4),e=page(5);
    check(std::abs(similarity(a,a)-1)<1e-9,"identical similarity");
    check(similarity(a,b)<.78,"different signatures rejected");
    check(similarity(a,page(1,4))>.85,"small shift tolerance");
    auto same=align({a,b,c},{a,b,c});
    check(same.size()==3,"identical sequence length");
    check(same[1].a==1&&same[1].b==1,"identical indices");
    auto missing=align({a,b,c},{a,c});
    check(missing.size()==3,"deletion length");
    check(missing[1].a==1&&missing[1].b==-1,"deleted B page identified");
    auto extra=align({a,c},{a,b,c});
    check(extra[1].a==-1&&extra[1].b==1,"insertion identified");
    auto ends=align({a,b,c},{b});
    check(ends.size()==3&&ends.front().b==-1&&ends.back().b==-1,"end deletions");
    auto duplicates=align({a,a},{a});
    check(duplicates.size()==2,"duplicates not lost");
    bool dupflag=false;for(auto& p:duplicates)if(p.a>=0&&p.b>=0)dupflag=p.ambiguous;
    check(dupflag,"duplicate counterpart uncertain");
    Page blank;blank.width=128;blank.height=128;blank.pixels.assign(16384,255);describe(blank);
    check(align({blank},{blank})[0].ambiguous,"blank pages uncertain");
    check(similarity(a,blank)==0,"blank versus content");
    auto unequal=align({a},{b});check(unequal.size()==2,"unrelated not forced together");
    bool empty=false;try{align({}, {a});}catch(...){empty=true;}check(empty,"empty rejected");
    bool big=false;try{align(std::vector<Page>(1001),{a});}catch(...){big=true;}check(big,"page cap");
    check(escape("<script>\"'&") == "&lt;script&gt;&quot;&#39;&amp;","HTML escaping");
    check(thumbnail(a).rfind("data:image/bmp;base64,Qk",0)==0,"BMP data URI");
    const auto tmp=std::filesystem::temp_directory_path()/("scanpair-tests-"+std::to_string(getpid()));
    std::filesystem::create_directory(tmp);
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}}cleanup{tmp};
    {std::ofstream f(tmp/"ok.pgm",std::ios::binary);f<<"P5\n# comment\n2 2\n255\n";char pixels[]={0,10,32,char(255)};f.write(pixels,4);}
    auto parsed=read_pgm(tmp/"ok.pgm");check(parsed.pixels[1]==10&&parsed.pixels[2]==32,"PGM binary whitespace preserved");
    {std::ofstream f(tmp/"bad.pgm");f<<"P5\n2 2\n255\nx";}
    bool truncated=false;try{read_pgm(tmp/"bad.pgm");}catch(...){truncated=true;}check(truncated,"truncated PGM rejected");
    {std::ofstream f(tmp/"large.pgm");f<<"P5\n20000 20000\n255\n";}
    bool oversized=false;try{read_pgm(tmp/"large.pgm");}catch(...){oversized=true;}check(oversized,"oversized PGM rejected");
    report(tmp/"r.html","<script>.pdf","B.pdf",{a,b,c},{a,c},missing);
    std::ifstream in(tmp/"r.html");std::string html((std::istreambuf_iterator<char>(in)),{});
    check(html.find("<script>")==std::string::npos,"filename injection escaped");
    check(html.find("Content-Security-Policy")!=std::string::npos,"report CSP");
    check(html.find("Unmatched candidate")!=std::string::npos,"unmatched report card");
    check(html.find("This is not a probability")!=std::string::npos,"score limitation");
    std::cout<<checks<<" core checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
