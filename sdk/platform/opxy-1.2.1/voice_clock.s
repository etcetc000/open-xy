/* The synth voice adapter's clock (voice.cpp.in): the 64-bit cycle count in
   2^17-cycle ticks, about 0.33 ms at 400 MHz, wrapping after some 16 days.
   Reading CYCLES latches CYCLES2; interrupts stay masked between the two
   reads, as stock reads them (8d51b532), so no handler can latch it again. */
.section .text
.align 2
.global sdk_voice_ticks
.type sdk_voice_ticks, STT_FUNC
sdk_voice_ticks:
    CLI R2;
    R0 = CYCLES;
    R1 = CYCLES2;
    STI R2;
    R0 >>= 17;
    R1 <<= 15;
    R0 = R0 | R1;
    RTS;
.size sdk_voice_ticks, .-sdk_voice_ticks
