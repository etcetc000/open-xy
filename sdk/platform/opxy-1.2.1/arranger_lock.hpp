#ifndef OPXY_ARRANGER_LOCK_HPP
#define OPXY_ARRANGER_LOCK_HPP
#include <opxy/types.hpp>
#include "barrier.hpp"
namespace opxy { namespace platform {
// BF70x Programming Reference 8-162: atomic byte test-and-set, CC=1 if zero.
// GCC8 __atomic_test_and_set emits a non-atomic load/store on this target.
inline bool arranger_try_lock(volatile u8* flag) {
    u32 acquired;
    // The generic address constraint also permits FP/SP, illegal for TESTSET.
    __asm__ volatile("P0 = %1; TESTSET (P0); %0 = CC;"
        : "=d"(acquired) : "a"(flag) : "P0", "CC", "memory");
    barrier();
    return acquired != 0;
}
}}
#endif
