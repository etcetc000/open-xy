#pragma once
#include "collection_resources.hpp"
namespace opxy { namespace collection { namespace resource_stock {
struct View {
    const resources::Cache* cache;
    unsigned char* working;
    u32 working_bytes;
    const void* catalog;
    volatile u32 lock;
};
// Native parsing can allocate internally. Call only after the pending attempt
// has been persisted; the returned stock Json remains independently owned.
void* prepare_catalog(const resources::Cache&,activation::Allocator,bool pending_persisted);
void destroy_catalog(void*,activation::Allocator);
}}}
extern "C" {
opxy::collection::resource_stock::View* sdk_collection_selected_resources() __asm__("sdk_collection_selected_resources");
void sdk_collection_resource_fallback(opxy::u32) __asm__("sdk_collection_resource_fallback");
void* sdk_collection_asset_load(const char*) __asm__("sdk_collection_asset_load");
bool sdk_collection_catalog_load(const char*,void*) __asm__("sdk_collection_catalog_load");
}
