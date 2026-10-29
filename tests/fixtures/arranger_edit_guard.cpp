#include "../../sdk/platform/opxy-1.2.1/arranger_edit_guard.hpp"
extern "C" int puts(const char*);
extern "C" void exit(int);
using namespace opxy;
using namespace opxy::platform;
void require(bool ok, const char* message) { if (!ok) { puts(message); exit(1); } }
int main() {
    require(arranger_edit_binding_valid(true,true,100,100,4,4,7,7,15), "valid scope");
    require(!arranger_edit_binding_valid(true,false,100,100,4,4,7,7,15), "audio cannot inherit GUI edit");
    require(!arranger_edit_binding_valid(true,true,100,100,4,5,7,7,15), "reused project buffer invalidates scope");
    require(!arranger_edit_binding_valid(true,true,100,200,4,4,7,7,15), "different project invalidates scope");
    require(!arranger_edit_binding_valid(true,true,100,100,4,4,7,6,15), "selected track change invalidates scope");
    require(!arranger_edit_binding_valid(false,true,100,100,4,4,7,7,15), "closed scope");
    require(!arranger_edit_binding_valid(true,true,100,100,4,4,8,8,0), "aux track excluded");
    require(!arranger_edit_binding_valid(true,true,100,100,4,4,0,0,16), "invalid pattern excluded");
    for (u32 key=0; key<80; ++key) {
        const bool note = key>=26 && key<=52;
        const bool step = key>=56 && key<72;
        const bool encoder = key>=10 && key<=13;
        require(arranger_edit_key_allowed(key,false,false)==(note||step||key==55), "unheld engine and management keys excluded");
        require(arranger_edit_key_allowed(key,true,false)==(note||step||key==55||encoder), "step encoder scope");
        require(!arranger_edit_key_allowed(key,true,true), "pending exit blocks new presses");
        require(!arranger_edit_swallow_release(key,true), "forwarded notes retain release during exit or invalidation");
        require(arranger_edit_swallow_release(key,false)==(key<72), "blocked press has matching suppression only within bounded keys");
    }
    ArrangerRawEditGate gate = {};
    require(gate.consume(50,true,true,false,false), "blocked down never reaches held insertion");
    require(gate.consume(50,true,false,true,false), "blocked repeat remains blocked after exit");
    require(gate.consume(50,false,false,true,false), "matching release consumed even after exit");
    require(!gate.consume(50,true,false,true,false), "next intentional gesture unaffected");
    require(!gate.consume(26,true,true,true,false), "note down enters stock held bookkeeping");
    require(gate.consume(26,true,true,false,true), "repeat rejected after binding invalidation");
    require(!gate.consume(26,false,false,true,true), "old sounding note release preserved");
    require(!gate.consume(13,false,true,false,true), "initiating encoder release preserved");
    require(!gate.consume(4,true,true,true,false), "arrange exit remains immediate");
    return 0;
}
