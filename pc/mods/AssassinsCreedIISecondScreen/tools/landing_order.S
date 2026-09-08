/* Exact-build landing-order candidate. Resolve a genuine native landing before
 * the airborne update can consume/replace transient contact data. If no landing
 * is requested, run the full original update and retain a guarded late check.
 * All native calls run on the original update thread. No state IDs, transforms,
 * contact records, or physics velocities are overwritten by this shim.
 */
.text
.globl _LandingOrder
_LandingOrder:
    pushfl
    pushal
    movl %ecx, %esi
    call check_landing
    testl %eax, %eax
    jnz landed_before_update
    popal
    popfl
    pushl %esi
    movl %ecx, %esi
    .byte 0xe8
    .long 0x01083410 - (0x0256e100 + (. - _LandingOrder) + 4)
    pushfl
    pushal
    call check_landing
    popal
    popfl
    popl %esi
    ret
landed_before_update:
    popal
    popfl
    ret
check_landing:
    pushl %ebx
    pushl %edi
    pushl %ebp
    /* The added absolute layout checks apply only at the verified image base.
     * A relocated image still completes its original update, without retrying. */
    call location
location:
    popl %eax
    cmpl $(0x0256e100 + (location - _LandingOrder)), %eax
    jne rejected
    /* Resolve the current player after the original update. Do not dereference
     * the saved context until its ownership has been re-established. */
    movl 0x02212490, %eax
    testl %eax, %eax
    jz rejected
    movl 0x02212570, %edx
    testl %edx, %edx
    jz rejected
    cmpl $-1, 0xd0(%edx)
    je session_world
    movl 0xc4(%edx), %edx
    testl %edx, %edx
    jz rejected
    testl $0x80000000, 8(%edx)
    jz rejected
    movl (%edx), %eax
    jmp world
session_world:
    movl 0x4c(%eax), %eax
world:
    testl %eax, %eax
    jz rejected
    movl 0x53c(%eax), %eax
    testl %eax, %eax
    jz rejected
    movl 4(%eax), %eax
    testl %eax, %eax
    jz rejected
    movl 0x02214d00, %edx
    testl %edx, %edx
    jz rejected
    movl 0x24(%edx), %edx
    cmpl $4, %edx
    jae rejected
    movl (%eax,%edx,4), %edi
    testl %edi, %edi
    jz rejected
    movzwl 0x7a(%edi), %ecx
    andl $0x3fff, %ecx
    testl %ecx, %ecx
    jz rejected
    cmpl $256, %ecx
    ja rejected
    movl 0x74(%edi), %edx
    testl %edx, %edx
    jz rejected
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
    jmp rejected
controller:
    cmpl %edi, 8(%eax)
    jne rejected
    movl 0x18(%eax), %ebx
    testl %ebx, %ebx
    jz rejected
    cmpl $0x01e7c080, (%ebx)
    jne rejected
    cmpl %esi, 0x48(%ebx)
    jne rejected
    cmpl %edi, 0x114(%ebx)
    jne rejected
    cmpl $0x01e55bb8, (%esi)
    jne rejected
    cmpl %ebx, 8(%esi)
    jne rejected
    cmpb $1, 0x362(%esi)
    jne rejected
    cmpw $0, 0x180(%esi)
    jne rejected
    /* Bit 04 is latched by 01070d7b/01075320 and suppresses another geometry
     * attempt at 01074ed8. It is not a dispatcher exit request: 010853a0
     * consumes bit 40 here, not bit 04. Blocking on 04 prevents recovery
     * with the observed 0c940000 state. Keep the other mode/exit guards. */
    testb $0x63, 0x182(%esi)
    jnz rejected
    movl 0x140(%ebx), %eax
    testl %eax, %eax
    jz rejected
    cmpl $0x01e7b884, (%eax)
    jne rejected
    addl $0x3e0, %eax
    cmpl %eax, 0x10(%esi)
    jne rejected
    cmpl $0x01e7b34c, (%eax)
    jne rejected
    cmpl $1, 0x1f8(%eax)
    jne rejected
    cmpl $0, 0x3b0(%eax)
    jne rejected
    cmpb $0, 0x220(%eax)
    je rejected
    movl 0x218(%eax), %edx
    movl 0x21c(%eax), %ecx
    movl %edx, %ebp
    orl %ecx, %ebp
    jnz clock_ready
    movl 0x224(%eax), %ebp
    testl %ebp, %ebp
    jz rejected
    movl (%ebp), %edx
    movl 4(%ebp), %ecx
clock_ready:
    cmpl 0x214(%eax), %ecx
    jb rejected
    ja contacts
    cmpl 0x210(%eax), %edx
    jb rejected
contacts:
    movl 0xfc(%ebx), %ecx
    leal 0x110(%ebx), %edx
    cmpl %ecx, %edx
    jne rejected
    /* Native resolution refreshes stale index/generation exactly as the stock
     * classifier does. Preserve FP state across the resolver AND classifier. */
    movl %esp, %ebp
    andl $-16, %esp
    subl $512, %esp
    fxsave (%esp)
    .byte 0xe8
    .long 0x005eb010 - (0x0256e100 + (. - _LandingOrder) + 4)
    testl %eax, %eax
    jz native_rejected
    cmpl $0x01cde3c8, (%eax)
    jne native_rejected
    cmpl %edi, 8(%eax)
    jne native_rejected
    testw $0x3fff, 0x1a(%eax)
    jz native_rejected
    cmpl $0, 0x14(%eax)
    je native_rejected
    pushl $0
    pushl $1
    pushl $1
    movl %esi, %ecx
    .byte 0xe8
    .long 0x0107a250 - (0x0256e100 + (. - _LandingOrder) + 4)
    /* Only a native pending transition justifies skipping the original update.
     * A true return alone is insufficient (some other traversal producers use
     * true to mean handled, without requesting a context replacement). */
    xorl %eax, %eax
    cmpw $0, 0x180(%esi)
    setne %al
    jmp native_done
native_rejected:
    xorl %eax, %eax
native_done:
    fxrstor (%esp)
    movl %ebp, %esp
    popl %ebp
    popl %edi
    popl %ebx
    ret
rejected:
    xorl %eax, %eax
    popl %ebp
    popl %edi
    popl %ebx
    ret
