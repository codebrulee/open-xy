// The aux controller's four hook sites (aux_controller_hooks.s), shared by the
// features that change an aux track's pages and keys: the aux service
// (aux_tracks.cpp), linked only with a machine that takes over an aux track,
// and FX III on Aux 5 (track5_fx.cpp), linked in every build. Only one hook can
// own a site, so the stubs come here: each site asks the aux service first,
// through the entry points its startup registers (null without it), then FX III,
// and otherwise leaves the key or page to stock. They act on different tracks
// (the service on CV, track 11; FX III on Aux 5, track 12), so at most one of
// them answers.
#include "aux_controller.hpp"

extern "C" {
void sdk_aux_ctl_shift_m1() __asm__("sdk_aux_ctl_shift_m1");
opxy::i32 sdk_aux_ctl_page() __asm__("sdk_aux_ctl_page");
opxy::u32 sdk_aux_ctl_key(opxy::u32, const opxy::u32*) __asm__("sdk_aux_ctl_key");
opxy::u32 sdk_aux_ctl_turn(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_aux_ctl_turn");
// FX III (track5_fx.cpp).
void sdk_track5_shift_m1() __asm__("sdk_track5_shift_m1");
opxy::u32 sdk_track5_screen() __asm__("sdk_track5_screen");
void sdk_track5_key_press(const opxy::u32* event) __asm__("sdk_track5_key_press");
opxy::u32 sdk_track5_turn(opxy::u32 encoder, opxy::u32 event) __asm__("sdk_track5_turn");
}

opxy::platform::AuxController sdk_aux_controller;

namespace {
using opxy::i32;
using opxy::u32;
using opxy::platform::AuxController;
const AuxController& service = sdk_aux_controller;
}

// 8c08f6ec: Shift + M1, which stock ignores and reports handled.
void sdk_aux_ctl_shift_m1() {
    if (service.shift_m1) service.shift_m1();
    sdk_track5_shift_m1();
}

// 8c08f540: the page the selected track shows on its tab, or -1 for stock's.
i32 sdk_aux_ctl_page() {
    const i32 page = service.page ? service.page() : -1;
    if (page >= 0) return page;
    const u32 fx = sdk_track5_screen();
    return fx ? static_cast<i32>(fx) : -1;
}

// 8c08fd30: an unshifted key. 0 leaves it to stock; 1 or 2 is stock's result
// (0 or 1) plus one. FX III only watches M1, so it never consumes a key.
u32 sdk_aux_ctl_key(u32 controller, const u32* event) {
    const u32 result = service.key ? service.key(controller, event) : 0;
    if (result) return result;
    if (event) sdk_track5_key_press(event);
    return 0;
}

// 8c08fa14: an encoder turn. Nonzero when it was handled here.
u32 sdk_aux_ctl_turn(u32 controller, u32 encoder, u32 event) {
    if (service.turn && service.turn(controller, encoder, event)) return 1;
    return sdk_track5_turn(encoder, event);
}
