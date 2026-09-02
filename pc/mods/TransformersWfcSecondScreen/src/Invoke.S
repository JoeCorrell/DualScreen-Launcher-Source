    .text
    .globl _WfcInvokeReflected
_WfcInvokeReflected:
    pushl %ebp
    movl %esp, %ebp
    movl 12(%ebp), %eax
    movl 8(%ebp), %edx
    pushl 20(%ebp)
    pushl 16(%ebp)
    call *%edx
    leave
    ret
