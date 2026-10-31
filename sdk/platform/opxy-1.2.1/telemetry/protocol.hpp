#pragma once

// SDK audio/display wire protocol. No allocation, firmware pointer or wait loop.
// Audio callback owns producer writes; background access uses Critical callbacks.
namespace opxy_transport { namespace telemetry {
typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
static_assert(sizeof(u32) == 4 && sizeof(u16) == 2, "Telemetry wire widths");
const unsigned max_words = 256;
const unsigned header_bytes = 30;
const unsigned fragment_words = 54;
const unsigned max_fragments = 5;
const unsigned max_packet_bytes = 249;
const u8 wire_version = 2;

enum class Mode : u8 { Persistent = 0, AccumulatePacked16 = 1, Packed16AndSnapshot = 2 };
enum class Kind : u8 { Request = 1, Snapshot = 2, Ack = 3 };
struct Key {
    u16 engine;
    u8 track;
    u32 generation;
};
struct Packet { u8 bytes[max_packet_bytes]; unsigned size; };
struct Header {
    Kind kind;
    Mode mode;
    Key key;
    u32 sequence;
    u32 command;
    u16 total;
    u16 offset;
    u8 count;
};
bool decode(const u8* bytes, unsigned length, Header& output);
bool encode(const Header& header, const u32* words, Packet& output, unsigned packed_prefix = 1, u32 target = 1);
bool extension(const u8* bytes, unsigned length);
u32 packed_add(u32 a, u32 b);
bool newer(u32 candidate, u32 reference);

struct Critical {
    void* context;
    u32 (*enter)(void*);
    void (*leave)(void*, u32);
};
struct Guard {
    const Critical& critical;
    u32 token;
    explicit Guard(const Critical& c) : critical(c), token(c.enter(c.context)) {}
    ~Guard() { critical.leave(critical.context, token); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
// A test-only serialized environment may use these. Native requires real guards.
u32 serialized_enter(void*);
void serialized_leave(void*, u32);

struct Bank {
    volatile u32* words;
    u16 count;
    Mode mode;
    bool reset_supported;
    u8 packed_prefix;
    volatile u32 update_version;
    volatile u32 requested_command;
    volatile u32 requested_target;
    volatile u32 applied_command;
    Bank(volatile u32* storage, u16 n, Mode m, bool reset, unsigned prefix = 1)
        : words(storage), count(n), mode(m), reset_supported(reset), packed_prefix(u8(m == Mode::Persistent ? 0 : m == Mode::AccumulatePacked16 ? 1 : prefix)), update_version(0),
          requested_command(0), requested_target(1), applied_command(0) {}
    // Bracket the complete author-render publication transaction. These are
    // single-producer calls, not locks. Background never snapshots odd versions.
    void begin_update();
    void end_update();
    void publish(unsigned index, u32 value);
    void add(unsigned index, u32 value);
    // Called only on audio callback owner. Apply the engine mutation first,
    // then acknowledge the returned command; replayed requests are idempotent.
    u32 pending_command() const;
    u32 pending_target() const { return pending_command() ? requested_target : 0; }
    void command_applied(u32 command);
};
template <unsigned Words> struct Producer {
    static_assert(Words > 0 && Words <= max_words, "Telemetry bank capacity");
    volatile u32 storage[Words];
    Bank bank;
    explicit Producer(Mode mode = Mode::Persistent, bool reset = false, unsigned prefix = 1)
        : storage{}, bank(storage, Words, mode, reset, prefix) {}
};

typedef bool (*Send)(void*, const Packet&);
typedef Bank* (*Resolve)(void*, u16 engine, u8 track);
struct Timing {
    u32 frame;
    u32 fragment;
    u32 retry;
    u32 heartbeat;
    u32 lease;
    // Caller supplies monotonic milliseconds. All intervals < 2^31.
    Timing() : frame(16), fragment(1), retry(64), heartbeat(100), lease(1000) {}
};

class AudioEndpoint {
    Critical critical_;
    Resolve resolve_;
    void* registry_;
    Timing timing_;
    Bank* bank_;
    Key key_;
    Mode mode_;
    u16 count_;
    u8 packed_prefix_;
    u32 next_sequence_;
    u32 flight_sequence_;
    u32 flight_command_;
    u32 flight_[max_words];
    u32 last_request_;
    u32 next_send_;
    u8 fragment_;
    bool selected_;
    bool flying_;
    bool ever_selected_;
    bool release_flight();
public:
    AudioEndpoint(Critical critical, Resolve resolve, void* registry, Timing timing = Timing());
    bool receive(const u8* bytes, unsigned length, u32 now);
    // At most one packet, one snapshot and one producer critical section per call.
    bool service(u32 now, Send send, void* context);
    // Stop publication but park the flight. Only a later receipt/new selection
    // can determine whether its Phase contribution was already consumed.
    void deactivate();
    bool in_flight() const { return flying_; }
    u32 sequence() const { return flight_sequence_; }
};

class GuiEndpoint {
    Timing timing_;
    Key key_;
    Mode mode_;
    u16 count_;
    u8 packed_prefix_;
    bool selected_;
    bool reset_supported_;
    bool have_snapshot_;
    bool ack_pending_;
    bool request_dirty_;
    u32 requested_command_;
    u32 requested_target_;
    u32 command_counter_;
    u32 applied_command_;
    u32 delivered_;
    u32 last_receipt_;
    u32 assembling_;
    u32 assembling_command_;
    u32 fragment_mask_;
    u32 next_request_;
    u32 staging_[max_words];
    u32 snapshot_[max_words];
public:
    explicit GuiEndpoint(Timing timing = Timing());
    // Generations are globally increasing across all selections, not per track.
    bool select(Key key, u16 words, Mode mode, bool reset_supported = false, unsigned packed_prefix = 1);
    void deselect();
    u32 request_reset(u32 target = 1);
    bool receive(const u8* bytes, unsigned length);
    bool service(u32 now, Send send, void* context);
    // GUI owner only. Persistent frames remain available; accumulation consumes.
    unsigned take(u32* output, unsigned capacity);
    u32 applied_command() const { return applied_command_; }
    u32 sequence() const { return delivered_; }
};
}}
