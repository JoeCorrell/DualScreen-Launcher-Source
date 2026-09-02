    .text
    .globl _WfcInvokeReflected
_WfcInvokeReflected:
    movl 8(%esp), %eax
    movl 4(%esp), %edx
    pushl 16(%esp)
    pushl 16(%esp)
    call *%edx
    ret
