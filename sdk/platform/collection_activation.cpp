#include "collection_activation.hpp"

namespace opxy { namespace collection { namespace activation {
namespace {
u32 little(const unsigned char* p) {
    return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
}
void store(unsigned char* out,u32 value) {
    for (u32 i=0;i<4;++i) out[i]=static_cast<unsigned char>(value>>(i*8));
}
bool equal(const unsigned char* a, const unsigned char* b, u32 count) {
    unsigned char difference=0;
    while (count--) difference |= *a++ ^ *b++;
    return !difference;
}
bool exact(Reader r, unsigned char* out, u32 bytes) {
    while (bytes) {
        const u32 n=r.read(r.context,out,bytes);
        if (!n || n>bytes) return false;
        out+=n; bytes-=n;
    }
    return true;
}
bool entries_valid(const unsigned char* bytes, const Image& image) {
    const u32 prefix=image.entries*4;
    for (u32 i=0; i<image.entries; ++i) {
        const u32 instruction=little(bytes+i*4);
        // Unconditional four-byte PC-relative JUMP.L, never CALL or a short jump.
        if ((instruction & 0xff00u)!=0xe200u) return false;
        i32 delta=i32((instruction & 255u) << 16 | instruction >> 16);
        if (delta & 0x800000) delta-=0x1000000;
        const i32 target=i32(i*4)+delta*2;
        if (target<i32(prefix) || u32(target)>image.text_bytes-2) return false;
    }
    return true;
}
bool resources_valid(const Image& image) {
    if (image.version==1) return true;
    u32 catalogs=0,total=0;
    const unsigned char zero[32]={};
    for (u32 i=0;i<image.resources_count;++i) {
        const unsigned char* p=image.bytes+image.resources_offset+i*resource_bytes;
        const u32 bytes=little(p+32),kind=little(p+36);
        if ((kind!=svg_resource && kind!=catalog_resource) || !bytes ||
            bytes>(kind==svg_resource ? maximum_svg_bytes : maximum_catalog_bytes) || equal(p,zero,32)) return false;
        if (bytes>maximum_resource_total_bytes-total) return false;
        total+=bytes;
        if (kind==catalog_resource) ++catalogs;
        for (u32 j=0;j<i;++j) {
            const unsigned char* previous=image.bytes+image.resources_offset+j*resource_bytes;
            if (little(previous+36)==kind && equal(previous,p,32)) return false;
        }
    }
    return catalogs==1;
}
}

bool resource(const Image& image, u32 index, Resource& out) {
    out=Resource{};
    if (image.status!=staged || !image.bytes || image.version!=2 || !image.resources_count ||
        image.resources_count>maximum_resources || index>=image.resources_count ||
        (image.resources_offset & 3) || image.resources_offset<image.text_bytes ||
        image.resources_offset>image.initialized ||
        image.resources_count>(image.initialized-image.resources_offset)/resource_bytes) return false;
    const unsigned char* p=image.bytes+image.resources_offset+index*resource_bytes;
    const u32 bytes=little(p+32),kind=little(p+36);
    const unsigned char zero[32]={};
    if ((kind!=svg_resource && kind!=catalog_resource) || !bytes ||
        bytes>(kind==svg_resource ? maximum_svg_bytes : maximum_catalog_bytes) || equal(p,zero,32)) return false;
    out.digest=p; out.bytes=bytes; out.kind=kind;
    return true;
}

Image stage(Reader reader, const Limits& limits, Allocator allocator, Workspace& workspace) {
    Image image={}; image.status=io_error;
    if (!reader.read || !reader.failed || !reader.ended || !allocator.allocate || !allocator.release)
        return image;
    unsigned char* h=workspace.header;
    image.status=truncated;
    if (!exact(reader,h,header_bytes)) {
        if (reader.failed(reader.context)) image.status=io_error;
        return image;
    }
    image.status=malformed;
    const unsigned char magic[8]={'O','X','Y','L','O','A','D','1'};
    image.version=little(h+8);
    if (!equal(h,magic,8) || (image.version!=1 && image.version!=2) || little(h+12)!=header_bytes || little(h+40)!=1 || little(h+44))
        return image;
    if (image.version==1) {
        for (u32 i=144; i<header_bytes; ++i) if (h[i]) return image;
    } else {
        image.resources_offset=little(h+144); image.resources_count=little(h+148);
        if (!image.resources_count || image.resources_count>maximum_resources ||
            little(h+152)!=resource_bytes || little(h+156)) return image;
    }
    image.status=incompatible;
    if (!equal(h+48,limits.platform,32) || !equal(h+80,limits.abi,32) || little(h+36)!=limits.entries)
        return image;
    image.initialized=little(h+20); image.bss_offset=little(h+24);
    image.bss_bytes=little(h+28); image.text_bytes=little(h+32); image.entries=little(h+36);
    image.status=outside_pool;
    if (!limits.bytes || limits.base>0xffffffffu-limits.bytes || (limits.base & 31) ||
        little(h+16)!=limits.base || !image.entries || image.entries>maximum_entries ||
        image.entries*4>=image.text_bytes || (image.text_bytes & 1) || image.text_bytes>image.initialized ||
        image.initialized>limits.bytes || image.bss_offset<image.initialized ||
        image.bss_offset>limits.bytes || image.bss_bytes>limits.bytes-image.bss_offset ||
        ((image.bss_offset | image.bss_bytes) & 3)) return image;
    if (image.version==2 && ((image.resources_offset & 3) || image.resources_offset<image.text_bytes ||
        image.resources_offset>image.initialized ||
        image.resources_count>(image.initialized-image.resources_offset)/resource_bytes)) return image;
    image.status=allocation_failed;
    image.bytes=allocator.allocate(allocator.context,image.initialized);
    if (!image.bytes) return image;
    for (u32 i=0; i<32; ++i) { image.digest[i]=h[112+i]; h[112+i]=0; }
    workspace.hash.begin(); workspace.hash.add(h,header_bytes);
    image.status=truncated;
    if (!exact(reader,image.bytes,image.initialized)) {
        if (reader.failed(reader.context)) image.status=io_error;
    } else {
        workspace.hash.add(image.bytes,image.initialized);
        unsigned char extra;
        const u32 n=reader.read(reader.context,&extra,1);
        if (reader.failed(reader.context) || (!n && !reader.ended(reader.context))) image.status=io_error;
        else if (n) image.status=trailing;
        else {
            unsigned char digest[32]; workspace.hash.finish(digest);
            if (!equal(digest,image.digest,32)) image.status=checksum;
            else if (!entries_valid(image.bytes,image)) image.status=invalid_entry;
            else image.status=resources_valid(image) ? staged : invalid_resources;
        }
    }
    if (image.status!=staged) {
        allocator.release(allocator.context,image.bytes);
        image.bytes=nullptr;
    }
    return image;
}

Decision decide(const Image& image, const Limits& limits, const Gate& gate, const Peer& peer) {
    if (gate.bypass || gate.previous_attempt || !gate.quiescent || !gate.epoch || !gate.resources_ready ||
        image.status!=staged || !image.bytes || image.version!=2 || !image.resources_count ||
        !peer.prepared || peer.epoch!=gate.epoch ||
        !equal(peer.platform,limits.platform,32) || !equal(peer.abi,limits.abi,32) ||
        !equal(peer.digest,image.digest,32)) return embedded;
    return prepare_attempt;
}

Decision after_copy(bool cache_synchronized, bool peer_committed) {
    // This is admission for the resident handoff, not a boot-health confirmation.
    return cache_synchronized && peer_committed ? handoff_ready : reset_required;
}

bool read_control(const unsigned char* data, u32 bytes, const Limits& limits, Control& out, Sha256& hash) {
    out=Control{};
    const unsigned char magic[8]={'O','X','Y','C','T','L','0','1'};
    if (!data || bytes!=control_bytes || !equal(data,magic,8) || little(data+8)!=1 ||
        little(data+12)>verify_only || !little(data+16) || little(data+20)>1 ||
        !equal(data+32,limits.platform,32) || !equal(data+64,limits.abi,32)) return false;
    for (u32 i=24;i<32;++i) if (data[i]) return false;
    const unsigned char zero[32]={}; unsigned char digest[32];
    hash.begin(); hash.add(data,128); hash.add(zero,32); hash.finish(digest);
    if (!equal(digest,data+128,32)) return false;
    const u32 phase=little(data+12), slot=little(data+20);
    const bool empty=equal(data+96,zero,32);
    if ((phase==disabled && (!empty || slot)) || (phase!=disabled && empty)) return false;
    out.phase=phase; out.generation=little(data+16); out.slot=slot;
    for (u32 i=0;i<32;++i) out.digest[i]=data[96+i];
    return true;
}

bool encode_control(const Control& control,const Limits& limits,unsigned char* out,Sha256& hash) {
    if (!out) return false;
    for (u32 i=0;i<control_bytes;++i) out[i]=0;
    const unsigned char zero[32]={};
    const bool empty=equal(control.digest,zero,32);
    if (control.phase>verify_only || !control.generation || control.slot>1 ||
        (control.phase==disabled ? (!empty || control.slot) : empty)) return false;
    const unsigned char magic[8]={'O','X','Y','C','T','L','0','1'};
    for (u32 i=0;i<8;++i) out[i]=magic[i];
    store(out+8,1);store(out+12,control.phase);store(out+16,control.generation);store(out+20,control.slot);
    for (u32 i=0;i<32;++i) { out[32+i]=limits.platform[i];out[64+i]=limits.abi[i];out[96+i]=control.digest[i]; }
    hash.begin();hash.add(out,control_bytes);hash.finish(out+128);
    return true;
}

bool rollback_needed(const Control& selection, bool attempt_present, bool attempt_valid, const Control& attempt) {
    if (selection.phase!=armed || !selection.generation) return true;
    if (!attempt_present) return false;
    if (!attempt_valid || (attempt.phase!=pending && attempt.phase!=confirmed) ||
        attempt.generation>selection.generation) return true;
    if (attempt.generation<selection.generation) return false;  // A new explicit install/retry request.
    return attempt.phase!=confirmed || attempt.slot!=selection.slot ||
           !equal(attempt.digest,selection.digest,32);
}
}}}
