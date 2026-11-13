#include "collection_resources.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace opxy;
namespace ca=opxy::collection::activation;
namespace cr=opxy::collection::resources;
namespace {
void require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
struct File { std::vector<unsigned char> bytes; u32 at; bool error; };
struct Context {
    std::map<std::string,File> files;
    std::map<unsigned char*,u32> allocations;
    u32 calls,releases,opened,closed,progress,fail;
};
unsigned char* allocate(void* context,u32 bytes) {
    Context& c=*static_cast<Context*>(context);
    if (++c.calls==c.fail) return nullptr;
    unsigned char* raw=new unsigned char[bytes+32]; std::memset(raw,0xa7,bytes+32);
    unsigned char* p=raw+16; c.allocations[p]=bytes; return p;
}
void release(void* context,unsigned char* p) {
    Context& c=*static_cast<Context*>(context); const auto i=c.allocations.find(p);
    require(i!=c.allocations.end(),"unknown allocation released");
    for (u32 n=0;n<16;++n) require(p[-16+int(n)]==0xa7 && p[i->second+n]==0xa7,"allocation guard overwritten");
    delete[] (p-16); c.allocations.erase(i); ++c.releases;
}
u32 read(void* context,void* out,u32 count) {
    File& f=*static_cast<File*>(context); const u32 left=u32(f.bytes.size())-f.at;
    if (count>7) count=7;
    if (count>left) count=left;
    if (count) std::memcpy(out,f.bytes.data()+f.at,count);
    f.at+=count; return count;
}
bool failed(void* context) { const File& f=*static_cast<File*>(context); return f.error && f.at==f.bytes.size(); }
bool ended(void* context) { const File& f=*static_cast<File*>(context); return !f.error && f.at==f.bytes.size(); }
bool open(void* context,const char* path,opxy::collection::Reader& reader,void*& handle) {
    Context& c=*static_cast<Context*>(context); auto i=c.files.find(path);
    if (i==c.files.end()) return false;
    i->second.at=0; handle=&i->second; reader={handle,read,failed,ended}; ++c.opened; return true;
}
void close(void* context,void*) { ++static_cast<Context*>(context)->closed; }
void progress(void* context) { ++static_cast<Context*>(context)->progress; }
void little(unsigned char* out,u32 value) { for (u32 i=0;i<4;++i) out[i]=static_cast<unsigned char>(value>>(i*8)); }
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    const std::string mode=argv[1]; Context c={};
    if (mode.size()==4 && mode.substr(0,3)=="oom") c.fail=u32(mode[3]-'0');
    std::vector<unsigned char> payload(84,0),svg(7001,'a'),catalog(19,'b');
    const std::vector<unsigned char>* inputs[]={&svg,&catalog};
    std::string paths[2];
    for (u32 i=0;i<2;++i) {
        unsigned char* p=payload.data()+4+i*40; opxy::collection::Sha256 hash={};
        hash.begin(); hash.add(inputs[i]->data(),u32(inputs[i]->size())); hash.finish(p);
        little(p+32,u32(inputs[i]->size())); little(p+36,i+1);
        char path[128]; require(cr::path(path,sizeof(path),i+1,p),"path failed"); paths[i]=path;
        c.files[paths[i]]={*inputs[i],0,false};
    }
    if (mode=="missing") c.files.erase(paths[1]);
    if (mode=="short") c.files[paths[1]].bytes.pop_back();
    if (mode=="trailing") c.files[paths[1]].bytes.push_back('x');
    if (mode=="checksum") c.files[paths[1]].bytes.back()^=1;
    if (mode=="nul") c.files[paths[1]].bytes[5]=0;
    if (mode=="io-error") c.files[paths[1]].error=true;
    ca::Image image={}; image.status=ca::staged; image.version=2; image.bytes=payload.data();
    image.initialized=u32(payload.size()); image.text_bytes=4; image.resources_offset=4; image.resources_count=2;
    if (mode=="invalid") little(payload.data()+4+36,3);
    cr::Cache cache={}; cr::Workspace workspace={};
    require(!cr::matches(image,cache),"empty cache admitted");
    const ca::Allocator allocator={&c,allocate,release};
    const cr::Files files={&c,open,close,progress};
    const cr::Status result=cr::collect(image,files,allocator,cache,workspace);
    require(c.opened==c.closed,"file handle leaked");
    if (result==cr::ready) {
        require(cache.count==2 && cache.entries,"cache missing");
        require(cr::matches(image,cache),"completed cache not bound to image");
        image.digest[0]^=1;require(!cr::matches(image,cache),"foreign cache admitted");image.digest[0]^=1;
        // Removing storage and the staging image must not invalidate retained bytes.
        c.files.clear(); payload.clear();
        for (u32 i=0;i<2;++i) {
            require(cache.entries[i].bytes==inputs[i]->size(),"cache extent changed");
            require(!std::memcmp(cache.entries[i].data,inputs[i]->data(),inputs[i]->size()),"cache data changed");
            require(cache.entries[i].data[cache.entries[i].bytes]==0,"working-input terminator missing");
        }
        require(cr::collect(image,files,allocator,cache,workspace)==cr::cache_in_use,"live cache overwritten");
    } else require(!cache.count && !cache.entries,"failed cache retained allocations");
    cr::release(cache); cr::release(cache);
    require(c.allocations.empty(),"heap allocation leaked");
    char guard[101]; std::memset(guard,'?',sizeof(guard)); const unsigned char digest[32]={1};
    require(!cr::path(guard,99,1,digest) && guard[0]=='?',"short path buffer modified");
    require(cr::path(guard,100,2,digest) && guard[99]==0 && guard[100]=='?',"path buffer overrun");
    require(!cr::path(guard,100,0,digest),"invalid kind path accepted");
    std::printf("%u\n",unsigned(result));
    return 0;
}
