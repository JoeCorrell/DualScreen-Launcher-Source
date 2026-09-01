    .text

    .globl _GddsCallThisUIntNoArgs
_GddsCallThisUIntNoArgs:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    call *%eax
    ret

    .globl _GddsCallThisBoolNoArgs
_GddsCallThisBoolNoArgs:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    call *%eax
    movzbl %al, %eax
    ret

    .globl _GddsCallThisPointerNoArgs
_GddsCallThisPointerNoArgs:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    call *%eax
    ret

    .globl _GddsCallThisFloatNoArgs
_GddsCallThisFloatNoArgs:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    call *%eax
    ret

    .globl _GddsCallThisDoubleNoArgs
_GddsCallThisDoubleNoArgs:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    call *%eax
    ret

    .globl _GddsCallThisFloat1
_GddsCallThisFloat1:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    movl 12(%esp), %edx
    pushl %edx
    call *%eax
    ret

    .globl _GddsCallThisVoid1
_GddsCallThisVoid1:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    movl 12(%esp), %edx
    pushl %edx
    call *%eax
    ret

    .globl _GddsCallThisWorldVec
_GddsCallThisWorldVec:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    movl 12(%esp), %edx
    pushl %edx
    call *%eax
    ret

    .globl _GddsCallThisVoidNoArgs
_GddsCallThisVoidNoArgs:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    call *%eax
    ret

    .globl _GddsCallThisPointer1
_GddsCallThisPointer1:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 12(%esp)
    call *%eax
    ret

    .globl _GddsCallThisPointer2
_GddsCallThisPointer2:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 16(%esp)
    pushl 16(%esp)
    call *%eax
    ret

    .globl _GddsCallThisPointer5
_GddsCallThisPointer5:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 28(%esp)
    pushl 28(%esp)
    pushl 28(%esp)
    pushl 28(%esp)
    pushl 28(%esp)
    call *%eax
    ret

    .globl _GddsCallThisVoid2
_GddsCallThisVoid2:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 16(%esp)
    pushl 16(%esp)
    call *%eax
    ret

    .globl _GddsCallThisVoid3
_GddsCallThisVoid3:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 20(%esp)
    pushl 20(%esp)
    pushl 20(%esp)
    call *%eax
    ret

    .globl _GddsCallThisVoid4
_GddsCallThisVoid4:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 24(%esp)
    pushl 24(%esp)
    pushl 24(%esp)
    pushl 24(%esp)
    call *%eax
    ret

    .globl _GddsCallThisFrustum
_GddsCallThisFrustum:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pushl 16(%esp)
    pushl 16(%esp)
    call *%eax
    ret
