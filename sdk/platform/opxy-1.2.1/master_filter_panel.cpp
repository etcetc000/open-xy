// Shared GUI dispatch; each extension owns its typed retained display instance.
#include "master_processor.hpp"
namespace opxy { namespace platform { namespace master_processor {
void frame(i32 elapsed) { const auto e=registration(menu.type);if(e.display)e.display->frame(elapsed); }
void parameter_reset(u32 slot) {const auto e=registration(menu.type);if(e.display)e.display->parameter_reset(slot);}
void reset_project() {for(u32 i=1;i<=processor_count();++i){const auto e=registration(i);if(e.display)e.display->reset();}}
}}}
