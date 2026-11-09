/* Aux 5's stage (aux5_stage.cpp, generated). DSP board: 8d51234e, the mixer's
 * CALL of Aux 5's audio processor 8d50fdc0, replaced by a call. The stage runs
 * the build's aux 5 effect, if any, then stock's processor, and passes the
 * cycles both took to the build's aux 5 timer, if any. The mixer has summed
 * tracks 1-8 into the aux bus by now and keeps its address at [SP + 0x54],
 * which LINK 0 moves to [FP + 0x5c]; the bus holds 64 interleaved stereo frames.
 * The processor gets the caller's R0-R2 and this frame's 12-byte argument area,
 * and its result comes back in R0.
 */
.section .text
.extern sdk_aux5_effect
.extern sdk_aux5_time
.align 2

.global sdk_aux5_call
.type sdk_aux5_call, STT_FUNC
sdk_aux5_call:
    LINK 0;
    [--SP] = (R7:4);
    SP += -12;
    R7 = R0;
    R6 = R1;
    R4 = R2;
    R5 = CYCLES;
    R2 = [FP + 0x5c];
    CALL sdk_aux5_effect;
    R0 = R7;
    R1 = R6;
    R2 = R4;
    P1.H = 0x8d50; P1.L = 0xfdc0;
    CALL (P1);
    R7 = R0;
    R1 = CYCLES;
    R0 = R5;
    CALL sdk_aux5_time;
    R0 = R7;
    SP += 12;
    (R7:4) = [SP++];
    UNLINK;
    RTS;
.size sdk_aux5_call, .-sdk_aux5_call
