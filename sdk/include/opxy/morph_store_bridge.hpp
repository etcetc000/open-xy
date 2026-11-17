// Morph v0.0.6 private stored-CURRENT / BASE-guard building blocks.
// C++11, zero-initialized POD only: the stock Blackfin loader does not run C++ constructors.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace morphport {
inline bool supported(u32 p) {
    return (p>=64u && p<72u) || p<=11u || p==13u || p==15u || (p>=16u && p<=23u) || (p>=32u && p<=35u);
}
struct ShiftSchema {
 u32 engine;
 u8 count, choices[8], defaults[8];
};
struct Session {
 u32 aux_block;
 ShiftSchema shift[8];
    u32 project;
    u32 scene;
    u32 blocks[8];
    bool active;
    void clear() { active=false; }
    void begin(u32 p,u32 sc,const u32* b) {
        active=false;
        project=p;scene=sc;
        for(u32 t=0;t<8u;++t)blocks[t]=b[t];
        active=true;
    }
    bool matches(u32 p,u32 sc,const u32* b) const {
        if(!active || !p || project!=p || scene!=sc)return false;
        for(u32 t=0;t<8u;++t)
            if(!b[t] || b[t]!=blocks[t])return false;
        return true;
    }
};
struct Pending {
    u32 block;
    i32 current;
    bool accepted;
};
}}

// PRIVATE SDK bridge, available only inside an authorized Morph control callback.
// Stored write acceptance means added to the v0.0.6 Overlay::stored queue.
// It does not claim that the value was applied until subsequent readback.
extern "C" bool sdk_morph_stored_current_read(opxy::u32 track,opxy::u32 index,opxy::i32* out);
extern "C" bool sdk_morph_stored_once_submit(opxy::u32 track,opxy::u32 index,opxy::i32 value);
extern "C" opxy::u32 sdk_morph_stored_once_take_feedback();
extern "C" bool sdk_morph_session_context_begin();
extern "C" bool sdk_morph_session_context_ok();
extern "C" void sdk_morph_session_context_end();
extern "C" bool sdk_morph_setbase_context_begin();
extern "C" bool sdk_morph_setbase_context_ok();
extern "C" bool sdk_morph_setbase_current_read(opxy::u32 track,opxy::u32 index,opxy::i32* out);
extern "C" void sdk_morph_setbase_context_end();
extern "C" opxy::u32 sdk_morph_setbase_take_feedback();

// GUI-thread inspection outside control(). Never call from the audio thread.
// Read-only: does not begin sessions or permit stored writes.
extern "C" bool sdk_morph_view_context_ok();
extern "C" bool sdk_morph_view_current_read(opxy::u32 track,opxy::u32 index,opxy::i32* out);

// All-or-nothing GUI read of at most 26 supported slots; out unchanged on refusal.
extern "C" bool sdk_morph_view_current_batch(opxy::u32 track,const opxy::u32* indices,
                                            opxy::u32 count,opxy::i32* out);

// Explicit physical movement only: records the AUX lane, never destination writes.
extern "C" bool sdk_morph_movement_record(opxy::i32 value);

extern "C" bool sdk_morph_persist_identity(opxy::morphport::ShiftSchema* schemas,opxy::u32* patterns);


extern "C" bool sdk_morph_movement_has_locks();

extern "C" bool sdk_morph_movement_lfo_active();

extern "C" bool sdk_morph_stored_batch_read(opxy::u32 track,const opxy::u32* indices,opxy::u32 count,opxy::i32* out);

extern "C" bool sdk_morph_view_visual_batch(opxy::u32 track,const opxy::u32* indices,opxy::i32* out,opxy::u32* filter,opxy::i32* play,opxy::i32* bend) __asm__("sdk_morph_view_visual_batch");
