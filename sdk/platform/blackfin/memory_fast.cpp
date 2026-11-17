// fast_helpers (docs/cpu-capabilities.md): the device's memset and memcpy as
// hand-written assembly. A build links this file as its `memory` unit in place of
// memory.cpp only when the CPU extension's fast_helpers setting is 1; every other
// build, and every host and browser-preview build, links memory.cpp unchanged.
//
// Hand-written, so the compiler cannot turn the loops into calls of themselves and
// nothing depends on its scheduling. Every byte written is the one memory.cpp's byte
// loops write, for every length, alignment and value:
// - memset: under 16 bytes, the byte loop; else a byte and a half-word up to a
//   word boundary, whole words of the replicated byte, then the 0-3 byte tail.
// - memcpy: under 16 bytes, or when the two ranges overlap at all, the forward
//   byte loop (an overlapping copy behaves exactly as it always did). The two
//   overlap checks change no result: the wide paths run only when source and
//   destination agree mod 4 or mod 2, so overlapping ranges are then at least a
//   unit apart and a forward unit copy writes the bytes the byte loop writes
//   (tests/test_runtime_helpers.py passes with them removed). They stay so no
//   wide path ever runs over overlapping ranges, which keeps the code simple to
//   argue about. Else,
//   when source and destination agree mod 4, a byte and a half-word up to a
//   word boundary, whole words, then the tail; when they agree mod 2 only,
//   half-words; otherwise the byte loop.
// Both return the destination in R0 and use no stack. They clobber R1-R3,
// P0-P2, LC0/LT0/LB0 and CC, all call-clobbered. (memory.cpp's compiled C uses
// LC1; hand assembly that calls these inside an LC0 loop must save it.)
// tests/test_runtime_helpers.py runs this assembly in an instruction model.
asm(R"(
    .pushsection .text
    .p2align 2
    .global _memset
    .type _memset, STT_FUNC
_memset:
    P0 = R0;
    R3 = 15;
    CC = R2 <= R3 (IU);
    IF CC JUMP .Lset_bytes;
    R1 = R1.B (Z);
    R3 = R1 << 8;
    R1 = R1 | R3;
    R3 = R1 << 16;
    R1 = R1 | R3;
    R3 = P0;
    CC = BITTST (R3, 0);
    IF !CC JUMP .Lset_half;
    B[P0++] = R1;
    R2 += -1;
    R3 = P0;
.Lset_half:
    CC = BITTST (R3, 1);
    IF !CC JUMP .Lset_words;
    W[P0++] = R1;
    R2 += -2;
.Lset_words:
    R3 = R2 >> 2;
    P1 = R3;
    LSETUP (.Lset_word, .Lset_word) LC0 = P1;
.Lset_word:
    [P0++] = R1;
    CC = BITTST (R2, 1);
    IF !CC JUMP .Lset_last;
    W[P0++] = R1;
.Lset_last:
    CC = BITTST (R2, 0);
    IF !CC JUMP .Lset_done;
    B[P0] = R1;
.Lset_done:
    RTS;
.Lset_bytes:
    CC = R2 == 0;
    IF CC JUMP .Lset_done;
    P1 = R2;
    LSETUP (.Lset_byte, .Lset_byte) LC0 = P1;
.Lset_byte:
    B[P0++] = R1;
    RTS;
    .size _memset, .-_memset

    .p2align 2
    .global _memcpy
    .type _memcpy, STT_FUNC
_memcpy:
    P0 = R0;
    P1 = R1;
    R3 = 15;
    CC = R2 <= R3 (IU);
    IF CC JUMP .Lcopy_bytes;
    R3 = R0 - R1;
    CC = R3 < R2 (IU);
    IF CC JUMP .Lcopy_bytes;
    R3 = R1 - R0;
    CC = R3 < R2 (IU);
    IF CC JUMP .Lcopy_bytes;
    R3 = R0 ^ R1;
    CC = BITTST (R3, 0);
    IF CC JUMP .Lcopy_bytes;
    CC = BITTST (R3, 1);
    IF CC JUMP .Lcopy_halves;
    R3 = P0;
    CC = BITTST (R3, 0);
    IF !CC JUMP .Lcopy_half;
    R3 = B[P1++] (Z);
    B[P0++] = R3;
    R2 += -1;
    R3 = P0;
.Lcopy_half:
    CC = BITTST (R3, 1);
    IF !CC JUMP .Lcopy_words;
    R3 = W[P1++] (Z);
    W[P0++] = R3;
    R2 += -2;
.Lcopy_words:
    R3 = R2 >> 2;
    P2 = R3;
    LSETUP (.Lcopy_word, .Lcopy_word_end) LC0 = P2;
.Lcopy_word:
    R3 = [P1++];
.Lcopy_word_end:
    [P0++] = R3;
    CC = BITTST (R2, 1);
    IF !CC JUMP .Lcopy_last;
    R3 = W[P1++] (Z);
    W[P0++] = R3;
.Lcopy_last:
    CC = BITTST (R2, 0);
    IF !CC JUMP .Lcopy_done;
    R3 = B[P1] (Z);
    B[P0] = R3;
.Lcopy_done:
    RTS;
.Lcopy_halves:
    R3 = P0;
    CC = BITTST (R3, 0);
    IF !CC JUMP .Lcopy_half_loop;
    R3 = B[P1++] (Z);
    B[P0++] = R3;
    R2 += -1;
.Lcopy_half_loop:
    R3 = R2 >> 1;
    P2 = R3;
    LSETUP (.Lcopy_h, .Lcopy_h_end) LC0 = P2;
.Lcopy_h:
    R3 = W[P1++] (Z);
.Lcopy_h_end:
    W[P0++] = R3;
    JUMP.S .Lcopy_last;
.Lcopy_bytes:
    CC = R2 == 0;
    IF CC JUMP .Lcopy_done;
    P2 = R2;
    LSETUP (.Lcopy_b, .Lcopy_b_end) LC0 = P2;
.Lcopy_b:
    R3 = B[P1++] (X);
.Lcopy_b_end:
    B[P0++] = R3;
    RTS;
    .size _memcpy, .-_memcpy
    .popsection
)");
