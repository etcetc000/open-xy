#include "checkpoint_control.hpp"

extern "C" const opxy::platform::checkpoint::Support* sdk_checkpoint_control_support() {
    namespace cp = opxy::platform::checkpoint;
    static const cp::Support support = {cp::kept::save, cp::kept::validate, cp::kept::load};
    return &support;
}
