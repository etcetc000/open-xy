#include <cstdio>
#include <cstdlib>
#include "../../sdk/platform/opxy-1.2.1/arranger_history.hpp"
using namespace opxy;
using opxy::arranger::Clip;
using opxy::arranger::Model;
using opxy::platform::ArrangerHistory;
namespace {
void require(bool yes, const char* label) { if (!yes) { std::puts(label); std::exit(1); } }
ArrangerHistory history;
Model model;
u32 track, cursor;
const u32 project = 0x12345678, generation = 42;
void begin(u32 group = 0) { history.begin(model, track, cursor, project, generation, group); }
bool undo() { return history.undo(model, track, cursor, project, generation); }
bool redo() { return history.redo(model, track, cursor, project, generation); }
void clear() { history.reset(); model = Model{}; track = cursor = 0; }
}
int main() {
    require(!undo() && !redo(), "empty history");
    begin(); require(model.insertAt(0, Clip{0,0,0,7680,0,7680}), "insert");
    cursor=7680;require(history.commit(model), "insert recorded");
    require(undo() && !model.clip(0) && cursor==0, "undo insert and cursor");
    require(redo() && model.clip(0) && cursor==7680, "redo insert and cursor");
    begin();require(model.move(0,1,15360), "move");track=1;cursor=15360;
    require(history.commit(model), "move recorded");
    require(undo() && model.clip(0)->track==0 && track==0 && cursor==7680, "undo lane/cursor");
    require(redo() && model.clip(0)->track==1 && track==1 && cursor==15360, "redo lane/cursor");
    begin();require(model.resize(0,15360), "resize");require(history.commit(model), "resize recorded");
    require(undo() && model.clip(0)->length==7680, "undo resize");
    require(redo() && model.clip(0)->length==15360, "redo resize");
    begin();require(model.remove(0), "remove");cursor=0;require(history.commit(model), "remove recorded");
    require(undo() && model.clip(0) && cursor==15360, "undo delete");
    require(redo() && !model.clip(0), "redo delete");

    clear();begin();require(model.setLoop(true,7680,23040), "loop change");
    cursor=123;model.seek(999);require(history.commit(model), "loop recorded");
    require(undo() && !model.loopEnabled() && model.loopStart()==0 && model.loopEnd()==1 && cursor==0,
        "undo loop points and toggle");
    require(redo() && model.loopEnabled() && model.loopStart()==7680 && model.loopEnd()==23040 &&
        cursor==123 && model.position()==999, "redo loop independent cursor and playhead");
    begin();cursor=456;model.seek(321);require(!history.commit(model), "navigation alone not recorded");
    require(undo() && !model.loopEnabled(), "navigation does not add undo step");
    require(redo() && cursor==456 && model.position()==321, "redo restores context immediately before undo");

    clear();require(model.insertAt(0,Clip{0,0,0,7680,0,7680}), "base clip");
    begin(101);require(model.move(0,0,7680), "first drag");cursor=7680;history.commit(model);
    begin(101);require(model.move(0,0,15360), "second drag");cursor=15360;history.commit(model);
    begin(101);require(!model.resize(0,0), "failed turn");require(!history.commit(model), "failed turn no history");
    begin(101);require(model.move(0,0,23040), "third drag");cursor=23040;history.commit(model);
    require(undo() && model.clip(0)->start==0 && cursor==0 && !history.canUndo(), "drag grouped into one undo");
    require(redo() && model.clip(0)->start==23040 && cursor==23040, "redo grouped drag");
    history.breakGroup();begin(101);require(model.move(0,0,30720), "new gesture");history.commit(model);
    require(undo() && model.clip(0)->start==23040 && history.canUndo(), "break splits same encoder group");
    begin(102);require(!model.resize(0,0), "failed branch edit");history.commit(model);
    require(history.canRedo() && redo() && model.clip(0)->start==30720, "failed edit preserves redo");
    require(undo(), "undo before branch");begin();require(model.resize(0,15360), "branch edit");history.commit(model);
    require(!history.canRedo() && !redo(), "real edit truncates redo");
    begin();history.cancel();require(!history.commit(model), "cancel abandons pending capture");

    // Split and MakeUnique are model reference transactions. Their appended
    // native source data stays outside history and must retain its identity.
    clear();require(model.insertAt(0,Clip{0,0,0,15360,0,15360}), "split source");
    begin();require(model.resize(0,7680), "left half");
    require(model.insertAt(1,Clip{0,0,7680,7680,7680,15360}), "right half");cursor=7680;history.commit(model);
    require(undo() && model.clip(0)->length==15360 && !model.clip(1), "undo split atomically");
    require(redo() && model.clip(1)->offset==7680, "redo split restores source offset");
    u32 native_source_count=1;
    begin();Clip unique=*model.clip(1);unique.pattern=static_cast<u8>(native_source_count++);
    require(model.remove(1)&&model.insertAt(1,unique), "unique reference");history.commit(model);
    require(undo() && model.clip(1)->pattern==0 && native_source_count==2, "undo unique keeps appended source alive");
    require(redo() && model.clip(1)->pattern==1 && native_source_count==2, "redo unique reuses same retained source");
    require(!history.undo(model,track,cursor,project,generation+1) && !history.canUndo() && !history.canRedo(),
        "generation change invalidates history without modifying model");
    require(model.clip(1)->pattern==1, "stale undo leaves model intact");
    begin();require(model.remove(1), "generation reset edit");history.commit(model);
    require(!history.undo(model,track,cursor,project+4,generation) && !model.clip(1), "project identity also invalidates");

    clear();require(model.insertAt(0,Clip{0,0,0,7680,0,7680}), "capacity source");
    for(u32 i=1;i<=ArrangerHistory::capacity+3;++i){
        begin();require(model.move(0,0,i*7680), "capacity move");require(history.commit(model), "capacity commit");
    }
    for(u32 i=0;i<ArrangerHistory::capacity;++i)require(undo(), "bounded undo retained");
    require(!undo() && model.clip(0)->start==3*7680, "oldest entries evicted");
    for(u32 i=0;i<ArrangerHistory::capacity;++i)require(redo(), "bounded redo retained");
    require(!redo() && model.clip(0)->start==(ArrangerHistory::capacity+3)*7680, "full redo after eviction");
    clear();require(model.insertAt(0,Clip{0,0,0,7680,0,7680}), "fractional history setup");
    begin();Clip fractional=*model.clip(0);fractional.phase_remainder=105;
    require(model.replace(0,fractional) && history.commit(model), "fractional-only edit records history");
    require(undo() && model.clip(0)->phase_remainder==0, "undo fractional phase");
    require(redo() && model.clip(0)->phase_remainder==105, "redo fractional phase");
    begin(); require(model.setMuted(0,true) && history.commit(model), "mute-only edit records history");
    require(undo() && !model.clip(0)->muted, "undo mute");
    require(redo() && model.clip(0)->muted, "redo mute");
    require(model.active(0,0,cursor)==model.clip(0), "muted clip stays selectable");
    const i32 split_muted=model.split(0,3840);
    require(split_muted>=0 && model.clip(static_cast<u32>(split_muted))->muted && model.clip(0)->muted,
        "split preserves both instance mute flags");
    const i32 copy_muted=model.duplicate(0,7680);
    require(copy_muted>=0 && model.clip(static_cast<u32>(copy_muted))->muted, "duplicate preserves mute");
    require(model.setMuted(0,false) && model.clip(static_cast<u32>(copy_muted))->muted,
        "unmuting one instance leaves shared pattern instance muted");
    require(!model.setMuted(255,true), "invalid mute target rejected");
    history.reset();require(!history.canUndo()&&!history.canRedo(), "explicit reset");
    return 0;
}
