#include "master_processor.hpp"
using namespace opxy;
namespace opxy { namespace platform { namespace master_processor {
Menu menu = {};
namespace { u32 pending_project; State pending_state; }
const Parameter* parameters(u32& count, u32 type) {
    const auto entry = registration(type);
    count = entry.parameter_count;
    return entry.parameters;
}
const Parameter* parameters(u32& count) { return parameters(count, menu.type ? menu.type : 1); }
const char* owner(u32 type) { const auto entry = registration(type); return entry.id ? entry.id : ""; }
const char* owner() { return owner(menu.type ? menu.type : 1); }
void clear_pending() { pending_project = 0; }
State current() {
    if (!menu.initialized) menu.reset();
    State state = {}; state.type = menu.type;
    for (u32 i=0;i<parameter_slots;++i) state.values[i]=menu.values[i];
    return state;
}
State defaults(u32 type) {
    State state = {}; state.type = type <= processor_count() ? type : 0;
    u32 count=0; const Parameter* p=parameters(count,state.type ? state.type : 1);
    for(u32 i=0;i<count;++i)state.values[i]=p[i].initial;
    return state;
}
void loaded_state(u32 project,const State& state) {pending_project=project;pending_state=state;}
State restore_state(u32 project) {
    State state=project && project==pending_project ? pending_state : defaults();
    if(state.type>processor_count())state=defaults();
    clear_pending();return state;
}
u32 Menu::signature() const {
    u32 hash=type;
    for(u32 i=0;i<parameter_slots;++i)hash=(hash^static_cast<u32>(values[i]))*16777619u;
    return hash;
}
void Menu::reset() {
    initialized=true;
    for(u32 n=0;n<=max_processors;++n) {
        const State initial=defaults(n);
        for(u32 i=0;i<parameter_slots;++i)bank[n][i]=initial.values[i];
    }
    for(u32 i=0;i<parameter_slots;++i)values[i]=bank[0][i];
    for(u32 i=0;i<page_controls;++i)shown[i]=i;
    type=candidate=0;open=false;
}
void Menu::show(){if(!initialized)reset();candidate=type;open=true;}
void Menu::confirm(){
    if(open) {
        if(type<=max_processors)for(u32 i=0;i<parameter_slots;++i)bank[type][i]=values[i];
        const u32 next=candidate<=processor_count() ? candidate : 0;
        if(next!=type) {
            type=next;
            for(u32 i=0;i<parameter_slots;++i)values[i]=bank[type ? type : 1][i];
            for(u32 i=0;i<page_controls;++i)shown[i]=i;
        }
    }
    open=false;
}
void Menu::cancel(){open=false;}
void Menu::click(u32 encoder){
    if(encoder>=page_controls)return;
    u32 count=0;const Parameter* p=parameters(count);const u32 slot=shown[encoder];
    if(slot>=count)return;
    u32 next=slot;
    for(u32 n=1;n<count;++n)if(p[(slot+n)%count].position==encoder){next=(slot+n)%count;break;}
    if(next!=slot)shown[encoder]=next;else values[slot]=p[slot].initial;
}
}}}
