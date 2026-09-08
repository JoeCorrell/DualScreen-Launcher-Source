.text
.globl _WfcGameDeathCast
_WfcGameDeathCast:
  // Scoped TnGame.Killed class decision. ECX=raw victim, stack raw class.
  // Preserve the compiled caller's complete FP/register state around policy.
  pushfl
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  call _WfcResolveGameDeath
  cmpl $-1,%eax
  je 1f
  movl %eax,28(%ebx)
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  ret $4
1:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  jmp *_WfcGameDeathOriginalCast
