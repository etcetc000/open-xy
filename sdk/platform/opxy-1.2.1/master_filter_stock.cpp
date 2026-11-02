#include "master_processor.hpp"
#include "stock.hpp"
#include "stock_browser.hpp"
#include "synth_controls.hpp"
#include <opxy/q31.hpp>

#include "project_companion.hpp"

using namespace opxy;
using namespace opxy::platform;
using namespace opxy::platform::master_processor;

extern "C" {
u32 sdk_master_buttons(void *, const u32 *) __asm__("sdk_master_buttons");
u32 sdk_master_shift_buttons(void *, const u32 *) __asm__("sdk_master_shift_buttons");
void sdk_master_turn(void *, u32, const i32 *) __asm__("sdk_master_turn");
void sdk_master_frame(void *, i32) __asm__("sdk_master_frame");
void sdk_master_receive(u32, u32, i32) __asm__("sdk_master_receive");
void sdk_master_process(void *, i32 *, i32 *) __asm__("sdk_master_process");
u32 sdk_master_project() __asm__("sdk_master_project");
u32 sdk_master_stock_buttons(void *, const u32 *) __asm__("sdk_master_stock_buttons");
u32 sdk_master_stock_shift_buttons(void *, const u32 *) __asm__("sdk_master_stock_shift_buttons");
void sdk_master_stock_turn(void *, u32, const i32 *) __asm__("sdk_master_stock_turn");
void sdk_master_stock_frame(void *, i32) __asm__("sdk_master_stock_frame");
}

namespace {
// GUI and audio execute on separate roles: never use the GUI object as audio
// state. A bounded B3 transaction publishes complete normalized parameter sets.
State audio_state = {}, staged = {};
u32 staged_sequence, staged_mask, staged_count;
bool staging;
i32 selection;
u32 audio_epoch, active_epoch;
State active_state = {};
bool fading_out;
u32 selection_remaining;
i32 selection_step, selection_rest, selection_error, selection_sign;
const master::Processor *audio_processor;
bool audio_ready;
i32 eq_left[block_frames], eq_right[block_frames];

u32 &page(void *controller) { return static_cast<u32 *>(controller)[0x88 / 4]; }
void publish(bool reset_audio = false) {
    static u32 sequence = 0;
    sequence = (sequence + 1) & 0x1fffffu;
    u32 count = 0;
    parameters(count, menu.type);
    stock<void(u32, i32)>(0x8d4dc84c)(message_id,
                                      static_cast<i32>((sequence << 10) | (count << 5) | (reset_audio ? 512u : 0u) | menu.type));
    for (u32 i = 0; i < count; ++i)
        stock<void(u32, i32)>(0x8d4dc84c)(message_id + 1 + i, menu.values[i]);
    stock<void(u32, i32)>(0x8d4dc84c)(commit_id, static_cast<i32>(sequence));
}
void publish_edit(u32 before) {
    if (before != menu.signature())
        stock<void()>(0x8d585ee8)();
    publish();
}

} // namespace

namespace {
stock_browser::StockSelector<max_processors + 1> selector = {};
void selected(void *, u32 index) {
    if (menu.open) {
        const u32 before = menu.signature(), old_type = menu.type;
        menu.candidate = index;
        menu.confirm();
        master_processor::reset_project();
        if(before != menu.signature())stock<void()>(0x8d585ee8)();
        publish(old_type != menu.type);
    }
}
void cancelled(void *) { menu.cancel(); }
void show_browser() {
    if (menu.open)
        return;
    const u32 count = processor_count();
    if (!count)
        return;
    menu.show();
    const char* labels[max_processors + 1] = {"original eq"};
    for(u32 i=1;i<=count;++i)labels[i]=registration(i).processor->label;
    if (!selector.open("filter", labels, count+1, menu.type, {nullptr, selected, cancelled}))
        menu.cancel();
}
u32 buttons(void *controller, const u32 *event, bool shifted) {
    if (!controller || !event)
        return shifted ? sdk_master_stock_shift_buttons(controller, event)
                       : sdk_master_stock_buttons(controller, event);
    const u32 key = event[0];
    // The key router calls vtable+0x10 while Shift is held, and +0x0c
    // otherwise. A Shift+M2 press never reaches the ordinary button method.
    if (key == 7 && page(controller) == 1 && shifted) {
        show_browser();
        return 1;
    }
    if (page(controller) == 1 && menu.type && key >= 10 && key <= 13) {
        const u32 before = menu.signature();
        const u32 encoder = key - 10, slot = menu.shown[encoder];
        menu.click(encoder);
        u32 count = 0;
        parameters(count);
        if (slot < count && menu.shown[encoder] == slot)
            master_processor::parameter_reset(slot);
        publish_edit(before);
        return 1;
    }
    return shifted ? sdk_master_stock_shift_buttons(controller, event)
                   : sdk_master_stock_buttons(controller, event);
}
} // namespace
u32 sdk_master_buttons(void *controller, const u32 *event) { return buttons(controller, event, false); }
u32 sdk_master_shift_buttons(void *controller, const u32 *event) { return buttons(controller, event, true); }
void sdk_master_turn(void *controller, u32 position, const i32 *event) {
    if (controller && event && page(controller) == 1 && position < 8) {
        const u32 encoder = position & 3u;
        if (menu.type) {
            u32 count = 0;
            const Parameter *p = parameters(count);
            const u32 slot = menu.shown[encoder];
            if (slot < count) {
                const u32 before = menu.signature();
                i32 &value = menu.values[slot];
                if (p[slot].steps) {
                    const u32 n = p[slot].steps;
                    i32 choice = static_cast<i32>(control_choice(value, n));
                    choice += event[1];
                    if (choice < 0)
                        choice = 0;
                    if (choice >= static_cast<i32>(n))
                        choice = n - 1;
                    value = static_cast<i32>(((0x80000000u / n) * (2 * static_cast<u32>(choice) + 1)) / 2);
                } else {
                    value = stock<i32(i32, const i32 *, u32, i32)>(0x8d4d6cc8)(value, event, 3, -1);
                    if (value < 0)
                        value = 0;
                }
                publish_edit(before);
            }
            return;
        }
    }
    sdk_master_stock_turn(controller, position, event);
}
void sdk_master_frame(void *object, i32 elapsed) {
    if (!menu.type || menu.open) {
        sdk_master_stock_frame(object, elapsed);
        return;
    }
    master_processor::frame(elapsed);
}

void sdk_master_receive(u32 base, u32 id, i32 value) {
    // The receiver only publishes bounded state. DSP lifecycle runs at block boundaries.
    static bool staged_reset;
    if(id>=message_id && id<=commit_id) {
        if(id==message_id) {
            staging=value>=0;staged_mask=0;staged={};
            const u32 word=static_cast<u32>(value);
            staged.type=word&31u;staged_count=(word>>5)&15u;
            staged_reset=(word&512u)!=0;staged_sequence=word>>10;
            u32 count=0;parameters(count,staged.type);
            if(staged.type>processor_count() || staged_count!=count || staged_count>parameter_slots)staging=false;
        } else if(staging && id==commit_id) {
            if(value>=0 && static_cast<u32>(value)==staged_sequence && staged_mask==((1u<<staged_count)-1)) {
                if(staged_reset || staged.type!=audio_state.type)++audio_epoch;
                audio_state=staged;
            }
            staging=false;
        } else if(staging && id<commit_id) {
            const u32 slot=id-message_id-1;
            if(value<0 || slot>=staged_count){staging=false;return;}
            staged.values[slot]=value;staged_mask|=1u<<slot;
        }
        return;
    }
    stock<void(u32,u32,i32)>(0x8d50eb8c)(base,id,value);
}
namespace {
void ramp(i32 target) {
    selection_remaining=ramp_frames;selection_error=0;
    const i32 delta=target-selection;
    selection_step=delta/static_cast<i32>(ramp_frames);
    const i32 rest=delta%static_cast<i32>(ramp_frames);
    selection_sign=rest<0 ? -1 : 1;selection_rest=rest<0 ? -rest : rest;
}
}
void sdk_master_process(void* object,i32* left,i32* right) {
    if(!audio_ready){audio_ready=true;active_epoch=audio_epoch;}
    // On switch/project reset, fade the old processor to EQ before discarding it.
    // Never run two processor extensions, including during a transition.
    if(audio_processor && (active_state.type!=audio_state.type || active_epoch!=audio_epoch) && !fading_out) {
        fading_out=true;ramp(0);
    }
    if(!audio_processor && audio_state.type) {
        const auto entry=registration(audio_state.type);
        audio_processor=entry.processor;
        if(audio_processor) {
            audio_processor->reset();active_state=audio_state;active_epoch=audio_epoch;
            fading_out=false;ramp(32768);
        }
    }
    if(!audio_processor) {
        stock<void(void*,i32*,i32*)>(0x11a02c10)(object,left,right);return;
    }
    if(!fading_out)active_state=audio_state;
    const bool run_stock=fading_out || selection_remaining || selection<32768;
    if(run_stock) {
        for(u32 i=0;i<block_frames;++i){eq_left[i]=left[i];eq_right[i]=right[i];}
        stock<void(void*,i32*,i32*)>(0x11a02c10)(object,eq_left,eq_right);
    }
    audio_processor->prepare(active_state.values);
    audio_processor->process(left,right);
    if(run_stock)for(u32 i=0;i<block_frames;++i) {
        if(selection_remaining) {
            selection+=selection_step;selection_error+=selection_rest;
            if(selection_error>=static_cast<i32>(ramp_frames)){selection+=selection_sign;selection_error-=ramp_frames;}
            if(!--selection_remaining)selection=fading_out ? 0 : 32768;
        }
        left[i]=static_cast<i32>((opxy::q31::wide(eq_left[i],32768-selection)+opxy::q31::wide(left[i],selection))/32768);
        right[i]=static_cast<i32>((opxy::q31::wide(eq_right[i],32768-selection)+opxy::q31::wide(right[i],selection))/32768);
    }
    if(fading_out && !selection_remaining) {
        audio_processor->reset();audio_processor=nullptr;active_state={};fading_out=false;
    }
}
u32 sdk_master_project() {
    // Called only by the full active-project parameter publication, after
    // stock has switched projects. Save and ordinary EQ edits don't reach it.
    const u32 project = stock<u32()>(0x8d584210)();
    const State restored = restore_state(project);
    menu.reset();
    menu.type = menu.candidate = restored.type;
    for (u32 i = 0; i < parameter_slots; ++i)
        menu.values[i] = restored.values[i];
    for(u32 i=0;i<parameter_slots;++i)menu.bank[restored.type ? restored.type : 1][i]=restored.values[i];
    master_processor::reset_project();
    publish(true);
    return project;
}

extern "C" void sdk_project_new() __asm__("sdk_project_new");
extern "C" void sdk_project_stock_new() __asm__("sdk_project_stock_new");
void sdk_project_new() {
    clear_pending();
    sdk_project_stock_new();
}
