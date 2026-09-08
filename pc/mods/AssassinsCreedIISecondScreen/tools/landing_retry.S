/* Exact-build late landing retry, entered instead of the call to 01083410
 * at 010853b3. Complete the original update first, then retry only the current
 * player's unchanged free-fall context with contacts, an expired timer and no
 * pending transition/target. The original dispatcher consumes the resulting
 * flags. Never write position, animation IDs, contact records or grounded state.
 */
.text
.globl _LandingRetry
_LandingRetry:
    pushl %esi
    movl %ecx, %esi
    .byte 0xe8
    .long 0x01083410 - (0x0256e100 + (. - _LandingRetry) + 4)
    pushfl
    pushal
    /* The added absolute layout checks apply only at the verified image base.
     * A relocated image still completes its original update, without retrying. */
    call location
location:
    popl %eax
    cmpl $(0x0256e100 + (location - _LandingRetry)), %eax
    jne done
    /* Resolve the current player after the original update. Do not dereference
     * the saved context until its ownership has been re-established. */
    movl 0x02212490, %eax
    testl %eax, %eax
    jz done
    movl 0x02212570, %edx
    testl %edx, %edx
    jz done
    cmpl $-1, 0xd0(%edx)
    je session_world
    movl 0xc4(%edx), %edx
    testl %edx, %edx
    jz done
    testl $0x80000000, 8(%edx)
    jz done
    movl (%edx), %eax
    jmp world
session_world:
    movl 0x4c(%eax), %eax
world:
    testl %eax, %eax
    jz done
    movl 0x53c(%eax), %eax
    testl %eax, %eax
    jz done
    movl 4(%eax), %eax
    testl %eax, %eax
    jz done
    movl 0x02214d00, %edx
    testl %edx, %edx
    jz done
    movl 0x24(%edx), %edx
    cmpl $4, %edx
    jae done
    movl (%eax,%edx,4), %edi
    testl %edi, %edi
    jz done
    movzwl 0x7a(%edi), %ecx
    andl $0x3fff, %ecx
    testl %ecx, %ecx
    jz done
    cmpl $256, %ecx
    ja done
    movl 0x74(%edi), %edx
    testl %edx, %edx
    jz done
component:
    movl (%edx), %eax
    testl %eax, %eax
    jz next_component
    cmpl $0x01e9cd48, (%eax)
    je controller
next_component:
    addl $4, %edx
    decl %ecx
    jnz component
    jmp done
controller:
    cmpl %edi, 8(%eax)
    jne done
    movl 0x18(%eax), %ebx
    testl %ebx, %ebx
    jz done
    cmpl $0x01e7c080, (%ebx)
    jne done
    cmpl %esi, 0x48(%ebx)
    jne done
    cmpl %edi, 0x114(%ebx)
    jne done
    cmpl $0x01e55bb8, (%esi)
    jne done
    cmpl %ebx, 8(%esi)
    jne done
    cmpb $1, 0x362(%esi)
    jne done
    cmpw $0, 0x180(%esi)
    jne done
    /* Bit 04 is latched by 01070d7b/01075320 and suppresses another geometry
     * attempt at 01074ed8. It is not a dispatcher exit request: 010853a0
     * consumes bit 40 here, not bit 04. Blocking on 04 prevents recovery
     * with the observed 0c940000 state. Keep the other mode/exit guards. */
    testb $0x63, 0x182(%esi)
    jnz done
    movl 0x140(%ebx), %eax
    testl %eax, %eax
    jz done
    cmpl $0x01e7b884, (%eax)
    jne done
    addl $0x3e0, %eax
    cmpl %eax, 0x10(%esi)
    jne done
    cmpl $0x01e7b34c, (%eax)
    jne done
    cmpl $1, 0x1f8(%eax)
    jne done
    cmpl $0, 0x3b0(%eax)
    jne done
    cmpb $0, 0x220(%eax)
    je done
    movl 0x218(%eax), %edx
    movl 0x21c(%eax), %ecx
    movl %edx, %ebp
    orl %ecx, %ebp
    jnz clock_ready
    movl 0x224(%eax), %ebp
    testl %ebp, %ebp
    jz done
    movl (%ebp), %edx
    movl 4(%ebp), %ecx
clock_ready:
    cmpl 0x214(%eax), %ecx
    jb done
    ja contacts
    cmpl 0x210(%eax), %edx
    jb done
contacts:
    movl 0xfc(%ebx), %eax
    leal 0x110(%ebx), %edx
    cmpl %eax, %edx
    jne done
    movb 0xf(%eax), %dl
    cmpb 0xf4(%edi), %dl
    jne done
    movzbl 0x10(%eax), %edx
    cmpl $0xfe, %edx
    jae done
    movzwl 0x7a(%edi), %ecx
    andl $0x3fff, %ecx
    cmpl %ecx, %edx
    jae done
    movl 0x74(%edi), %eax
    movl (%eax,%edx,4), %eax
    testl %eax, %eax
    jz done
    cmpl $0x01cde3c8, (%eax)
    jne done
    cmpl %edi, 8(%eax)
    jne done
    testw $0x3fff, 0x1a(%eax)
    jz done
    cmpl $0, 0x14(%eax)
    je done
    /* Preserve the original update's floating-point register/control state.
     * The retry uses the exact stock arguments and callee-cleanup convention. */
    movl %esp, %ebp
    andl $-16, %esp
    subl $512, %esp
    fxsave (%esp)
    pushl $0
    pushl $1
    pushl $1
    movl %esi, %ecx
    .byte 0xe8
    .long 0x0107a250 - (0x0256e100 + (. - _LandingRetry) + 4)
    fxrstor (%esp)
    movl %ebp, %esp
done:
    popal
    popfl
    popl %esi
    ret
