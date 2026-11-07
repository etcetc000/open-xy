#pragma once
#include "types.hpp"

namespace opxy { namespace divide {
// SDK-owned platform boundary. Engine source always calls the same operation.
//
// The exact quotient n / d when the divisor is below 32768 and the quotient
// below 65536 (n < d * 65536); the caller checks both, as any other values
// are undefined. On the device this is 17 single-cycle divide steps inline,
// where a general 32-bit division is a call of tens of cycles. Typical uses
// are sub-sample positions: a phase past an edge over a 16-bit increment.
inline u32 quotient16(u32 n, u32 d) {
#if defined(__bfin__)
    // DIVS then 16 DIVQ: non-restoring division of n by d's low half, one
    // quotient bit per step into n's low half. Both operands are positive in
    // 16-bit signed terms (n's high half is below d), so the signed first
    // step clears AQ exactly as unsigned division needs.
    __asm__("DIVS (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); "
            "DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); "
            "DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); DIVQ (%0, %1); "
            "DIVQ (%0, %1); DIVQ (%0, %1);" : "+d"(n) : "d"(d) : "ASTAT");
    return n & 0xffffu;
#else
    return n / d;
#endif
}
}} // namespace opxy::divide
