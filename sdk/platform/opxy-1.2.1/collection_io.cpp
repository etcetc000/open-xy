#include "collection_io.hpp"
#include "stock.hpp"
namespace opxy { namespace collection { namespace io {
using platform::stock;
using platform::word;
namespace {
const u32 maximum_read=2u*1024u*1024u;
bool valid(Heap* heap) {
    return heap && heap->protected_bytes && heap->protected_bytes<=0xffffffffu-heap->protected_base &&
        heap->arena_bytes>=64 && !(heap->protected_base & 31) && !(heap->protected_bytes & 31) &&
        heap->arena_base<heap->protected_base && heap->arena_bytes==heap->protected_base-heap->arena_base &&
        heap->reserve_bytes<=heap->arena_bytes;
}
bool allowed(Heap* heap,const unsigned char* pointer,u32 bytes) {
    if (!valid(heap)) return false;
    const platform::Address address=reinterpret_cast<platform::Address>(pointer);
    if (!pointer || (address & 3) || address>~platform::Address(0)-bytes) return false;
    return heap->arena_base<=address && address+bytes<=heap->protected_base;
}
unsigned char* allocate_wait(void* context,u32 bytes,u32 wait) {
    Heap* heap=static_cast<Heap*>(context);
    if (!bytes || !valid(heap) || bytes>heap->protected_bytes) return nullptr;
    // Use stock's nonfatal heap entry under exactly its usual mutex.
    const u32 mutex=word(0x08036aa4),arena=word(0x08036aa8);
    if (!mutex || (arena & 3) || arena<heap->arena_base || arena>heap->protected_base-28 ||
        !stock<u32(u32,u32)>(0x8c03e6f4)(mutex,wait)) return nullptr;
    // Exact dlmalloc topchunk fields used by stock mspace_malloc. This is a
    // conservative contiguous-free lower bound, not total or largest free.
    const u32 top_bytes=word(arena+0x0c),top=word(arena+0x18);
    const u32 rounded=bytes<27 ? 32 : (bytes+35)&~31u;
    const bool admitted=top>=heap->arena_base && top<heap->protected_base && !(top & 7) &&
        !(top_bytes & 7) && top_bytes<=heap->protected_base-top && top_bytes>rounded &&
        rounded<=0xffffffffu-heap->reserve_bytes && top_bytes>=rounded+heap->reserve_bytes;
    unsigned char* result=admitted ? stock<unsigned char*(u32,u32)>(0x8c048788)(arena,bytes) : nullptr;
    const u32 unlocked=stock<u32(u32,const void*,u32,u32)>(0x8c03e9c8)(mutex,nullptr,0,0);
    // Do not pass an invalid/overlapping result to free: it could be code.
    return unlocked && allowed(heap,result,bytes) ? result : nullptr;
}
unsigned char* allocate(void* context,u32 bytes) { return allocate_wait(context,bytes,0xffffffffu); }
void release(void* context,unsigned char* pointer) {
    if (allowed(static_cast<Heap*>(context),pointer,1)) stock<void(void*)>(0x8d4d2910)(pointer);
}
void* open(const char* path,const char* mode) { return stock<void*(const char*,const char*)>(0x8fed0348)(path,mode); }
u32 read(void* file,void* out,u32 bytes) { return stock<u32(void*,u32,u32,void*)>(0x8fed05bc)(out,1,bytes,file); }
bool failed(void* file) { return (*static_cast<const volatile u16*>(file)&0x200u)!=0; }
bool ended(void* file) { return (*static_cast<const volatile u16*>(file)&0x100u)!=0; }
bool open_resource(void*,const char* path,Reader& reader,void*& handle) {
    handle=open(path,"rb");
    if (!handle) return false;
    reader=Reader{handle,read,failed,ended};return true;
}
void close_resource(void*,void* handle) { stock<int(void*)>(0x8fed00ae)(handle); }
void progress(void*) { delay(1); }
}
activation::Allocator allocator(Heap& heap) { return activation::Allocator{&heap,allocate,release}; }
unsigned char* try_allocate(Heap& heap,u32 bytes) { return allocate_wait(&heap,bytes,0); }
u32 contiguous_free(Heap& heap) {
    if (!valid(&heap)) return 0;
    const u32 mutex=word(0x08036aa4),arena=word(0x08036aa8);
    if (!mutex || (arena & 3) || arena<heap.arena_base || arena>heap.protected_base-28 ||
        !stock<u32(u32,u32)>(0x8c03e6f4)(mutex,0)) return 0;
    const u32 bytes=word(arena+0x0c),top=word(arena+0x18);
    const bool correct=top>=heap.arena_base && top<heap.protected_base && !(top & 7) &&
        !(bytes & 7) && bytes<=heap.protected_base-top;
    const u32 unlocked=stock<u32(u32,const void*,u32,u32)>(0x8c03e9c8)(mutex,nullptr,0,0);
    return correct && unlocked ? bytes : 0;
}
resources::Files files() { return resources::Files{nullptr,open_resource,close_resource,progress}; }
bool read_bytes(const char* path,unsigned char* out,u32 bytes,bool& opened,Cancelled cancelled,void* context) {
    opened=false;
    if (!path || !out || !bytes || bytes>maximum_read || (cancelled && cancelled(context))) return false;
    void* file=open(path,"rb");
    if (!file) return false;
    opened=true;u32 at=0;
    while (at<bytes && !(cancelled && cancelled(context))) {
        const u32 left=bytes-at;
        const u32 n=read(file,out+at,left>4096 ? 4096 : left);
        if (!n || n>bytes-at) break;
        at+=n;
    }
    unsigned char extra=0;
    const u32 trailing=at==bytes && !(cancelled && cancelled(context)) ? read(file,&extra,1) : 0;
    const bool correct=!(cancelled && cancelled(context)) && at==bytes && !trailing && !failed(file) && ended(file);
    const int closed=stock<int(void*)>(0x8fed00ae)(file);
    return correct && closed==0;
}
bool persist_attempt(const activation::Control& control,const activation::Limits& limits,Cancelled cancelled,void* context) {
    if (!gui() || (control.phase!=activation::pending && control.phase!=activation::confirmed) ||
        (cancelled && cancelled(context))) return false;
    unsigned char expected[activation::control_bytes],actual[activation::control_bytes];Sha256 hash;
    if (!activation::encode_control(control,limits,expected,hash)) return false;
    const char* path="/fat32/projects/openxy/attempt.oxc";
    void* file=open(path,"wb");
    if (!file) return false;
    if (cancelled && cancelled(context)) { stock<int(void*)>(0x8fed00ae)(file);return false; }
    const u32 written=stock<u32(const void*,u32,u32,void*)>(0x8fed0780)(expected,1,sizeof(expected),file);
    const bool error=failed(file);
    const int closed=stock<int(void*)>(0x8fed00ae)(file);
    if (written!=sizeof(expected) || error || closed!=0 || (cancelled && cancelled(context)) ||
        stock<i32()>(0x8c0c0f00)()!=0 || (cancelled && cancelled(context))) return false;
    bool opened=false;
    if (!read_bytes(path,actual,sizeof(actual),opened,cancelled,context)) return false;
    unsigned char difference=0;
    for (u32 i=0;i<sizeof(actual);++i) difference|=actual[i]^expected[i];
    return !difference;
}
u32 ticks() { return stock<u32()>(0x8c03ee20)(); }
void delay(u32 ticks) { stock<void(u32)>(0x8c040188)(ticks); }
bool gui() { return stock<u32()>(0x8d5c3eac)()!=0; }
}}}
