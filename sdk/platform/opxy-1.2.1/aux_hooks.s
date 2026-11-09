/* Aux track hooks in the aux controller (aux_tracks.cpp). Each replaces eight
 * bytes with an absolute CALL into a stub here; no stock branch lands inside
 * any site's eight bytes.
 */
.section .text
.align 2

/* 8c08f610: Shift + track key on an aux track reaches 8c08f60c(controller,
 * track), which opens the effect list for the sends and does nothing else.
 * Just after its LINK, `[--SP] = (R7:4); R4 = R1; R7 = R1; R1 = 14` fills
 * eight bytes. The stub asks sdk_aux_shift_track(controller, track): when it
 * opened a list (1), it leaves the method as its UNLINK; RTS would; otherwise it
 * makes the replaced instructions and returns into stock at 8c08f618.
 */
.extern sdk_aux_shift_track
.global sdk_aux_shift_track_hook
.type sdk_aux_shift_track_hook, STT_FUNC
sdk_aux_shift_track_hook:
    LINK 0;
    [--SP] = R0;
    [--SP] = R1;
    SP += -12;
    CALL sdk_aux_shift_track;
    SP += 12;
    R2 = R0;
    R1 = [SP++];
    R0 = [SP++];
    UNLINK;
    CC = R2 == 0;
    IF CC JUMP 1f;
    UNLINK;
    RTS;
1:
    [--SP] = (R7:4);
    R4 = R1;
    R7 = R1;
    R1 = 14;
    RTS;
.size sdk_aux_shift_track_hook, .-sdk_aux_shift_track_hook

/* 8d53d2a0: the aux LFO's destination label 8d53d160(track, group, slot),
 * after its LINK 0x10, [--SP] = (R7:4) and SP += -0x10, with track in R7 and
 * slot in R5, loads P2 = 84056d50 ("param1".."param4") over eight bytes for
 * group 0. sdk_aux_lfo_label(track, slot) answers the name of that control
 * of the machine holding the track, or 0: then the stub makes the replaced
 * load and returns into stock; otherwise it leaves the method with the name,
 * as the method's own epilogue would. R0-R3 and P0-P2 are dead at the site.
 */
.extern sdk_aux_lfo_label
.global sdk_aux_lfo_label_hook
.type sdk_aux_lfo_label_hook, STT_FUNC
sdk_aux_lfo_label_hook:
    LINK 0;
    SP += -12;
    R0 = R7;
    R1 = R5;
    CALL sdk_aux_lfo_label;
    SP += 12;
    UNLINK;
    CC = R0 == 0;
    IF CC JUMP 1f;
    SP += 0x10;
    (R7:4) = [SP++];
    UNLINK;
    RTS;
1:
    P2.L = 0x6d50;
    P2.H = 0x8405;
    RTS;
.size sdk_aux_lfo_label_hook, .-sdk_aux_lfo_label_hook
