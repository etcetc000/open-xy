#pragma once
#include "collection_activation.hpp"
namespace opxy { namespace collection { namespace activation {
struct HandoffMemory {
    unsigned char* bytes;
    void* context;
    bool (*synchronize)(void*,u32,u32);
};
enum HandoffResult { untouched, overwritten, synchronized };
// Main must irrevocably own commit before calling. The complete fixed payload
// partition is cleared, but resident code and the table tail are never written.
// Any result after commit still requires reset unless BOTH roles publish.
HandoffResult install(const Image&,const Limits&,bool commit_owned,HandoffMemory,Sha256&);
}}}
