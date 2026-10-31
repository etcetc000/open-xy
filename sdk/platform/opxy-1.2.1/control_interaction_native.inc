// Included once by synth_controls.cpp. GUI callbacks and GUI drawing share
// this role-local bank; no audio callback, IPC or parameter-delta inference.
namespace {
opxy::control_interaction::detail::Bank interaction_bank;
// Stock loads SVGs and calls display reset before project patterns exist.
// Reset may request an interaction baseline, so the new engine lookup must
// satisfy the stock pattern getter's project and selected-pattern preconditions first.
bool interaction_pattern_ready(opxy::u32 track) {
    using namespace opxy::platform;
    if(track>=8)return false;
    const opxy::u32 project=word(0x840a0524u);
    if(!project)return false;
    const opxy::u32 count=word(project+0x54cbcu+track*0x53f44u);
    if(!count)return false;
    const opxy::u32 mask=byte(project+0xcu)|(byte(project+0xdu)<<8);
    const opxy::u32 offset=(mask&(1u<<track))?0xeu+track:0x98u+33u*byte(project+6)+track;
    return byte(project+offset)<count;
}
void capture_interaction(opxy::u32 project,opxy::u32 track,opxy::u32 engine,
                         opxy::u32 physical,opxy::u32 parameter) {
    using namespace opxy::platform;
    if(track>=8 || physical>=4 || parameter>=8 || !byte(0x86c05ca4u))return;
    const opxy::i32 value=stock<opxy::i32(opxy::u32,opxy::u32,opxy::u32)>(0x8d5ac9dcu)(project,track,parameter);
    interaction_bank.available=1;
    interaction_bank.publish(engine,track,physical,parameter,value);
}
}
namespace opxy { namespace control_interaction { namespace detail {
Event native_latest(u32 track) {
    using namespace platform;
    if(track>=8 || !byte(0x86c05ca4u) || !interaction_pattern_ready(track))return Event{};
    const u32 engine=track_engine(track);
    if(!owned(engine))return Event{};
    interaction_bank.available=1;
    return interaction_bank.read(engine,track);
}
}}}
