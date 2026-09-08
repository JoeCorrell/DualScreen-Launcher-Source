.text
// cdecl(entry, compiled weapon, output). Preserve host state; canary every
// callee-saved GPR and x87/SSE register around the actual shipping fire body.
.globl _ExposureTestCall
_ExposureTestCall:
  pushl %ebp
  movl %esp,%ebp
  pushl %ebx
  pushl %esi
  pushl %edi
  subl $544,%esp
  andl $-16,%esp
  fxsave (%esp)
  movl %esp,-16(%ebp)
  fninit
  fld1
  pcmpeqd %xmm0,%xmm0
  pcmpeqd %xmm1,%xmm1
  pcmpeqd %xmm2,%xmm2
  pcmpeqd %xmm3,%xmm3
  pcmpeqd %xmm4,%xmm4
  pcmpeqd %xmm5,%xmm5
  pcmpeqd %xmm6,%xmm6
  pcmpeqd %xmm7,%xmm7
  movl $0x11223344,%ebx
  movl $0x33445566,%edx
  movl $0x44556677,%esi
  movl $0x55667788,%edi
  movl 12(%ebp),%ecx
  call *8(%ebp)
  pushfl
  pushal
  movl 16(%ebp),%edi
  movl %esp,%esi
  movl $9,%ecx
  rep movsl
  movl 16(%ebp),%edi
  fxsave 48(%edi)
  addl $36,%esp
  movl -16(%ebp),%esp
  fxrstor (%esp)
  leal -12(%ebp),%esp
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
