/* Diagnostic callback stopwatch. Unsigned subtraction handles one 32-bit wrap. */
.section .text
.align 2
.global sdk_benchmark_cycles
.type sdk_benchmark_cycles, STT_FUNC
sdk_benchmark_cycles:
    R0 = CYCLES;
    RTS;
.size sdk_benchmark_cycles, .-sdk_benchmark_cycles
