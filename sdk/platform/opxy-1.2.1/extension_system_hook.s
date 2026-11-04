/* The System descriptor-vector copy at 8c098f32 executes inside a stock
 * constructor with a 252-byte stack frame.  An ordinary C++ adapter adds at
 * least another 48 bytes before the stock vector copy's own frame; the generic
 * HOOK_SAVE frame is larger still.  Populate the three fixed SDK rows here,
 * with caller-clobbered registers only, then call the stock copier directly.
 *
 * The generated hook table contains a link-time pointer to every selected
 * extension's Hooks object.  Hooks::system is at +28 in the 32-bit ABI.  This
 * path has no C++ call or dynamic allocation.  It copies exactly six stock
 * descriptors, accepts at most three non-null SystemEntry pointers, and then
 * delegates vector ownership to stock as before.
 *
 * The 12-byte patch also covers stock's P1 global load.  Replay it after the
 * stock copier returns, before returning to the next complete instruction. */
.section .text
.align 2
/* Plain C data names have the Blackfin toolchain's leading underscore. */
.extern _sdk_extension_system_rows
.extern _sdk_extension_system_values
.extern _sdk_extension_system_installed
.extern _sdk_extension_system_destination
.extern _sdk_extension_system_saved_p3
.extern _sdk_extension_hooks_static
.extern _sdk_extension_hooks_static_count

.global sdk_extensions_system_entries_hook
.type sdk_extensions_system_entries_hook, STT_FUNC
sdk_extensions_system_entries_hook:
    R3 = 6;
    CC = R2 == R3;
    IF !CC JUMP .Lsystem_stock;

    /* P3 is preserved by the replaced stock call.  Keep it outside the
     * constrained stock frame while this adapter uses it as a scratch
     * pointer.  P0-P2 and R0-R3 are caller-clobbered at this call site. */
    P0.H = _sdk_extension_system_saved_p3;
    P0.L = _sdk_extension_system_saved_p3;
    [P0] = P3;

    /* Preserve the destination in module RAM while P0-P3 walk the sources. */
    P3.H = _sdk_extension_system_destination;
    P3.L = _sdk_extension_system_destination;
    [P3] = R0;

    P0 = R1;
    P1.H = _sdk_extension_system_rows;
    P1.L = _sdk_extension_system_rows;
    R3 = 6;
.Lsystem_copy_stock:
    R0 = [P0++]; [P1++] = R0;
    R0 = [P0++]; [P1++] = R0;
    R0 = [P0++]; [P1++] = R0;
    R0 = [P0++]; [P1++] = R0;
    R3 += -1;
    CC = R3 == 0;
    IF !CC JUMP .Lsystem_copy_stock;

    P0.H = _sdk_extension_hooks_static_count;
    P0.L = _sdk_extension_hooks_static_count;
    R2 = [P0];
    P0.H = _sdk_extension_hooks_static;
    P0.L = _sdk_extension_hooks_static;
    P1.H = _sdk_extension_system_rows;
    P1.L = _sdk_extension_system_rows;
    P1 += 24;
    P1 += 24;
    P1 += 24;
    P1 += 24;
    P2.H = _sdk_extension_system_values;
    P2.L = _sdk_extension_system_values;
    R3 = 0;
.Lsystem_entries:
    CC = R2 == 0;
    IF CC JUMP .Lsystem_ready;
    R0 = [P0++];
    R2 += -1;
    CC = R0 == 0;
    IF CC JUMP .Lsystem_entries;
    P3 = R0;
    R0 = [P3 + 28];
    CC = R0 == 0;
    IF CC JUMP .Lsystem_entries;
    P3 = R0;
    R1 = [P3];
    CC = R1 == 0;
    IF CC JUMP .Lsystem_entries;
    R0 = [P3 + 4];
    CC = R0 == 0;
    IF CC JUMP .Lsystem_entries;
    [P1] = R1;
    [P2 + 8] = R1;
    P1 += 16;
    P2 += 24;
    R3 += 1;
    CC = R3 == 3;
    IF !CC JUMP .Lsystem_entries;

.Lsystem_ready:
    P0.H = _sdk_extension_system_installed;
    P0.L = _sdk_extension_system_installed;
    [P0] = R3;
    P0.H = _sdk_extension_system_rows;
    P0.L = _sdk_extension_system_rows;
    R1 = P0;
    P0.H = _sdk_extension_system_destination;
    P0.L = _sdk_extension_system_destination;
    R0 = [P0];
    R2 = R3;
    R2 += 6;
    P0.H = 0x8c09;
    P0.L = 0x7f0c;
    CALL (P0);
    P0.H = _sdk_extension_system_saved_p3;
    P0.L = _sdk_extension_system_saved_p3;
    P3 = [P0];
    P1.H = 0x8400;
    P1.L = 0xc0ec;
    RTS;
.Lsystem_stock:
    P0.H = 0x8c09;
    P0.L = 0x7f0c;
    CALL (P0);
    P1.H = 0x8400;
    P1.L = 0xc0ec;
    RTS;
.size sdk_extensions_system_entries_hook, .-sdk_extensions_system_entries_hook
