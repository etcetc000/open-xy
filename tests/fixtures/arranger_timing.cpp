#include "../../sdk/platform/opxy-1.2.1/arranger_timing.hpp"
#include <cstdio>
#include <cstdlib>
using namespace opxy;
using namespace opxy::platform;
static void require(bool ok, const char* message) { if (!ok) { std::puts(message); std::exit(1); } }
int main() {
    require(arranger_step_ticks(3)==480 && arranger_step_ticks(1)==240 && arranger_step_ticks(5)==960,
        "native normal faster slower step mapping");
    require(!arranger_step_ticks(16), "invalid scale rejected");
    for (u32 old=0; old<16; ++old) for (u32 next=0; next<16; ++next) {
        const u32 inputs[] = {1,480,7680,30720,491520,0xffffffffu};
        for (u32 n=0;n<6;++n) {
            const u32 cap=64*arranger_step_ticks(next);
            const unsigned long long expected=static_cast<unsigned long long>(inputs[n])*arranger_source_rate(old)/arranger_source_rate(next);
            require(arranger_rescaled_ticks(inputs[n],old,next,cap)==(expected>cap?cap:expected),
                "bounded conversion matches wide reference across all native rates");
        }
        arranger::Model model;
        const u32 original=16*arranger_step_ticks(old);
        require(model.insert({0,0,100,original,77,64*480,17})==0,"scale setup");
        require(arranger_rescale_clip(model,0,old,next),"rescale all combinations");
        require(model.clip(0)->length==16*arranger_step_ticks(next) && model.clip(0)->start==100 &&
            model.clip(0)->offset==77 && model.clip(0)->phase_remainder==17 && model.clip(0)->source_length==64*480,
            "scale changes only visible duration");
        require(arranger_rescale_clip(model,0,next,old) && model.clip(0)->length==original,"whole step scale roundtrip");
    }
    arranger::Model model;
    require(model.insert({0,0,100,7680,0,30720})==0,"collision setup");
    require(model.insert({0,1,10000,480,0,30720})==1,"same lane next");
    require(model.insert({1,0,8000,480,0,30720})==2,"other lane ignored");
    require(arranger_rescale_clip(model,0,3,7) && model.clip(0)->length==9900,"slower clamps exactly to next lane clip");
    require(model.clip(1)->start==10000 && model.clip(1)->length==480,"neighbor untouched");
    require(!arranger_rescale_clip(model,0,16,3) && model.clip(0)->length==9900,"invalid scale no mutation");
    require(arranger_clip_limit(model,0,3,8)==8*480,"internal eight-step source bounds resize");
    require(arranger_rescale_clip(model,0,3,3,8) && model.clip(0)->length==8*480,"internal source shortening clamps visible trim");
    require(!arranger_clip_limit(model,0,3,0) && !arranger_clip_limit(model,0,3,65),"invalid source lengths rejected");
    arranger::Model edge;
    require(edge.insert({0,0,0xffffffffu-100,1,0,30720})==0,"end setup");
    require(arranger_clip_limit(edge,0,15)==100 && arranger_rescale_clip(edge,0,3,15) && edge.clip(0)->length==100,
        "timeline end caps without wrap and permits substep gap");
    arranger::Model tiny;
    require(tiny.insert({0,0,0,1,0,30720})==0,"tiny setup");
    require(arranger_rescale_clip(tiny,0,3,15) && tiny.clip(0)->length==arranger_step_ticks(15),"minimum one step");
    require(!arranger_clip_limit(tiny,1,3) && !arranger_rescale_clip(tiny,1,3,3),"missing clip");
}
