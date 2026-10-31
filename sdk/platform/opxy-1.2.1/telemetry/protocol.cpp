#include "protocol.hpp"

namespace opxy_transport { namespace telemetry {
namespace {
u16 get16(const u8* p) { return u16(unsigned(p[0]) | (unsigned(p[1]) << 8)); }
u32 get32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
void put16(u8* p, u16 x) { p[0] = u8(x); p[1] = u8(x >> 8); }
void put32(u8* p, u32 x) { for (unsigned i = 0; i < 4; ++i) p[i] = u8(x >> (8 * i)); }
bool same(Key a, Key b) { return a.engine == b.engine && a.track == b.track && a.generation == b.generation; }
bool due(u32 now, u32 deadline) { return now - deadline < 0x80000000u; }
bool accumulates(Mode m) { return m == Mode::AccumulatePacked16 || m == Mode::Packed16AndSnapshot; }
unsigned prefix_for(Mode m, unsigned prefix) { return m == Mode::Persistent ? 0 : m == Mode::AccumulatePacked16 ? 1 : prefix; }
bool valid_prefix(Mode m, unsigned prefix, unsigned words) { return m != Mode::Packed16AndSnapshot || (prefix > 0 && prefix <= 24 && prefix < words); }
bool valid_mode(Mode m) { return m == Mode::Persistent || accumulates(m); }
bool valid_words(unsigned n, Mode m) { return n > 0 && n <= max_words && (m != Mode::AccumulatePacked16 || n == 1) && (m != Mode::Packed16AndSnapshot || n >= 2); }
unsigned chunks(unsigned n) { return (n + fragment_words - 1) / fragment_words; }
unsigned chunk_size(unsigned total, unsigned offset) { return total - offset < fragment_words ? total - offset : fragment_words; }
u32 advance(u32 x) { ++x; return x == 0 ? 1 : x; }
int lane(u32 x) { unsigned v = x & 0xffffu; return v < 32768 ? int(v) : int(v) - 65536; }
int clamp(int x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }
}
u32 packed_add(u32 a, u32 b) {
    return (u32(clamp(lane(a) + lane(b))) & 0xffffu) |
           (u32(clamp(lane(a >> 16) + lane(b >> 16))) << 16);
}
bool newer(u32 a, u32 b) { return a != b && a - b < 0x80000000u; }
u32 serialized_enter(void*) { return 0; }
void serialized_leave(void*, u32) {}
bool extension(const u8* p, unsigned n) { return p && n > 0 && p[0] == 4; }
bool decode(const u8* p, unsigned n, Header& h) {
    if (!p || n < header_bytes || n > max_packet_bytes || p[0] != 4 ||
        p[1] != 'F' || p[2] != 'X' || p[3] != 'T' || p[4] != 'M' || p[5] != wire_version ||
        p[11] != 0) return false;
    Header candidate = {Kind(p[6]), Mode(p[7]), {get16(p + 8), p[10], get32(p + 12)},
                        get32(p + 16), get32(p + 20), get16(p + 24), get16(p + 26), p[28]};
    if (!valid_mode(candidate.mode) || !candidate.key.engine || candidate.key.track >= 8 ||
        !candidate.key.generation || !valid_words(candidate.total, candidate.mode) ||
        (candidate.mode != Mode::Packed16AndSnapshot && p[29]) || !valid_prefix(candidate.mode, p[29], candidate.total)) return false;
    if (candidate.kind == Kind::Snapshot) {
        if (!candidate.sequence || candidate.offset >= candidate.total ||
            candidate.offset % fragment_words ||
            candidate.count != chunk_size(candidate.total, candidate.offset) ||
            n != header_bytes + 4u * candidate.count) return false;
    } else if (candidate.kind == Kind::Request || candidate.kind == Kind::Ack) {
        const bool targeted = candidate.kind == Kind::Request && candidate.mode == Mode::Packed16AndSnapshot && candidate.command;
        if (n != header_bytes + (targeted ? 4u : 0u) || (targeted && !get32(p + header_bytes)) || candidate.count || candidate.offset ||
            (candidate.kind == Kind::Ack && candidate.sequence == 0)) return false;
    } else return false;
    h = candidate;
    return true;
}
bool encode(const Header& h, const u32* words, Packet& out, unsigned prefix, u32 target) {
    if (h.count > fragment_words || (h.count && !words) || !valid_prefix(h.mode,prefix,h.total)) return false;
    Packet p = {};
    const bool targeted = h.kind == Kind::Request && h.mode == Mode::Packed16AndSnapshot && h.command;
    p.size = header_bytes + 4u * h.count + (targeted ? 4u : 0u);
    p.bytes[0] = 4; p.bytes[1] = 'F'; p.bytes[2] = 'X'; p.bytes[3] = 'T'; p.bytes[4] = 'M';
    p.bytes[5] = wire_version; p.bytes[6] = u8(h.kind); p.bytes[7] = u8(h.mode);
    put16(p.bytes + 8, h.key.engine); p.bytes[10] = h.key.track;
    put32(p.bytes + 12, h.key.generation); put32(p.bytes + 16, h.sequence);
    put32(p.bytes + 20, h.command); put16(p.bytes + 24, h.total); put16(p.bytes + 26, h.offset);
    p.bytes[28] = h.count;
    p.bytes[29] = h.mode == Mode::Packed16AndSnapshot ? u8(prefix) : 0;
    if (targeted) put32(p.bytes + header_bytes, target);
    for (unsigned i = 0; i < h.count; ++i) put32(p.bytes + header_bytes + 4 * i, words[i]);
    Header check;
    if (!decode(p.bytes, p.size, check)) return false;
    out = p;
    return true;
}
void Bank::begin_update() { if (!(update_version & 1u)) ++update_version; }
void Bank::end_update() { if (update_version & 1u) ++update_version; }
void Bank::publish(unsigned index, u32 value) { if (index < count) words[index] = value; }
void Bank::add(unsigned index, u32 value) {
    if (index < packed_prefix && index < count) words[index] = packed_add(words[index], value);
}
u32 Bank::pending_command() const {
    u32 request = requested_command;
    return reset_supported && request != applied_command ? request : 0;
}
void Bank::command_applied(u32 command) {
    if (reset_supported && command && command == requested_command) applied_command = command;
}
AudioEndpoint::AudioEndpoint(Critical c, Resolve resolve, void* registry, Timing timing)
    : critical_(c), resolve_(resolve), registry_(registry), timing_(timing), bank_(0), key_{0, 0, 0},
      mode_(Mode::Persistent), count_(0), packed_prefix_(0), next_sequence_(0), flight_sequence_(0), flight_command_(0),
      flight_{}, last_request_(0), next_send_(0), fragment_(0), selected_(false), flying_(false), ever_selected_(false) {}
bool AudioEndpoint::release_flight() {
    if (flying_ && bank_ && accumulates(mode_)) {
        Guard guard(critical_);
        if (bank_->update_version & 1u) return false;
        for (unsigned i = 0; i < packed_prefix_; ++i)
            bank_->words[i] = packed_add(flight_[i], bank_->words[i]);
    }
    flying_ = false;
    return true;
}
void AudioEndpoint::deactivate() {
    // A lost ACK is indistinguishable from a lost snapshot until GUI reports its
    // persistent receipt. Never merge the flight merely because its lease ended.
    selected_ = false;
    if (!flying_) bank_ = 0;
}
bool AudioEndpoint::receive(const u8* bytes, unsigned length, u32 now) {
    Header h;
    if (!decode(bytes, length, h)) return false;
    if (h.kind == Kind::Ack) {
        if (!flying_ || !same(h.key, key_) || h.mode != mode_ || h.total != count_ ||
            h.sequence != flight_sequence_ || h.command != flight_command_ ||
            (h.mode == Mode::Packed16AndSnapshot && bytes[29] != packed_prefix_)) return false;
        flying_ = false;
        next_send_ = now + timing_.frame;
        return true;
    }
    if (h.kind != Kind::Request || !resolve_ || !critical_.enter || !critical_.leave) return false;
    if (ever_selected_ && h.key.generation != key_.generation && !newer(h.key.generation, key_.generation)) return false;
    if (ever_selected_ && h.key.generation == key_.generation && !same(h.key, key_)) return false;
    Bank* bank = resolve_(registry_, h.key.engine, h.key.track);
    if (!bank || !bank->words || bank->count != h.total || bank->mode != h.mode ||
        (h.command && !bank->reset_supported) ||
        (h.mode == Mode::Packed16AndSnapshot && bytes[29] != bank->packed_prefix)) return false;
    if (h.sequence && (!next_sequence_ || newer(h.sequence, next_sequence_))) return false;
    // Request.sequence is the last globally committed audio sequence, retained
    // by GUI across selection changes. It acknowledges the old flight without
    // requiring the old key/command fields to fit in the new selection request.
    if (flying_ && h.sequence == flight_sequence_) flying_ = false;
    if (!ever_selected_ || !same(h.key, key_) || bank_ != bank) {
        if (!release_flight()) return false;
        bank_ = bank; key_ = h.key; mode_ = h.mode; count_ = h.total; packed_prefix_ = bank->packed_prefix;
        selected_ = true; ever_selected_ = true; next_send_ = now;
    } else if (!selected_) {
        // Same generation after a quiet interval: retransmit the parked flight
        // if its receipt is still missing, preserving GUI deduplication evidence.
        selected_ = true; next_send_ = now;
    }
    // An older reordered request must not roll back a command. Zero means no
    // command requested, never reset the producer's command state.
    if (h.command) {
        Guard guard(critical_);
        const u32 target = h.mode == Mode::Packed16AndSnapshot ? get32(bytes + header_bytes) : 1;
        if (h.command == bank_->requested_command && target != bank_->requested_target) return false;
        if (bank_->pending_command() && newer(h.command,bank_->requested_command)) return false;
        if (!bank_->requested_command || newer(h.command, bank_->requested_command)) {
            bank_->requested_target = target;
            bank_->requested_command = h.command;
        }
    }
    last_request_ = now;
    return true;
}
bool AudioEndpoint::service(u32 now, Send send, void* context) {
    if (!selected_ || !send) return false;
    if (now - last_request_ >= timing_.lease) { deactivate(); return false; }
    if (!due(now, next_send_)) return false;
    if (!flying_) {
        { Guard guard(critical_);
          if (bank_->update_version & 1u) { next_send_ = now + timing_.fragment; return false; }
          for (unsigned i = 0; i < count_; ++i) {
              flight_[i] = bank_->words[i];
              if (i < packed_prefix_) bank_->words[i] = 0;
          }
          flight_command_ = bank_->applied_command;
        }
        next_sequence_ = advance(next_sequence_);
        flight_sequence_ = next_sequence_; fragment_ = 0; flying_ = true;
    }
    unsigned offset = unsigned(fragment_) * fragment_words;
    Header h = {Kind::Snapshot, mode_, key_, flight_sequence_, flight_command_, count_, u16(offset),
                u8(chunk_size(count_, offset))};
    Packet packet;
    if (!encode(h, flight_ + offset, packet, packed_prefix_)) return false;
    if (!send(context, packet)) { next_send_ = now + timing_.fragment; return false; }
    ++fragment_;
    if (fragment_ >= chunks(count_)) { fragment_ = 0; next_send_ = now + timing_.retry; }
    else next_send_ = now + timing_.fragment;
    return true;
}
GuiEndpoint::GuiEndpoint(Timing timing)
    : timing_(timing), key_{0, 0, 0}, mode_(Mode::Persistent), count_(0), packed_prefix_(0), selected_(false),
      reset_supported_(false), have_snapshot_(false), ack_pending_(false), request_dirty_(false),
      requested_command_(0), requested_target_(1), command_counter_(0), applied_command_(0), delivered_(0), last_receipt_(0), assembling_(0), assembling_command_(0),
      fragment_mask_(0), next_request_(0), staging_{}, snapshot_{} {}
bool GuiEndpoint::select(Key key, u16 words, Mode mode, bool reset, unsigned prefix) {
    if (!key.engine || key.track >= 8 || !key.generation || !valid_mode(mode) || !valid_words(words, mode) || !valid_prefix(mode, prefix, words) ||
        (key_.generation && !newer(key.generation, key_.generation))) return false;
    key_ = key; count_ = words; mode_ = mode; packed_prefix_ = u8(prefix_for(mode, prefix)); selected_ = true; reset_supported_ = reset;
    have_snapshot_ = false; ack_pending_ = false; request_dirty_ = true;
    // Command IDs and the last delivery receipt are global for this endpoint's
    // lifetime. Neither resets on select/deselect or a temporary transport loss.
    requested_command_ = 0; requested_target_ = 1; applied_command_ = 0; delivered_ = 0; assembling_ = 0; fragment_mask_ = 0;
    next_request_ = 0;
    for (unsigned i = 0; i < max_words; ++i) { staging_[i] = 0; snapshot_[i] = 0; }
    return true;
}
void GuiEndpoint::deselect() { selected_ = false; have_snapshot_ = false; ack_pending_ = false; }
u32 GuiEndpoint::request_reset(u32 target) {
    if (!selected_ || !reset_supported_ || !target) return 0;
    if (requested_command_ && requested_command_ != applied_command_) return requested_command_;
    requested_target_ = mode_ == Mode::Packed16AndSnapshot ? target : 1;
    command_counter_ = advance(command_counter_); requested_command_ = command_counter_; request_dirty_ = true;
    return requested_command_;
}
bool GuiEndpoint::receive(const u8* bytes, unsigned length) {
    Header h;
    if (!selected_ || !decode(bytes, length, h) || h.kind != Kind::Snapshot || !same(h.key, key_) ||
        h.mode != mode_ || h.total != count_ || (!reset_supported_ && h.command) ||
        (h.mode == Mode::Packed16AndSnapshot && bytes[29] != packed_prefix_)) return false;
    if (h.sequence == delivered_) {
        if (h.command != applied_command_) return false;
        if (!accumulates(mode_) || last_receipt_ == delivered_) ack_pending_ = true;
        return true;
    }
    // An accumulator flight is not acknowledged until its consumer takes it.
    // Keep one pending flight; the sender cannot legitimately advance before ACK.
    if (accumulates(mode_) && delivered_ && delivered_ != last_receipt_) return false;
    if (last_receipt_ && !newer(h.sequence, last_receipt_)) return false;
    if (h.sequence != assembling_) {
        if (assembling_ && !newer(h.sequence, assembling_)) return false;
        assembling_ = h.sequence; assembling_command_ = h.command; fragment_mask_ = 0;
    } else if (h.command != assembling_command_) return false;
    unsigned bit = 1u << (h.offset / fragment_words);
    if (fragment_mask_ & bit) {
        for (unsigned i = 0; i < h.count; ++i)
            if (staging_[h.offset + i] != get32(bytes + header_bytes + 4 * i)) return false;
        return true;
    }
    for (unsigned i = 0; i < h.count; ++i) staging_[h.offset + i] = get32(bytes + header_bytes + 4 * i);
    fragment_mask_ |= bit;
    if (fragment_mask_ != (1u << chunks(count_)) - 1u) return true;
    for (unsigned i = 0; i < count_; ++i)
        snapshot_[i] = i < packed_prefix_ ? packed_add(snapshot_[i], staging_[i]) : staging_[i];
    have_snapshot_ = true; delivered_ = assembling_;
    applied_command_ = assembling_command_;
    if (!accumulates(mode_)) { last_receipt_ = delivered_; ack_pending_ = true; }
    return true;
}
bool GuiEndpoint::service(u32 now, Send send, void* context) {
    if (!selected_ || !send) return false;
    Header h = {Kind::Request, mode_, key_, last_receipt_, reset_supported_ ? requested_command_ : 0, count_, 0, 0};
    bool ack = ack_pending_;
    if (ack) { h.kind = Kind::Ack; h.sequence = delivered_; h.command = applied_command_; }
    else if (!request_dirty_ && !due(now, next_request_)) return false;
    Packet packet;
    if (!encode(h, 0, packet, packed_prefix_, requested_target_)) return false;
    if (!send(context, packet)) {
        if (!ack) { next_request_ = now + timing_.fragment; request_dirty_ = false; }
        return false;
    }
    if (ack) ack_pending_ = false;
    else { next_request_ = now + timing_.heartbeat; request_dirty_ = false; }
    return true;
}
unsigned GuiEndpoint::take(u32* output, unsigned capacity) {
    if (!selected_ || !have_snapshot_ || !output || capacity < count_) return 0;
    for (unsigned i = 0; i < count_; ++i) output[i] = snapshot_[i];
    if (accumulates(mode_)) {
        for (unsigned i = 0; i < packed_prefix_; ++i) snapshot_[i] = 0;
        if (last_receipt_ != delivered_) { last_receipt_ = delivered_; ack_pending_ = true; }
    }
    if (mode_ == Mode::AccumulatePacked16) have_snapshot_ = false;
    return count_;
}
}}
