#pragma once

namespace opxy { namespace platform {
// Completes earlier stores before later ones (SSYNC) and keeps the compiler
// from moving memory accesses across it. Orders publication between the stock
// worker, GUI and audio contexts.
inline void barrier() {
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    __builtin_bfin_ssync();
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
}
}}
