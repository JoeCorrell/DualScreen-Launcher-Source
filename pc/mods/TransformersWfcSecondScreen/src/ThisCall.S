    .text
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

    .globl _WfcAssignFString
_WfcAssignFString:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %ecx
    pushl 16(%ebp)
    call *8(%ebp)
    leave
    ret
