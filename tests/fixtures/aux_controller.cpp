// Execute the aux controller's shared dispatch with doubles for the aux service
// and FX III: the service answers first, FX III only when it doesn't, and
// neither leaves every site to stock.
#include <cstdio>
#include <cstdlib>
#include <aux_controller.cpp>

namespace {
using opxy::i32;
using opxy::u32;
u32 fx_page, fx_turned, fx_keys, fx_swaps, aux_swaps, aux_turned;
i32 aux_page_result = -1;
u32 aux_key_result, aux_turn_result;
const u32* fx_event;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void aux_shift_m1() { ++aux_swaps; }
i32 aux_page() { return aux_page_result; }
u32 aux_key(u32, const u32*) { return aux_key_result; }
u32 aux_turn(u32, u32, u32) { ++aux_turned; return aux_turn_result; }
}

void sdk_track5_shift_m1() { ++fx_swaps; }
u32 sdk_track5_screen() { return fx_page; }
void sdk_track5_key_press(const u32* event) { ++fx_keys; fx_event = event; }
u32 sdk_track5_turn(u32, u32) { ++fx_turned; return fx_page ? 1 : 0; }

int main() {
    static const u32 m1 = 6;
    // Without the aux service (null entry points): FX III, then stock.
    require(sdk_aux_ctl_page() == -1 && !sdk_aux_ctl_turn(0, 0, 1) && fx_turned == 1, "Idle site not left to stock");
    require(!sdk_aux_ctl_key(0, &m1) && fx_keys == 1 && fx_event == &m1, "FX III did not watch the key");
    require(!sdk_aux_ctl_key(0, nullptr) && fx_keys == 1, "A null event reached FX III");
    sdk_aux_ctl_shift_m1();
    require(fx_swaps == 1, "Shift + M1 did not reach FX III");
    fx_page = 21;
    require(sdk_aux_ctl_page() == 21 && sdk_aux_ctl_turn(0, 1, 1) == 1 && fx_turned == 2, "FX III's page not served");

    // With the aux service: it answers first, and FX III is asked only when it doesn't.
    sdk_aux_controller = opxy::platform::AuxController{aux_shift_m1, aux_page, aux_key, aux_turn};
    require(sdk_aux_ctl_page() == 21, "FX III lost its page behind an idle service");
    aux_page_result = 120;
    require(sdk_aux_ctl_page() == 120, "The service's page did not win");
    aux_turn_result = 1;
    require(sdk_aux_ctl_turn(0, 1, 1) == 1 && aux_turned == 1 && fx_turned == 2, "FX III turned a service's encoder");
    aux_turn_result = 0;
    require(sdk_aux_ctl_turn(0, 1, 1) == 1 && aux_turned == 2 && fx_turned == 3, "FX III missed an unclaimed turn");
    aux_key_result = 2;
    require(sdk_aux_ctl_key(0, &m1) == 2 && fx_keys == 1, "A consumed key reached FX III or lost its result");
    aux_key_result = 0;
    require(!sdk_aux_ctl_key(0, &m1) && fx_keys == 2, "An unclaimed key skipped FX III");
    sdk_aux_ctl_shift_m1();
    require(aux_swaps == 1 && fx_swaps == 2, "Shift + M1 did not reach both");
    return 0;
}
