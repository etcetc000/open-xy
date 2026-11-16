// Resource files are read and checked once before activation. Retain canonical
// bytes for the entire selected boot; lazy native parsers receive working copies.
#pragma once
#include "collection_activation.hpp"

namespace opxy { namespace collection { namespace resources {
enum Status { ready, invalid_image, open_failed, io_error, wrong_size, checksum,
              allocation_failed, invalid_text, cache_in_use };
const u32 maximum_total_bytes=activation::maximum_resource_total_bytes;
struct Files {
    void* context;
    bool (*open)(void*, const char*, Reader&, void*& handle);
    void (*close)(void*, void* handle);
    void (*progress)(void*); // Optional task yield; never called from an RPC handler.
};
struct Entry { unsigned char digest[32]; unsigned char* data; u32 bytes, kind; };
struct Cache {
    Entry* entries; u32 count, bytes;
    unsigned char image_digest[32];
    bool verified;
    activation::Allocator allocator;
};
struct Workspace { char path[128]; Sha256 hash; };
Status collect(const activation::Image&, Files, activation::Allocator, Cache&, Workspace&);
void release(Cache&);
bool matches(const activation::Image&, const Cache&);
// All path components are generated from a kind and the full digest.
bool path(char* out, u32 capacity, u32 kind, const unsigned char* digest);
}}}
