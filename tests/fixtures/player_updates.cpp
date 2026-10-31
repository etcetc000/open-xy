#include <cstdio>
#include <cstdlib>
#include "player_object_probeplayer.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
namespace { opxy::u32 updates=0, releases=0, resets=0, offs=0, ticks=0, before_tick=0; }
void release_stock(void*,opxy::u32) { ++releases; }
void reset_stock(void*) { ++resets; }
void all_off_stock(void*) { ++offs; }
void step_stock(void*) { ++ticks; require(updates==before_tick); }
const void* stock_address(opxy::u32 address) {
    using namespace opxy::platform;
    if(address==kMaestroRelease) return reinterpret_cast<const void*>(&release_stock);
    if(address==kMaestroReset) return reinterpret_cast<const void*>(&reset_stock);
    if(address==kMaestroStep) return reinterpret_cast<const void*>(&step_stock);
    require(address==kMaestroAllOff); return reinterpret_cast<const void*>(&all_off_stock);
}
namespace opxy { namespace platform {
void* player_state(void* object) { return object; }
player::Controls player_controls(void*) { return player::Controls{}; }
void play_plan(void*,u32,const player::Plan&) {}
void update_plan(void*,const player::Update& change) { require(change.root==60); ++updates; }
}}
extern "C" const char probeplayer_player_id[]="";
int main() {
    const auto* vt=probeplayer_player()->vtable;
    auto on=reinterpret_cast<opxy::u32(*)(void*,opxy::u32,opxy::u32,opxy::u32,opxy::u32)>(const_cast<void*>(vt[5]));
    auto off=reinterpret_cast<void(*)(void*,opxy::u32)>(const_cast<void*>(vt[6]));
    auto reset=reinterpret_cast<void(*)(void*)>(const_cast<void*>(vt[7]));
    auto alloff=reinterpret_cast<void(*)(void*)>(const_cast<void*>(vt[10]));
    auto step=reinterpret_cast<void(*)(void*)>(const_cast<void*>(vt[13]));
    probeplayer::State s={}; s.option=3;
    on(&s,60,0,0,90); require(updates==2 && s.pending==0);
    off(&s,60); require(updates==3 && releases==1);
    s.pending=100; on(&s,61,0,0,90); require(updates==11 && s.pending==92); // bounded drain
    reset(&s); require(resets==1 && s.pending==0 && s.option==3);
    s.pending=100; before_tick=updates+8; step(&s);
    require(ticks==1 && s.pending==92); // bounded updates precede stock scheduling
    s.pending=1; before_tick=updates+1; step(&s);
    require(ticks==2 && s.pending==0);
    s.pending=2; alloff(&s); require(offs==1 && s.pending==0 && s.option==3);
    std::puts("optional player updates and reset callbacks executed through generated vtable");
}
