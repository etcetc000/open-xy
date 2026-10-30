#include "master_filter.hpp"
#include <opxy/q31.hpp>
#include "stock.hpp"
#include "stock_browser.hpp"

#include "project_companion.hpp"

using namespace opxy;
using namespace opxy::platform;
using namespace opxy::platform::master_filter;

extern "C" {
u32 sdk_master_buttons(void *, const u32 *) __asm__("sdk_master_buttons");
u32 sdk_master_shift_buttons(void *, const u32 *) __asm__("sdk_master_shift_buttons");
void sdk_master_turn(void *, u32, const i32 *) __asm__("sdk_master_turn");
void sdk_master_frame(void *, i32) __asm__("sdk_master_frame");
void sdk_master_receive(u32, u32, i32) __asm__("sdk_master_receive");
void sdk_master_process(void *, i32 *, i32 *) __asm__("sdk_master_process");
u32 sdk_master_project() __asm__("sdk_master_project");
u32 sdk_master_snapshot() __asm__("sdk_master_snapshot");
u32 sdk_master_stock_buttons(void *, const u32 *) __asm__("sdk_master_stock_buttons");
u32 sdk_master_stock_shift_buttons(void *, const u32 *) __asm__("sdk_master_stock_shift_buttons");
void sdk_master_stock_turn(void *, u32, const i32 *) __asm__("sdk_master_stock_turn");
void sdk_master_stock_frame(void *, i32) __asm__("sdk_master_stock_frame");
}

namespace {
// GUI and audio execute on separate roles: never use the GUI object as audio
// state. One existing B3 packet carries an entire non-negative 31-bit snapshot.
u32 audio_packet = initial_packet;
i32 selection;
bool selected_target;
u32 selection_remaining;
i32 selection_step, selection_rest, selection_error, selection_sign;
const master::Filter *audio_filter;
bool audio_ready;
i32 eq_left[block_frames], eq_right[block_frames];

u32 &page(void *controller) {
    return static_cast<u32 *>(controller)[0x88 / 4];
}
void publish() {
    stock<void(u32, i32)>(0x8d4dc84c)(message_id, static_cast<i32>(menu.packet()));
}
void publish_edit(u32 before) {
    if (before != menu.packet())
        stock<void()>(0x8d585ee8)();
    publish();
}

} // namespace

namespace {
stock_browser::StockSelector<2> selector = {};
void selected(void *, u32 index) {
    if (menu.open) {
        const u32 before = menu.packet();
        menu.candidate = index;
        menu.confirm();
        publish_edit(before);
    }
}
void cancelled(void *) {
    menu.cancel();
}
void show_browser() {
    if (menu.open)
        return;
    const auto *filter = descriptor();
    if (!filter)
        return;
    menu.show();
    const char *const labels[] = {"original eq", filter->label};
    if (!selector.open("filter", labels, menu.type, {nullptr, selected, cancelled}))
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
        const u32 before = menu.packet();
        if (key == 10)
            master_filter::reset_visual();
        menu.click(key - 10);
        publish_edit(before);
        return 1;
    }
    return shifted ? sdk_master_stock_shift_buttons(controller, event)
                   : sdk_master_stock_buttons(controller, event);
}
} // namespace
u32 sdk_master_buttons(void *controller, const u32 *event) {
    return buttons(controller, event, false);
}
u32 sdk_master_shift_buttons(void *controller, const u32 *event) {
    return buttons(controller, event, true);
}
void sdk_master_turn(void *controller, u32 position, const i32 *event) {
    if (controller && event && page(controller) == 1 && position < 8) {
        const u32 encoder = position & 3u;
        if (menu.type) {
            if (encoder == 0) {
                const u32 before = menu.packet();
                menu.values[encoder] =
                    stock<i32(i32, const i32 *, u32, i32)>(0x8d4d6cc8)(menu.values[encoder], event, 3, -1);
                if (menu.values[encoder] < 0)
                    menu.values[encoder] = 0;
                master_filter::follow_visual(Controls::unpack(menu.packet()).sweep);
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
    master_filter::frame(Controls::unpack(menu.packet()).sweep, elapsed);
}

void sdk_master_receive(u32 base, u32 id, i32 value) {
    if (id == message_id) {
        // Consume malformed snapshots too; never let virtual IDs index stock.
        if (value >= 0)
            audio_packet = static_cast<u32>(value);
        return;
    }
    stock<void(u32, u32, i32)>(0x8d50eb8c)(base, id, value);
}
void sdk_master_process(void *object, i32 *left, i32 *right) {
    if (!audio_ready) {
        audio_filter = descriptor();
        if (audio_filter)
            audio_filter->reset();
        audio_ready = true;
    }
    const Controls controls = Controls::unpack(audio_packet);
    const bool selected = controls.type && audio_filter;
    if (selected != selected_target) {
        selected_target = selected;
        selection_remaining = ramp_frames;
        selection_error = 0;
        const i32 delta = (selected ? 32768 : 0) - selection;
        selection_step = delta / static_cast<i32>(ramp_frames);
        const i32 rest = delta % static_cast<i32>(ramp_frames);
        selection_sign = rest < 0 ? -1 : 1;
        selection_rest = rest < 0 ? -rest : rest;
    }
    if (!selected && !selection_remaining && !selection) {
        stock<void(void *, i32 *, i32 *)>(0x11a02c10)(object, left, right);
        return;
    }
    if (!audio_filter) {
        stock<void(void *, i32 *, i32 *)>(0x11a02c10)(object, left, right);
        return;
    }
    const bool run_stock = !selected || selection_remaining;
    if (run_stock) {
        for (u32 i = 0; i < block_frames; ++i) {
            eq_left[i] = left[i];
            eq_right[i] = right[i];
        }
        stock<void(void *, i32 *, i32 *)>(0x11a02c10)(object, eq_left, eq_right);
    }
    audio_filter->prepare(controls.sweep);
    audio_filter->process(left, right);
    if (run_stock)
        for (u32 i = 0; i < block_frames; ++i) {
            if (selection_remaining) {
                const i32 target = selected ? 32768 : 0;
                selection += selection_step;
                selection_error += selection_rest;
                if (selection_error >= static_cast<i32>(ramp_frames)) {
                    selection += selection_sign;
                    selection_error -= ramp_frames;
                }
                if (!--selection_remaining)
                    selection = target;
            }
            left[i] = static_cast<i32>(
                (opxy::q31::wide(eq_left[i], 32768 - selection) + opxy::q31::wide(left[i], selection)) /
                32768);
            right[i] = static_cast<i32>(
                (opxy::q31::wide(eq_right[i], 32768 - selection) + opxy::q31::wide(right[i], selection)) /
                32768);
        }
}
u32 sdk_master_snapshot() {
    return menu.packet();
}

u32 sdk_master_project() {
    // Called only by the full active-project parameter publication, after
    // stock has switched projects. Save and ordinary EQ edits don't reach it.
    const u32 project = stock<u32()>(0x8d584210)();
    const Controls restored = Controls::unpack(restore(project));
    menu.reset();
    menu.type = menu.candidate = restored.type;
    menu.values[0] = static_cast<i32>(restored.sweep << 21);
    master_filter::reset_project();
    master_filter::follow_visual(restored.sweep);
    publish();
    return project;
}

extern "C" void sdk_project_new() __asm__("sdk_project_new");
extern "C" void sdk_project_stock_new() __asm__("sdk_project_stock_new");
void sdk_project_new() {
    clear_pending();
    sdk_project_stock_new();
}
