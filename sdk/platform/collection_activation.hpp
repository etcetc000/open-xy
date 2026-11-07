// Fixed-address whole-collection staging. This code never writes executable RAM.
#pragma once
#include "collection_probe.hpp"

namespace opxy { namespace collection { namespace activation {
const u32 header_bytes = 160, maximum_entries = 192, maximum_resources = 64, resource_bytes = 40;
const u32 maximum_resource_total_bytes=2u*1024u*1024u;
const u32 maximum_svg_bytes=256u*1024u,maximum_catalog_bytes=64u*1024u;
enum Status {
    staged, truncated, malformed, incompatible, outside_pool, checksum,
    trailing, io_error, allocation_failed, invalid_entry, invalid_resources
};
struct Limits {
    u32 base, bytes, entries;
    unsigned char platform[32], abi[32];
};
struct Allocator {
    void* context;
    unsigned char* (*allocate)(void*, u32);
    void (*release)(void*, unsigned char*);
};
struct Image {
    Status status;
    unsigned char* bytes;
    u32 initialized, bss_offset, bss_bytes, text_bytes, entries;
    u32 version, resources_offset, resources_count;
    unsigned char digest[32];
};
enum ResourceKind { svg_resource=1, catalog_resource=2 };
struct Resource { const unsigned char* digest; u32 bytes, kind; };
// Descriptor bytes are borrowed from a successfully staged image and stay valid
// only while the caller owns that image. Activation requires a v2 resource table.
bool resource(const Image&, u32 index, Resource&);
struct Workspace { unsigned char header[header_bytes]; Sha256 hash; };
// Allocate only after header admission. On every failure release the allocation;
// on success the caller owns it and must release it after the activation decision.
Image stage(Reader, const Limits&, Allocator, Workspace&);

struct Peer {
    u32 epoch;
    bool prepared;
    unsigned char platform[32], abi[32], digest[32];
};
enum Decision { embedded, prepare_attempt, handoff_ready, reset_required };
struct Gate {
    bool bypass, previous_attempt, quiescent;
    u32 epoch;
    bool resources_ready;
};
// A prepared peer must come from the live boot's resident transport, never a
// cached status file. Persist/read back the attempt before any executable write.
Decision decide(const Image&, const Limits&, const Gate&, const Peer&);
// Once payload RAM is overwritten, returning through baseline pointers is unsafe.
// Failure after copying requires a coordinated reset, not an in-place fallback.
Decision after_copy(bool cache_synchronized, bool peer_committed);

enum Phase { disabled, armed, pending, confirmed, verify_only };
struct Control { u32 phase, generation, slot; unsigned char digest[32]; };
const u32 control_bytes = 160;
// Control and attempt records have independent checksums. A present invalid
// attempt record must be treated as a failed boot, not as a missing record.
bool read_control(const unsigned char*, u32, const Limits&, Control&, Sha256&);
bool encode_control(const Control&, const Limits&, unsigned char* out, Sha256&);
bool rollback_needed(const Control& selection, bool attempt_present,
                     bool attempt_valid, const Control& attempt);
}}}
