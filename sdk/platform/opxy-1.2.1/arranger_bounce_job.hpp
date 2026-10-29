#pragma once
#include <opxy/types.hpp>
extern "C" {
bool sdk_arranger_bounce_job_begin(opxy::u32 engine, opxy::u32 track)
    __asm__("sdk_arranger_bounce_job_begin");
bool sdk_arranger_bounce_job_busy() __asm__("sdk_arranger_bounce_job_busy");
void sdk_arranger_bounce_job_cancel() __asm__("sdk_arranger_bounce_job_cancel");
void sdk_arranger_bounce_destination_apply(void* job)
    __asm__("sdk_arranger_bounce_destination_apply");
}
