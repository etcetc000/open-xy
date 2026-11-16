#include "collection_resources.hpp"

namespace opxy { namespace collection { namespace resources {
namespace {
bool equal(const unsigned char* a,const unsigned char* b) {
    unsigned char difference=0;
    for (u32 i=0;i<32;++i) difference|=a[i]^b[i];
    return !difference;
}
Status read(Reader reader, Entry& entry, Files files, Sha256& hash) {
    if (!reader.read || !reader.failed || !reader.ended) return io_error;
    hash.begin(); u32 at=0;
    while (at<entry.bytes) {
        u32 count=entry.bytes-at;
        if (count>4096) count=4096;
        const u32 n=reader.read(reader.context,entry.data+at,count);
        if (!n || n>count) return reader.failed(reader.context) ? io_error : wrong_size;
        for (u32 i=0;i<n;++i) if (!entry.data[at+i]) return invalid_text;
        hash.add(entry.data+at,n); at+=n;
        if (files.progress) files.progress(files.context);
    }
    unsigned char extra;
    const u32 n=reader.read(reader.context,&extra,1);
    if (reader.failed(reader.context) || (!n && !reader.ended(reader.context))) return io_error;
    if (n) return wrong_size;
    unsigned char digest[32]; hash.finish(digest);
    if (!equal(digest,entry.digest)) return checksum;
    entry.data[entry.bytes]=0;
    return ready;
}
}
bool path(char* out, u32 capacity, u32 kind, const unsigned char* digest) {
    if (!out || !digest || capacity<100 || (kind!=activation::svg_resource && kind!=activation::catalog_resource)) return false;
    const char* prefix=kind==activation::svg_resource ? "/fat32/projects/openxy/asset-" : "/fat32/projects/openxy/catalog-";
    u32 n=0;
    while (*prefix) out[n++]=*prefix++;
    const char* digits="0123456789abcdef";
    for (u32 i=0;i<32;++i) { out[n++]=digits[digest[i]>>4]; out[n++]=digits[digest[i]&15]; }
    const char* suffix=".oxc";
    while (*suffix) out[n++]=*suffix++;
    out[n]=0;
    return true;
}
void release(Cache& cache) {
    if (cache.entries) {
        for (u32 i=0;i<cache.count;++i) if (cache.entries[i].data)
            cache.allocator.release(cache.allocator.context,cache.entries[i].data);
        cache.allocator.release(cache.allocator.context,reinterpret_cast<unsigned char*>(cache.entries));
    }
    cache=Cache{};
}
bool matches(const activation::Image& image,const Cache& cache) {
    return image.status==activation::staged && image.version==2 && cache.verified &&
        cache.entries && cache.count==image.resources_count && cache.count &&
        cache.bytes<=maximum_total_bytes && equal(image.digest,cache.image_digest);
}
Status collect(const activation::Image& image, Files files, activation::Allocator allocator,
               Cache& cache, Workspace& workspace) {
    if (cache.entries || cache.count) return cache_in_use;
    if (image.status!=activation::staged || image.version!=2 || !image.resources_count ||
        image.resources_count>activation::maximum_resources || !files.open || !files.close ||
        !allocator.allocate || !allocator.release) return invalid_image;
    u32 total=0;
    for (u32 i=0;i<image.resources_count;++i) {
        activation::Resource descriptor={};
        if (!activation::resource(image,i,descriptor) || descriptor.bytes>maximum_total_bytes-total) return invalid_image;
        total+=descriptor.bytes;
    }
    cache.allocator=allocator;
    cache.entries=reinterpret_cast<Entry*>(allocator.allocate(allocator.context,image.resources_count*sizeof(Entry)));
    if (!cache.entries) { cache=Cache{}; return allocation_failed; }
    cache.count=image.resources_count;
    for (u32 i=0;i<cache.count;++i) cache.entries[i]=Entry{};
    Status result=ready;
    for (u32 i=0;i<cache.count && result==ready;++i) {
        activation::Resource descriptor={}; Entry& entry=cache.entries[i];
        if (!activation::resource(image,i,descriptor) ||
            !path(workspace.path,sizeof(workspace.path),descriptor.kind,descriptor.digest)) {
            result=invalid_image; break;
        }
        for (u32 n=0;n<32;++n) entry.digest[n]=descriptor.digest[n];
        entry.bytes=descriptor.bytes; entry.kind=descriptor.kind;
        entry.data=allocator.allocate(allocator.context,entry.bytes+1);
        if (!entry.data) { result=allocation_failed; break; }
        Reader reader={}; void* handle=nullptr;
        if (!files.open(files.context,workspace.path,reader,handle)) { result=open_failed; break; }
        result=read(reader,entry,files,workspace.hash);
        files.close(files.context,handle);
    }
    if (result!=ready) release(cache);
    else {
        cache.bytes=total;
        for (u32 i=0;i<32;++i) cache.image_digest[i]=image.digest[i];
        cache.verified=true;
    }
    return result;
}
}}}
