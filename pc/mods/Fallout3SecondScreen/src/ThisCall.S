    .text
    .globl _Fo3dsCallThisFloat
_Fo3dsCallThisFloat:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisUInt1
_Fo3dsCallThisUInt1:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisPointer
_Fo3dsCallThisPointer:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisPointer1
_Fo3dsCallThisPointer1:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisUIntNoArgs
_Fo3dsCallThisUIntNoArgs:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    call *8(%ebp)
    leave
    ret

.globl _Fo3dsCallThisVoidNoArgs
_Fo3dsCallThisVoidNoArgs:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    call *8(%ebp)
    leave
  ret

.globl _Fo3dsCallThisVoid1
_Fo3dsCallThisVoid1:
  movl 4(%esp), %eax
  movl 8(%esp), %ecx
  pushl 12(%esp)
  call *%eax
  ret

    .globl _Fo3dsCallThisFree
_Fo3dsCallThisFree:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallCdeclVoid3
_Fo3dsCallCdeclVoid3:
    pushl %ebp
    movl %esp, %ebp
    pushl 20(%ebp)
    pushl 16(%ebp)
    pushl 12(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisPointer2
_Fo3dsCallThisPointer2:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 20(%ebp)
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisPointer3
_Fo3dsCallThisPointer3:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 24(%ebp)
    pushl 20(%ebp)
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    .globl _Fo3dsCallThisVoid3
_Fo3dsCallThisVoid3:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 24(%ebp)
    pushl 20(%ebp)
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret

    # Calls the six-argument native Actor equip/unequip methods.  The target
    # owns its six stack arguments (ret $0x18); this wrapper supplies ECX and
    # preserves a normal cdecl boundary for the C++ caller.
    .globl _Fo3dsCallThisAction
_Fo3dsCallThisAction:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 36(%ebp)
    pushl 32(%ebp)
    pushl 28(%ebp)
    pushl 24(%ebp)
    pushl 20(%ebp)
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret
