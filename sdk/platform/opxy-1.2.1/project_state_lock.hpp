#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
inline bool project_state_try_lock(volatile u8* flag) {
    u32 acquired;
    __asm__ volatile("P0 = %1; TESTSET (P0); %0 = CC;"
        : "=d"(acquired) : "a"(flag) : "P0", "CC", "memory");
    __asm__ volatile("SSYNC;" ::: "memory");
    return acquired != 0;
}
inline void project_state_unlock(volatile u8* flag) {
    __asm__ volatile("SSYNC;" ::: "memory");
    *flag = 0;
}
}}
