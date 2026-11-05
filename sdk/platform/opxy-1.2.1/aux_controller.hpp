// The aux controller's shared hook sites (aux_controller.cpp): the features that
// change an aux track's page, keys and encoders register here.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// The aux service's entry points (aux_tracks.cpp), which its startup fills in;
// null in builds that don't link it. Results as aux_controller.cpp describes.
struct AuxController {
    void (*shift_m1)();
    i32 (*page)();
    u32 (*key)(u32 controller, const u32* event);
    u32 (*turn)(u32 controller, u32 encoder, u32 event);
};
}}

extern "C" {
extern opxy::platform::AuxController sdk_aux_controller __asm__("sdk_aux_controller");
}
