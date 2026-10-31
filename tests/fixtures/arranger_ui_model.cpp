#include "../../sdk/platform/opxy-1.2.1/arranger_ui_model.hpp"
#include "../../sdk/platform/opxy-1.2.1/arranger_bounce_destination.hpp"
extern "C" int puts(const char*);
extern "C" void exit(int);
using namespace opxy;
using namespace opxy::platform;
void require(bool ok, const char* message) { if (!ok) { puts(message); exit(1); } }
int main() {
    u32 lane = 0;
    require(arranger_sync_lane(5, false, lane) && lane == 5, "entry follows stock instrument");
    require(!arranger_sync_lane(5, false, lane), "same lane keeps selection state");
    require(!arranger_sync_lane(14, false, lane) && lane == 5, "AUX cannot become timeline lane");
    require(!arranger_sync_lane(2, true, lane) && lane == 5, "captured edit keeps its lane");
    require(arranger_sync_lane(2, false, lane) && lane == 2, "return follows latest instrument");
    arranger::Model model;
    ArrangerClipboard clipboard = {};
    const arranger::Clip clip = {2, 3, 480, 960, 240, 1920};
    const i32 slot = model.insert(clip);
    require(slot >= 0, "insert fixture");
    require(clipboard.paste(model, 2, 1920) == -1, "empty clipboard rejects paste");
    require(clipboard.cut(model, slot) && !model.clip(slot), "cut removes clip after copying");
    require(!clipboard.cut(model, slot) && clipboard.valid, "invalid cut preserves clipboard");
    require(clipboard.paste(model, 1, 0) == -1, "cross track paste cannot relabel source");
    const i32 first = clipboard.paste(model, 2, 0);
    const i32 second = clipboard.paste(model, 2, 960);
    require(first >= 0 && second >= 0 && clipboard.valid, "repeated adjacent pastes retain clipboard");
    require(model.clip(second)->pattern == 3 && model.clip(second)->offset == 240 &&
        model.clip(second)->source_length == 1920, "paste preserves source identity and phase");
    require(clipboard.paste(model, 2, 480) == -1 && clipboard.valid, "collision preserves clipboard");
    require(!model.move(first, 2, 480) && model.clip(first)->start == 0, "rejected encoder move is atomic");
    require(!model.resize(first, 1440) && model.clip(first)->length == 960, "rejected extension is atomic");
    require(model.resize(first, 480) && model.clip(first)->offset == 240, "right trim preserves left edge and source phase");
    require(arranger_shift_time(0, -128, 480) == 0, "cursor lower clamp");
    require(arranger_shift_time(0xfffffffeu, 1, 480) == 0xffffffffu, "cursor upper clamp");
    require(arranger_shift_time(960, -1, 480) == 480 && arranger_shift_time(960, 1, 1) == 961, "grid and fine navigation");
    require(arranger_zoom(15360, 128) == 7680 && arranger_zoom(15360, -128) == 491520, "zoom limits");
    require(arranger_zoom(arranger_zoom(15360, -1), 1) == 15360, "zoom reversible");
    require(arranger_cursor_time(481, 1, 480) == 960 && arranger_cursor_time(481, -1, 480) == 480,
        "grid navigation from fine position reaches adjacent grid point");
    require(arranger_cursor_time(481, -2, 480) == 0 && arranger_cursor_time(481, 2, 480) == 1440,
        "multi detent grid movement counts crossed grid points");
    require(arranger_cursor_time(481, 1, 1) == 482 && arranger_cursor_time(481, 0, 480) == 481,
        "fine and stationary cursor preserved");
    ArrangerStopGesture stop = {};
    require(stop.begin() && !stop.begin(), "stop repeats do not start another gesture");
    require(stop.release() && !stop.release(), "stop alone forwards exactly one down/up pair on release");
    for (u32 modifier = 53; modifier <= 54; ++modifier) {
        require(stop.begin(), "new stop modifier gesture");
        stop.modify();
        require(!stop.begin() && !stop.release(), "stop minus/plus and repeats never forward stop");
    }
    require(stop.begin(), "stop before changing view");
    // Ownership survives leaving the timeline; release handling precedes view routing.
    require(stop.release(), "view exit still completes pending stock stop exactly once");
    require(stop.begin(), "stop before project restore");
    stop.cancel();
    require(!stop.release() && !stop.pending, "project restore consumes old gesture without stopping new project");
    require(stop.begin() && stop.release(), "next stop works after cancelled gesture");
    require(arranger_zoom(7680,1,480)==7042 && arranger_zoom(7042,-1,480)==7680,
        "piano zoom crosses the one-bar boundary in both directions");
    require(arranger_zoom(480,128,480)==480 && arranger_zoom(491520,-128,480)==491520,
        "piano zoom remains bounded without reducing the maximum view");
    u32 width = 7680;
    for (u32 i = 0; i < 48; ++i) {
        const u32 next = arranger_zoom(width, -1);
        require(next > width && next * 100u < width * 110u, "zoom changes less than ten percent per step");
        require(arranger_zoom(next, 1) == width, "every zoom step reverses exactly");
        const u32 relative = width * 3u / 4u;
        const u32 anchored = arranger_zoom_anchor(relative, width, next);
        require(anchored <= next && anchored * 4096u / next + 1u >= relative * 4096u / width,
            "fractional zoom keeps cursor screen position");
        width = next;
    }
    require(arranger_zoom_anchor(12345, 15360, 15360) == 12345, "unchanged zoom cannot drift");
    require(width == 491520 && arranger_zoom(15360, -8) == 30720,
        "eight fine steps double the visible window");
    ArrangerStopSequence sequence = {};
    require(sequence.target(2400) == 2400 && sequence.target(2400) == 0 && sequence.target(9000) == 0,
        "first stop edit cursor then consecutive stops timeline origin");
    sequence.interrupt();
    require(sequence.target(9600) == 9600, "intervening action starts a new stop sequence");
    arranger::Model boundaries;
    require(boundaries.insert(arranger::Clip{1,0,100,100,0,100}) >= 0 &&
        boundaries.insert(arranger::Clip{1,0,300,100,0,100}) >= 0 &&
        boundaries.insert(arranger::Clip{1,0,400,100,0,100}) >= 0 &&
        boundaries.insert(arranger::Clip{2,0,220,10,0,100}) >= 0, "boundary fixtures");
    require(arranger_boundary(boundaries,1,0,1)==100 && arranger_boundary(boundaries,1,100,1)==200 &&
        arranger_boundary(boundaries,1,200,1)==300, "start end next start ordered across gaps");
    require(arranger_boundary(boundaries,1,300,2)==500, "shared adjacent end/start visited only once");
    require(arranger_boundary(boundaries,1,500,-1)==400 && arranger_boundary(boundaries,1,400,-1)==300 &&
        arranger_boundary(boundaries,1,150,-1)==100, "backward strict boundaries and midclip navigation");
    require(arranger_boundary(boundaries,1,500,1)==500 && arranger_boundary(boundaries,1,100,-1)==100 &&
        arranger_boundary(boundaries,7,123,1)==123, "boundary navigation clamps and ignores other lanes");
    require(arranger_bar_spacing(7680)==7680 && arranger_bar_spacing(491520)==30720,
        "grid thins to four bars only at widest zoom");
    require(arranger_resize_length(7680,-12,480)==1920 && arranger_resize_length(7680,-128,480)==480,
        "E4 supports quarter-bar clips and clamps shrinking to one grid step");
    require(arranger_resize_length(480,-1000,1)==1 && arranger_resize_length(1,-1,1)==1 &&
        arranger_resize_length(0,-1,0)==1, "fine edits never create a zero-length clip");
    require(arranger_resize_length(120,-1,480)==120 && arranger_resize_length(120,0,480)==120 &&
        arranger_resize_length(480,1,480)==960 && arranger_resize_length(7680,1,480)==8160,
        "coarse edits preserve already-short clips and normal growth");
    arranger::Model short_clip;
    const i32 short_slot = short_clip.insert(arranger::Clip{0,0,0,7680,0,7680});
    require(short_slot >= 0 && short_clip.resize(short_slot, arranger_resize_length(7680,-12,480)) &&
        short_clip.clip(short_slot)->length == 1920 && short_clip.clip(short_slot)->source_length == 7680,
        "shortening changes the timeline window without destroying its source phrase");
    ArrangerBounceChoice bounce = {};
    require(!bounce.accept(), "closed bounce cannot confirm");
    bounce.begin();
    require(bounce.engine == 2 && !bounce.replace && bounce.accept(), "keep defaults to sampler and single confirm");
    bounce.turn(1, 1);
    require(!bounce.accept() && bounce.confirmed, "replace requires explicit second press");
    bounce.turn(0, 1);
    require(bounce.engine == 3 && !bounce.confirmed && !bounce.accept(), "engine change clears destructive confirmation");
    require(bounce.accept(), "second distinct confirmation accepts replacement");
    bounce.cancel();
    require(!bounce.open && !bounce.accept(), "cancel prevents stale confirmation");
    bounce.begin();
    require(!bounce.replace && !bounce.confirmed && bounce.engine == 2, "reopen clears previous choice");
    arranger::Model before, rendered;
    const i32 bounce_source = before.insert(arranger::Clip{2,1,480,150000,0,7680});
    const i32 overlap = before.insert(arranger::Clip{3,0,1000,7680,0,7680});
    const i32 distant = before.insert(arranger::Clip{3,0,200000,7680,0,7680});
    arranger::Clip bounce_clip; u32 bounce_steps = 0, bounce_scale = 0;
    require(!arranger_bounce_layout(before,bounce_source,2,false,rendered,bounce_clip,bounce_steps,bounce_scale),
        "occupied lane below requires replacement confirmation");
    require(before.clip(overlap) && before.clip(bounce_source), "failed candidate never mutates originals");
    require(arranger_bounce_layout(before,bounce_source,2,true,rendered,bounce_clip,bounce_steps,bounce_scale),
        "confirmed replacement constructs candidate");
    require(rendered.clip(bounce_source)->pattern == 1 && rendered.clip(distant)->pattern == 0 &&
        bounce_clip.track == 3 && bounce_clip.pattern == 2 && bounce_clip.start == 480,
        "source and distant shared-source clips preserved, new source on lane below");
    require(bounce_steps <= 255 && bounce_scale == 5 && bounce_clip.source_length >= bounce_clip.length,
        "long render uses slow scale and cannot retrigger within clip");
    const i32 lastlane = before.insert(arranger::Clip{7,0,0,7680,0,7680});
    require(!arranger_bounce_layout(before,lastlane,1,true,rendered,bounce_clip,bounce_steps,bounce_scale),
        "last lane has no destination below");
    arranger::Model publish_model = before, backup;
    require(!arranger_bounce_publish(publish_model,rendered,backup,[](){return false;},[](){return true;}) &&
        publish_model.clip(overlap) && publish_model.clip(overlap)->pattern == 0,
        "rejected publication restores overlapping clips and original source references");
    require(!arranger_bounce_publish(publish_model,rendered,backup,[&publish_model](){
        publish_model = arranger::Model(); return false;
    },[](){return false;}) && !publish_model.clip(bounce_source),
        "generation change keeps newly restored project instead of stale rollback");
    return 0;
}
