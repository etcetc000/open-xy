.section .text
.align 2
.extern sdk_live_input_finish
.global sdk_live_input_exit
.type sdk_live_input_exit, STT_FUNC
sdk_live_input_exit:
    /* The shared exit also runs if stock skipped the input render.
       The stock frame still has its outgoing argument area. */
    CALL sdk_live_input_finish;
    SP += 20;
    (R7:4, P5:3) = [SP++];
    UNLINK;
    RTS;
.size sdk_live_input_exit, .-sdk_live_input_exit
