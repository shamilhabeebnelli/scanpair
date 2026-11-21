#include "scanpair.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs=std::filesystem;
struct TempDir {
    fs::path path;
    TempDir() {
        auto s=(fs::temp_directory_path()/"scanpair-XXXXXX").string();
        std::vector<char> buf(s.begin(),s.end());buf.push_back(0);
        char* p=mkdtemp(buf.data());if(!p)throw std::runtime_error("Cannot create private temporary directory");path=p;
    }
    ~TempDir(){std::error_code ec;fs::remove_all(path,ec);}
};
static void render(const fs::path& input,const fs::path& prefix) {
    // Separate argv entries, never a shell command. Paths cannot become switches.
    std::vector<std::string> args={"pdftoppm","-f","1","-l","1001","-scale-to","128","-gray",fs::absolute(input).string(),prefix.string()};
    std::vector<char*> argv;for(auto& s:args)argv.push_back(s.data());argv.push_back(nullptr);
    pid_t pid=fork();if(pid<0)throw std::runtime_error("Cannot start renderer");
    if(pid==0){setpgid(0,0);execvp(argv[0],argv.data());_exit(127);}
    setpgid(pid,pid);
    int status=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    while(true) {
        auto r=waitpid(pid,&status,WNOHANG);
        if(r==pid)break;
        if(r<0) {if(errno==EINTR)continue;throw std::runtime_error("Cannot wait for renderer");}
        if(std::chrono::steady_clock::now()>deadline) {
            kill(-pid,SIGKILL);kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("Rendering exceeded 180 seconds; try a smaller PDF");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)throw std::runtime_error("pdftoppm failed. Install Poppler and check that the PDF is readable and not password-protected");
}
static std::vector<sp::Page> load(const fs::path& dir,const std::string& prefix) {
    std::vector<std::pair<int,fs::path>> paths;
    for(auto& entry:fs::directory_iterator(dir)) {
        auto name=entry.path().filename().string();
        if(name.rfind(prefix+"-",0)==0&&entry.path().extension()==".pgm") {
            auto stem=entry.path().stem().string();
            int n=std::stoi(stem.substr(prefix.size()+1));paths.emplace_back(n,entry.path());
        }
    }
    std::sort(paths.begin(),paths.end());
    if(paths.empty()||paths.size()>1000)throw std::runtime_error("Each PDF must contain 1 to 1000 pages; no report was written");
    std::vector<sp::Page> pages;
    for(size_t i=0;i<paths.size();++i){if(paths[i].first!=int(i)+1)throw std::runtime_error("Renderer page sequence is incomplete");pages.push_back(sp::read_pgm(paths[i].second));}
    return pages;
}
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--help") {
        std::cout<<"ScanPair 0.1\nUsage: scanpair SCAN_A.pdf SCAN_B.pdf NEW_REPORT.html\n"
        <<"Local review aid. Requires Poppler pdftoppm on PATH. Linux/macOS.\n"
        <<"Never proves completeness; cannot find a page absent from both scans.\n"
        <<"Inputs stay unchanged; output must not already exist. Max 1000 pages per PDF.\n";return 0;
    }
    if(argc!=4){std::cerr<<"Usage: scanpair SCAN_A.pdf SCAN_B.pdf NEW_REPORT.html\n";return 2;}
    try {
        fs::path a=fs::absolute(argv[1]), b=fs::absolute(argv[2]), out=fs::absolute(argv[3]);
        if(!fs::is_regular_file(a)||!fs::is_regular_file(b))throw std::runtime_error("Both inputs must be existing regular files");
        if(fs::exists(out)||fs::is_symlink(out))throw std::runtime_error("Output already exists; choose a new filename (nothing overwritten)");
        if(out.extension()!=".html")throw std::runtime_error("Report filename must end in .html");
        if(!fs::is_directory(out.parent_path()))throw std::runtime_error("Report parent directory does not exist");
        TempDir temp;
        std::cerr<<"Rendering A locally...\n";render(a,temp.path/"a");auto pa=load(temp.path,"a");
        std::cerr<<"Rendering B locally...\n";render(b,temp.path/"b");auto pb=load(temp.path,"b");
        std::cerr<<"Aligning "<<pa.size()<<" / "<<pb.size()<<" pages...\n";
        auto pairs=sp::align(pa,pb);auto staged=temp.path/"report.html";
        sp::report(staged,a.filename().string(),b.filename().string(),pa,pb,pairs);
        // Exclusive output creation prevents a race from overwriting an existing file.
        int fd=open(out.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0)throw std::runtime_error("Cannot create new output: "+std::string(std::strerror(errno)));
        bool ok=true;std::string reason;
        try {
            std::ifstream in(staged,std::ios::binary);char buf[65536];
            while(in){in.read(buf,sizeof(buf));std::streamsize left=in.gcount();const char* ptr=buf;
                while(left>0){ssize_t done=write(fd,ptr,static_cast<size_t>(left));if(done<0){if(errno==EINTR)continue;throw std::runtime_error("Report write failed");}ptr+=done;left-=done;}}
            if(!in.eof())throw std::runtime_error("Report read failed");
        } catch(const std::exception& e){ok=false;reason=e.what();}
        if(close(fd)!=0){ok=false;reason="Report close failed";}
        if(!ok){fs::remove(out);throw std::runtime_error(reason);}
        size_t unmatched=0;for(auto& p:pairs)if(p.a<0||p.b<0)++unmatched;
        std::cout<<"Report: "<<out<<"\nUnmatched candidates: "<<unmatched<<"\n"
        <<"Review every suggestion against the originals. This is not proof of completeness.\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"ScanPair: "<<e.what()<<"\n";return 1;}
}
