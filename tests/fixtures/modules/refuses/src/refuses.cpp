// A module whose init refuses: the loader must keep it out and go on.
#include <opxy/module.hpp>
using namespace opxy;

extern "C" int opxy_module_init(const ModuleApi* api) {
    if (api && api->note) api->note(api->gui ? "refusing on gui" : "refusing on audio");
    return 1;
}
