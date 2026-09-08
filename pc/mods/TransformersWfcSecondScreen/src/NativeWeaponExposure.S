.text
.globl _WfcWeaponExposureGate
_WfcWeaponExposureGate:
  // ECX=weapon+10; the stock entry already saved ECX/ESI/EDI and checked
  // network eligibility. No pawn dereference in the unmapped hot path.
  pushfl
  movl 0x84(%ecx),%esi
  testl %esi,%esi
  jz 1f
  cmpl _WfcExposurePlayerPawn,%esi
  je 2f
  cmpl _WfcExposureAiPawn,%esi
  je 2f
1:
  popfl
  jmp *_WfcExposureOriginal
2:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %esi,(%esp)
  call _WfcResolveWeaponExposure
  // Select continuation without leaking C++ register/FP/flag clobbers.
  testl %eax,%eax
  jz 3f
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  leal 0x84(%ecx),%edi
  // Native base TnPawn cast + native ExposeSelf, not a reflected call.
  jmp *_WfcExposureContinue
3:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  jmp *_WfcExposureReturn

.globl _WfcWeaponPickupCast
_WfcWeaponPickupCast:
  pushfl
  cmpl _WfcExposurePlayerPawn,%ecx
  je 4f
  cmpl _WfcExposureAiPawn,%ecx
  je 4f
  popfl
  jmp *_WfcPickupCastOriginal
4:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  call _WfcResolveWeaponExposure
  movl %eax,28(%ebx)
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  ret $4
