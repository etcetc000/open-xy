#ifndef OPXY_ARRANGER_LOCK_HPP
#define OPXY_ARRANGER_LOCK_HPP
#include <opxy/types.hpp>
#include "barrier.hpp"
namespace opxy { namespace platform {
// BF70x Programming Reference 8-162: atomic byte test-and-set, CC=1 if zero.
// GCC8 __atomic_test_and_set emits a non-atomic load/store on this target.
inline bool arranger_try_lock(volatile u8* flag) {
    u32 acquired;
    __asm__ volatile("TESTSET (%1); %0 = CC;" : "=d"(acquired) : "a"(flag) : "CC", "memory");
    barrier();
    return acquired != 0;
}
}}
#endif
