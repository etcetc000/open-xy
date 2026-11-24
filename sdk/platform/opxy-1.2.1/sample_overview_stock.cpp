// Stock OS 1.2.1 side of the sample overview: a job on the file queue, for a screen
// opened with the worker idle and its sample already cached. Device-only: the host
// test fixture supplies its own double. Derived from #83 (etcetc000).
#include "sample_overview.hpp"
#include "barrier.hpp"
#include "stock.hpp"

namespace {
volatile opxy::u32 queued;
void prepare_job(opxy::u32) {
    // Cleared first, so a newer request during this pass can queue another.
    queued = 0;
    opxy::platform::barrier();
    sdk_sample_overview_prepare();
}
}

void sdk_sample_overview_wake() {
    if (queued) return;
    queued = 1;
    opxy::platform::barrier();
    // 8d4dd728 (context, 0, job, 0): how the sample manager submits to the same
    // queue at 8d509d38 and 8d50c3ec. Stock allocates the job. GUI only.
    opxy::platform::stock<void(opxy::u32, opxy::u32, void (*)(opxy::u32), opxy::u32)>(0x8d4dd728)(0, 0, &prepare_job, 0);
}
