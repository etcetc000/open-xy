#include "collection_resource_stock.hpp"
#include "collection_io.hpp"
#include "stock.hpp"
using namespace opxy;
using namespace opxy::platform;
using namespace opxy::collection;
extern "C" {
u32 sdk_collection_try_lock(volatile u32*) __asm__("sdk_collection_try_lock");
void sdk_collection_unlock(volatile u32*) __asm__("sdk_collection_unlock");
}
namespace {
const char fallback[]="content/display/_openxy_loader_1.svg";
bool same(const char* a,const char* b) {
    if (!a || !b) return false;
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}
bool logical_asset(const char* path,unsigned char* digest) {
    if (!path) return false;
    const char* prefix="content/display/openxy-";
    while (*prefix) if (*path++!=*prefix++) return false;
    for (u32 i=0;i<64;++i) {
        const unsigned char c=static_cast<unsigned char>(*path++);
        const u32 n=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : 255;
        if (n>15) return false;
        if (!(i&1)) digest[i/2]=static_cast<unsigned char>(n<<4);
        else digest[i/2]|=static_cast<unsigned char>(n);
    }
    return same(path,".svg");
}
void* original_svg(const char* path) { return stock<void*(const char*)>(0x8d5f2400)(path); }
void* fallback_svg() { sdk_collection_resource_fallback(1);return original_svg(fallback); }
const resources::Entry* find_svg(const resources::Cache& cache,const unsigned char* digest) {
    if (!cache.verified || !cache.entries || cache.count>activation::maximum_resources) return nullptr;
    for (u32 i=0;i<cache.count;++i) {
        const resources::Entry& e=cache.entries[i];
        unsigned char difference=0;
        for (u32 n=0;n<32;++n) difference|=e.digest[n]^digest[n];
        if (e.kind==activation::svg_resource && !difference) return &e;
    }
    return nullptr;
}
}
void* sdk_collection_asset_load(const char* path) {
    unsigned char digest[32];
    const bool owned=logical_asset(path,digest);
    resource_stock::View* view=sdk_collection_selected_resources();
    if (!view) return owned ? fallback_svg() : original_svg(path);
    // Every parse at this call site shares stock's temporary parser state,
    // including ordinary files and the protected fallback. A contended lock
    // cannot be resolved by launching a second, unlocked fallback parse.
    u32 waits=0;
    while (!sdk_collection_try_lock(&view->lock)) {
        if (waits<100) ++waits;
        io::delay(1);
    }
    void* document=nullptr;
    if (!owned) document=original_svg(path);
    else {
        const resources::Entry* e=view->cache ? find_svg(*view->cache,digest) : nullptr;
        if (waits<100 && e && e->data && e->bytes && e->bytes<=activation::maximum_svg_bytes &&
            view->working && e->bytes<view->working_bytes) {
            for (u32 i=0;i<=e->bytes;++i) view->working[i]=e->data[i];
            document=stock<void*(char*)>(0x8d5f2160)(reinterpret_cast<char*>(view->working));
        }
        if (!document) document=fallback_svg();
    }
    sdk_collection_unlock(&view->lock);return document;
}
bool sdk_collection_catalog_load(const char* path,void* out) {
    resource_stock::View* view=sdk_collection_selected_resources();
    if (!same(path,"content/op1_factory.json") || !view)
        return stock<bool(const char*,void*)>(0x8c042490)(path,out);
    if (!view->cache || !view->cache->verified || !view->cache->entries || !view->cache->count ||
        view->cache->count>activation::maximum_resources || !view->catalog) {
        sdk_collection_resource_fallback(2);
        return stock<bool(const char*,void*)>(0x8c042490)(path,out);
    }
    // The existing call-site ABI supplies an already constructed Json. Stock
    // copy assignment constructs its temporary before swapping into that value.
    stock<void*(void*,const void*)>(0x8c054098)(out,view->catalog);
    return true;
}
namespace opxy { namespace collection { namespace resource_stock {
namespace {
bool bounded_json(const unsigned char* data,u32 bytes) {
    u32 depth=0;bool quoted=false,escape=false;
    // Stock's parser recurses for objects/arrays. Bound that recursion before
    // entering it on the fixed helper stack; quoted braces do not add depth.
    for (u32 i=0;i<bytes;++i) {
        const unsigned char c=data[i];
        if (quoted) {
            if (c<32) return false;
            if (escape) escape=false;
            else if (c=='\\') escape=true;
            else if (c=='"') quoted=false;
        } else if (c=='"') quoted=true;
        else if (c=='{' || c=='[') { if (++depth>8) return false; }
        else if (c=='}' || c==']') { if (!depth) return false;--depth; }
    }
    return !quoted && !depth;
}
}
void* prepare_catalog(const resources::Cache& cache,activation::Allocator allocator,bool persisted) {
    if (!persisted || !cache.verified || !cache.entries || cache.count>activation::maximum_resources ||
        !allocator.allocate || !allocator.release) return nullptr;
    const resources::Entry* entry=nullptr;
    for (u32 i=0;i<cache.count;++i) if (cache.entries[i].kind==activation::catalog_resource) {
        if (entry) return nullptr;
        entry=&cache.entries[i];
    }
    if (!entry || !entry->data || !entry->bytes || entry->bytes>activation::maximum_catalog_bytes ||
        !bounded_json(entry->data,entry->bytes)) return nullptr;
    unsigned char* value=allocator.allocate(allocator.context,12);
    if (!value) return nullptr;
    unsigned char* input=allocator.allocate(allocator.context,entry->bytes+1);
    if (!input) { allocator.release(allocator.context,value);return nullptr; }
    for (u32 i=0;i<=entry->bytes;++i) input[i]=entry->data[i];
    alignas(4) unsigned char reader[120];
    stock<void(void*,u32)>(0x8c0555c0)(value,0);
    stock<void(void*)>(0x8c050de8)(reader);
    const bool parsed=stock<bool(void*,const char*,const char*,void*,bool)>(0x8c052230)(
        reader,reinterpret_cast<const char*>(input),reinterpret_cast<const char*>(input+entry->bytes),value,false);
    stock<void(void*)>(0x8c042420)(reader);
    allocator.release(allocator.context,input);
    if (!parsed) { destroy_catalog(value,allocator);return nullptr; }
    return value;
}
void destroy_catalog(void* value,activation::Allocator allocator) {
    if (value) {
        stock<void(void*)>(0x8c053fc0)(value);
        allocator.release(allocator.context,static_cast<unsigned char*>(value));
    }
}
}}}
