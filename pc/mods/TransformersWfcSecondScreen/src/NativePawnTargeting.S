.text
.globl _WfcPawnAimGate
_WfcPawnAimGate:
  pushfl
  cmpl _WfcTargetPlayer,%ecx
  je 1f
  popfl
  jmp *_WfcPawnAimOriginal
1:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  call _WfcResolvePawnAim
  testl %eax,%eax
  jz 2f
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  // Identical ECX and output/W/StartFireLoc stack ABI, RET12. This body
  // retains the live controller's virtual aim and pawn base-aim fallback.
  jmp *_WfcPawnBaseAim
2:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  movl 4(%esp),%eax
  movl $0,(%eax)
  movl $0,4(%eax)
  movl $0,8(%eax)
  ret $12

.globl _WfcPawnSameTargetGate
_WfcPawnSameTargetGate:
  // Already past the native network/None gates. Both participants matter:
  // another AI may query the pawn which no longer has an AI controller.
  pushfl
  cmpl _WfcTargetPlayer,%esi
  je 3f
  cmpl _WfcTargetPlayer,%ecx
  je 3f
  popfl
  jmp *_WfcPawnSameTargetOriginal
3:
  popfl
  jmp *_WfcPawnSameTargetFalse

.globl _WfcPawnLocationGate
_WfcPawnLocationGate:
  pushfl
  cmpl _WfcTargetPlayer,%ecx
  je 4f
  popfl
  jmp *_WfcPawnLocationOriginal
4:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  call _WfcResolvePawnLocation
  testl %eax,%eax
  jz 5f
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  // Compiled-to-native ABI conversion, as in the shipping Actor bridge:
  // receiver and RequestedBy move from compiled +10/None10 to raw/None0.
  leal -16(%ecx),%ecx
  movl 8(%esp),%edx
  leal -16(%edx),%edx
  movl %edx,8(%esp)
  jmp *_WfcPawnActorLocation
5:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  movl 4(%esp),%eax
  movl $0,(%eax)
  movl $0,4(%eax)
  movl $0,8(%eax)
  ret $12
