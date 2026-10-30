/* Stock initializer exits. The hooks JUMP here from inside stock functions
 * (0x8d506a02 in the synth initializer, 0x8d523dac and 0x8d523e08 in the
 * screen factory), so these stubs run the generated C++ startup in the stock
 * frame and then perform the stock epilogue they replaced, returning 0.
 */
.section .text
.align 2
.extern sdk_startup_synth
.extern sdk_startup_screen

.global extensions_synth_exit
.type extensions_synth_exit, STT_FUNC
extensions_synth_exit:
    SP += -12;
    CALL sdk_startup_synth;
    SP += 12;
    R0 = 0;
    SP += 24;
    (R7:7, P5:3) = [SP++];
    UNLINK;
    RTS;
.size extensions_synth_exit, .-extensions_synth_exit

.global extensions_screen_exit
.type extensions_screen_exit, STT_FUNC
extensions_screen_exit:
    SP += -12;
    CALL sdk_startup_screen;
    SP += 12;
    R0 = 0;
    UNLINK;
    RTS;
.size extensions_screen_exit, .-extensions_screen_exit
