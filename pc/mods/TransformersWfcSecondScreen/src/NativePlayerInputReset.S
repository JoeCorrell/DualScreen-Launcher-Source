    .text
    # ResetPlayerMovementInput uses ESI script-this and has no stack args.
    # Preserve ALL incoming registers/flags and x87/MMX/SSE state around the
    # bookkeeping observer, then tail-jump into the original native prologue.
    # Never skip or synthesize the game's reset.
    .globl _WfcHookInputReset
_WfcHookInputReset:
    pushfl
    pushal
    movl %esp, %ebx
    subl $528, %esp
    andl $-16, %esp
    fxsave (%esp)
    subl $16, %esp
    movl %esi, (%esp)
    call _WfcObserveInputReset
    addl $16, %esp
    fxrstor (%esp)
    movl %ebx, %esp
    popal
    popfl
    jmp *_WfcInputResetOriginal

    # The unchanged ESI/no-stack-argument caller is shared in ThisCall.S.
