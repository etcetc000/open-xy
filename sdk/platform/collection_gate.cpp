#include "collection_gate.hpp"

namespace opxy { namespace collection { namespace activation {
namespace {
Reply reply(u32 status, u32 value=0) { Reply result={status,value}; return result; }
u32 word_at(const unsigned char* bytes, u32 index) {
    const unsigned char* p=bytes+index*4;
    return u32(p[0]) | u32(p[1])<<8 | u32(p[2])<<16 | u32(p[3])<<24;
}
}
bool StartupGate::enter(const Identity& identity, bool staged, u32 now, u32 timeout) {
    if (phase_!=waiting) return false;
    if (!staged || !timeout || timeout>=0x80000000u || !identity.generation || identity.slot>1) {
        phase_=baseline;
        return false;
    }
    identity_=identity; start_=now; timeout_=timeout; phase_=holding;
    return true;
}
bool StartupGate::enter_intent(const Identity& identity,bool eligible,u32 now,u32 timeout) {
    if (!enter(identity,eligible,now,timeout)) return false;
    phase_=intent_holding;return true;
}
bool StartupGate::prepared(bool verified) {
    if (phase_!=intent_leased) return false;
    phase_=verified ? leased : baseline;
    return verified;
}
void StartupGate::tick(u32 now) {
    // Copy authorization is the point of no return. No subsequent timeout or
    // cancellation can resume code from the overwritten embedded collection.
    if ((phase_==holding || phase_==leased || phase_==frozen || phase_==intent_holding || phase_==intent_leased) &&
        u32(now-start_)>=timeout_)
        phase_=baseline;
}
Reply StartupGate::request(u32 epoch, u32 operation, u32 now) {
    tick(now);
    if (!epoch) return reply(wrong_epoch);
    if (phase_==waiting || phase_==baseline) return reply(unavailable,phase_);
    if (operation==claim) {
        if (phase_==holding) { epoch_=epoch; phase_=leased; }
        else if (phase_==intent_holding) { epoch_=epoch; phase_=intent_leased; }
        return epoch_==epoch ? reply(accepted,phase_) : reply(wrong_epoch);
    }
    if (epoch_!=epoch) return reply(wrong_epoch);
    if (operation>=platform_word && operation<platform_word+8)
        return reply(accepted,word_at(identity_.platform,operation-platform_word));
    if (operation>=abi_word && operation<abi_word+8)
        return reply(accepted,word_at(identity_.abi,operation-abi_word));
    if (operation>=digest_word && operation<digest_word+8)
        return reply(accepted,word_at(identity_.digest,operation-digest_word));
    if (operation==generation) return reply(accepted,identity_.generation);
    if (operation==slot) return reply(accepted,identity_.slot);
    if (operation==request_mode) return reply(accepted,identity_.request_mode);
    if (operation==query) return reply(accepted,phase_);
    if (operation==freeze) {
        if (phase_==leased) phase_=frozen;
        return phase_==frozen ? reply(accepted,phase_) : reply(refused,phase_);
    }
    if (operation==copy) {
        if (phase_==frozen) phase_=copy_authorized;
        return phase_==copy_authorized || phase_==local_copied || phase_==published ?
            reply(accepted,copy_authorized) : reply(refused,phase_);
    }
    if (operation==publish) {
        if (phase_==local_copied) phase_=published;
        return phase_==published ? reply(accepted,phase_) : reply(refused,phase_);
    }
    if (operation==cancel && (phase_==leased || phase_==frozen || phase_==intent_leased)) {
        phase_=baseline;
        return reply(accepted,phase_);
    }
    return reply(refused,phase_);
}
bool StartupGate::copied(bool cache_synchronized) {
    if (phase_!=copy_authorized) return false;
    phase_=cache_synchronized ? local_copied : failed_hold;
    return cache_synchronized;
}

Reply GateMailbox::exchange(u32 ticket, u32 epoch, u32 operation) {
    const u32 state=state_;
    if (state==1) return reply(busy);
    if (state==2 && ticket_==ticket)
        return epoch_==epoch && operation_==operation ? reply(status_,value_) : reply(refused);
    if (!ticket || ticket_==0xffffffffu || ticket!=ticket_+1) return reply(refused);
    ticket_=ticket; epoch_=epoch; operation_=operation; state_=1;
    return reply(busy);
}
bool GateMailbox::take(u32& ticket, u32& epoch, u32& operation) const {
    if (state_!=1) return false;
    ticket=ticket_; epoch=epoch_; operation=operation_;
    return true;
}
bool GateMailbox::complete(u32 ticket, Reply result) {
    if (state_!=1 || ticket_!=ticket) return false;
    status_=result.status; value_=result.value; state_=2;
    return true;
}

bool BootBarrier::allow_bulk(bool persisted,bool execution) {
    if (state_!=small || !persisted) return false;
    execute_=execution;state_=bulk;return true;
}
bool BootBarrier::allow_commit(bool verified) {
    if (state_!=bulk || !execute_ || !verified) return false;
    state_=committed;return true;
}
void BootBarrier::cancel() {
    if (state_==small) state_=cancelled;
    else if (state_==bulk) state_=cleaning;
    else if (state_==committed || state_==adopted) state_=restart;
}
bool BootBarrier::worker_cleaned() {
    if (state_!=cleaning) return false;
    state_=cancelled;return true;
}
void BootBarrier::cleanup_timeout() { if (state_==cleaning) state_=restart; }
bool BootBarrier::adopt(bool both_published) {
    if (state_!=committed) return false;
    state_=both_published ? adopted : restart;
    return both_published;
}
}}}
