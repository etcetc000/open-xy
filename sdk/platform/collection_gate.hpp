// One startup coordinator owns Gate. The RPC task exchanges only mailbox data;
// it never changes gate state, waits, accesses files or copies executable code.
#pragma once
#include "collection_probe.hpp"

namespace opxy { namespace collection { namespace activation {
enum GatePhase { waiting, holding, leased, frozen, copy_authorized, local_copied,
                 published, baseline, failed_hold, intent_holding, intent_leased };
enum GateOperation { claim=1, platform_word=0x10, abi_word=0x20, digest_word=0x30,
                     generation=0x40, slot=0x41, request_mode=0x42, freeze=0x50, copy=0x51,
                     query=0x52, publish=0x53, cancel=0x54 };
enum GateReply { busy=0, accepted=1, unavailable=2, wrong_epoch=3, refused=4 };
struct Reply { u32 status, value; };
struct Identity {
    unsigned char platform[32], abi[32], digest[32];
    u32 generation, slot, request_mode;
};
class StartupGate {
    Identity identity_;
    u32 phase_, epoch_, start_, timeout_;
public:
    // Zero-initialize once at boot; no static constructor is needed.
    // Main-thread operations only. Time is a wrapping monotonic tick
    // count; timeout is positive and less than half its range. enter is once;
    // claims and repeated requests never extend its pre-copy deadline.
    bool enter(const Identity&, bool staged, u32 now, u32 timeout);
    // Agree the control/old-attempt decision before creating the pending marker
    // and doing bulk work. An intent lease cannot freeze or authorize copying.
    bool enter_intent(const Identity&, bool eligible, u32 now, u32 timeout);
    bool prepared(bool staged_and_resources_verified);
    void tick(u32 now);
    Reply request(u32 epoch, u32 operation, u32 now);
    bool copied(bool cache_synchronized);
    u32 phase() const { return phase_; }
    bool return_embedded() const { return phase_ == baseline; }
    bool return_selected() const { return phase_ == published; }
};

class GateMailbox {
    // This is a same-processor task mailbox, never interprocessor shared RAM.
    // Handler writes epoch/op then pending last; main writes reply then done
    // last. A pending command cannot be replaced, including by another epoch.
    volatile u32 state_, ticket_, epoch_, operation_, status_, value_;
public:
    // Zero-initialize once at boot; no static constructor is needed.
    // A fresh observation needs the next ticket, including repeated query.
    // Same-ticket replies are immutable receipts, never live lease evidence.
    // Tickets start at one, increase exactly once per command and never wrap.
    Reply exchange(u32 ticket, u32 epoch, u32 operation); // Handler only; immediate.
    bool take(u32& ticket, u32& epoch, u32& operation) const; // Coordinator only.
    bool complete(u32 ticket, Reply); // An old completion cannot answer a new command.
};

// Main-task ownership of asynchronous worker buffers. A logical baseline
// decision in StartupGate is NOT permission to resume it while bulk I/O still
// owns memory. Post-copy uncertainty is terminal until a device reset.
class BootBarrier {
    enum State { small, bulk, cleaning, committed, adopted, cancelled, restart };
    u32 state_;
    bool execute_;
public:
    // Zero-initialize at boot. Set bulk ownership BEFORE posting worker permit.
    bool allow_bulk(bool pending_persisted, bool execution_requested);
    bool allow_commit(bool preparations_verified);
    void cancel();
    bool worker_cleaned();
    void cleanup_timeout();
    bool adopt(bool both_published);
    bool abort_worker() const { return state_==cleaning || state_==cancelled; }
    bool return_embedded() const { return state_==cancelled; }
    bool return_selected() const { return state_==adopted; }
    bool requires_reset() const { return state_==restart; }
};
}}}
