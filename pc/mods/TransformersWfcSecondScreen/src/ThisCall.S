    .text
    # Compiled GotoState helper: ESI=UObject, EDI=force(false), and three stack
    # words (state, label=None, keep-stack=false), RET12. Preserve host ESI/EDI.
    .globl _WfcCallObjectGotoState
_WfcCallObjectGotoState:
    pushl %ebp
    movl %esp, %ebp
    pushl %esi
    pushl %edi
    movl 12(%ebp), %esi
    xorl %edi, %edi
    pushl $0
    pushl $0
    pushl 16(%ebp)
    call *8(%ebp)
    popl %edi
    popl %esi
    leave
    ret

    # TnAiWeaponAimer.Initialize: EDI script aimer, EAX script pawn, plain RET.
    .globl _WfcCallEdiEax
_WfcCallEdiEax:
    pushl %ebp
    movl %esp, %ebp
    pushl %edi
    movl 12(%ebp), %edi
    movl 16(%ebp), %eax
    call *8(%ebp)
    popl %edi
    leave
    ret

    # Player acknowledgment: EDI is script-this, one compiled pawn on stack,
    # callee RET4. Keep native caller registers and ordinary C call semantics.
    .globl _WfcCallEdiObject
_WfcCallEdiObject:
    pushl %ebp
    movl %esp, %ebp
    pushl %edi
    movl 12(%ebp), %edi
    pushl 16(%ebp)
    call *8(%ebp)
    popl %edi
    leave
    ret

    # Private compiled GOAP setters: ESI is script-this, one compiled object
    # on the stack (including 0x10 for None), callee RET 4. Preserve the host
    # ESI even if the optimized native method treats its receiver as scratch.
    .globl _WfcCallEsiObject
    .globl _WfcCallEsiValue
_WfcCallEsiValue:
_WfcCallEsiObject:
    pushl %ebp
    movl %esp, %ebp
    pushl %esi
    movl 12(%ebp), %esi
    pushl 16(%ebp)
    call *8(%ebp)
    popl %esi
    leave
    ret

    .globl _WfcCallThisTick
_WfcCallThisTick:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _WfcFindFunction
_WfcFindFunction:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 20(%ebp)
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    # Shared native ESI/no-stack-argument caller. Reused by input reset and
    # vehicle strategy StopMovement; preserve the original input-reset ABI.
    .globl _WfcCallEsiVoid
_WfcCallEsiVoid:
    pushl %ebp
    movl %esp, %ebp
    pushl %esi
    movl 12(%ebp), %esi
    call *8(%ebp)
    popl %esi
    leave
    ret

    # WFC private vector setter: EAX is compiled script-this, ECX points to
    # the input vector; no stack arguments, ordinary caller-saved registers.
    .globl _WfcCallEaxEcx
_WfcCallEaxEcx:
    movl 8(%esp), %eax
    movl 12(%esp), %ecx
    call *4(%esp)
    ret

    # Real PlayerController.SetViewTarget helper: EAX=transition pointer,
    # ECX=new raw actor, stack=raw player controller, callee RET4. The receiver
    # and target differ during a swap; neither uses script-this +0x10 here.
    .globl _WfcCallPlayerViewTarget
_WfcCallPlayerViewTarget:
    pushl %ebp
    movl %esp, %ebp
    movl 20(%ebp), %eax
    movl 16(%ebp), %ecx
    pushl 12(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _WfcAssignFString
_WfcAssignFString:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret
