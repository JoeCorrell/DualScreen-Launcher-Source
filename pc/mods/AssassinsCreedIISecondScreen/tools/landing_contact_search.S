/* Exact-build AC II contact-search correction. ECX = array header,
 * stack = minimum/maximum slope in degrees; callee removes 8 argument bytes.
 * A private header advances through existing records; contacts are never moved.
 * Original query/filter execute on the calling game thread.
 */
.text
.globl _LandingContactSearch
_LandingContactSearch:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    subl $8, %esp
    movl (%ecx), %eax
    movl %eax, -20(%ebp)
    movl 4(%ecx), %eax
    movl %eax, -16(%ebp)
search:
    leal -20(%ebp), %ecx
    pushl 12(%ebp)
    pushl 8(%ebp)
    .byte 0xe8
    .long 0x005c5900 - (0x0256e000 + (. - _LandingContactSearch) + 4)
    testl %eax, %eax
    jz done
    movl %eax, %edi
    pushl %eax
    .byte 0xe8
    .long 0x018fe9b0 - (0x0256e000 + (. - _LandingContactSearch) + 4)
    addl $4, %esp
    testb %al, %al
    jz accepted
    subl -20(%ebp), %edi
    shrl $6, %edi
    incl %edi
    movzwl -14(%ebp), %eax
    andl $0x3fff, %eax
    subl %edi, %eax
    jz done
    movw %ax, -14(%ebp)
    shll $6, %edi
    addl %edi, -20(%ebp)
    jmp search
accepted:
    movl %edi, %eax
done:
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret $8
