/* Frame for a hook entered from inside a stock function, where no register is
 * free for the compiled C++ it calls (see hook.s). Between HOOK_SAVE and
 * HOOK_RESTORE a stub calls C++ with the usual 12-byte argument area;
 * HOOK_RESULT replaces the saved R0, so HOOK_RESTORE returns it; HOOK_RESULT_R3
 * replaces the saved R3 instead. Every other register, ASTAT (CC) included, is
 * restored as the stub found it.
 */
.extern sdk_hook_save
.extern sdk_hook_restore

.macro HOOK_SAVE
    LINK 0;
    CALL sdk_hook_save;
.endm

.macro HOOK_RESULT source
    [SP + 12] = \source;
.endm

.macro HOOK_RESULT_R3 source
    [SP + 24] = \source;
.endm

.macro HOOK_RESTORE
    CALL sdk_hook_restore;
    UNLINK;
.endm
