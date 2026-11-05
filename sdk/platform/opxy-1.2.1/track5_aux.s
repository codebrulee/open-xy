/* FX III's stubs (track5_fx.cpp). Its audio runs from Aux 5's stage (aux5_hooks.s). */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.extern sdk_track5_pick
.align 2

/* 8c08f7d2: encoder 1's press on Aux 5's M1, CALL.A 8d4db490, the flag it inverts. */
.extern sdk_track5_enabled
.global sdk_track5_click
.type sdk_track5_click, STT_FUNC
sdk_track5_click:
    LINK 12;
    CALL sdk_track5_enabled;
    UNLINK;
    RTS;
.size sdk_track5_click, .-sdk_track5_click

/* 8d53b83e: the FX page's name call (R0 the track); P4 holds the page. */
.extern sdk_track5_name
.global sdk_track5_name_call
.type sdk_track5_name_call, STT_FUNC
sdk_track5_name_call:
    R1 = P4;
    JUMP.L sdk_track5_name;
.size sdk_track5_name_call, .-sdk_track5_name_call

/* 8c08f624: stock's selector for Shift and an aux track's key (8c08f60c)
 * leaving for any track but a send, in place of its epilogue SP += 0x14;
 * (R7:4) = [SP++]; UNLINK. R4 holds the track. The stub completes the epilogue
 * and returns to the selector's caller.
 */
.global sdk_track5_key
.type sdk_track5_key, STT_FUNC
sdk_track5_key:
    HOOK_SAVE
    R0 = R4;
    CALL sdk_track5_pick;
    HOOK_RESTORE
    SP += 0x14;
    (R7:4) = [SP++];
    UNLINK;
    RTS;
.size sdk_track5_key, .-sdk_track5_key
