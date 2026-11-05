/* Stock Drumline's second page (drumline.hpp): the two
 * hooks of the build option drumlinePage2. Each replaces one whole instruction
 * pair outside every hardware loop body and replays it.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_dbox_methods
.extern sdk_drumline_page

/* 8d51b554: P0 = [P1]; R0 = P1 (4891 4130) in the drum renderer 8d51b274,
 * which then loads the voice's render, P0 = [P0 + 0xc] (8d51b558), and calls it
 * (8d51b55a) with R0 the machine object ([voice + 0x94]), R1 the mono block
 * 0x11b01948, R2 the pad's payload and the track's words at [SP + 0xc]. A
 * stock dbox voice's method table (0x8404ce3c) becomes sdk_dbox_methods, whose
 * +12 is sdk_dbox_render; every other voice keeps its own. R3 is free here
 * (stock stored it at [SP + 0xc]) but is kept anyway; the pushes stay below
 * the caller's stack. */
.global sdk_dbox_render_select
.type sdk_dbox_render_select, STT_FUNC
sdk_dbox_render_select:
    [--SP] = ASTAT;
    [--SP] = R3;
    P0 = [P1];
    R0 = P0;
    R3.H = 0x8404; R3.L = 0xce3c;
    CC = R0 == R3;
    IF !CC JUMP .Ldbox_other;
    P0.H = sdk_dbox_methods;
    P0.L = sdk_dbox_methods;
.Ldbox_other:
    R0 = P1;
    R3 = [SP++];
    ASTAT = [SP++];
    RTS;
.size sdk_dbox_render_select, .-sdk_dbox_render_select

/* 8d531192: R0 = B[P4 + 0xb4] (Z) (a0e4 b400) in the dbox page, P4 the page,
 * where both of its branches end once they have set the four titles and values:
 * the Shift branch (8d530fb8..8d531190) falls through, the unshifted one jumps
 * back from 8d5314e2. */
.global sdk_drumline_page_hook
.type sdk_drumline_page_hook, STT_FUNC
sdk_drumline_page_hook:
    HOOK_SAVE
    R0 = P4;
    CALL sdk_drumline_page;
    HOOK_RESTORE
    R0 = B[P4 + 0xb4] (Z);
    RTS;
.size sdk_drumline_page_hook, .-sdk_drumline_page_hook
