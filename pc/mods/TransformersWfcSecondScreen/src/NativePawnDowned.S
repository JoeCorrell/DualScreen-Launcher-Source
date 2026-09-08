.text
// Full player and AI client bodies retain their different notification order.
// No callback is run through a reflected UFunction executor.
.macro DOWNED_ENTRY name,identity,original,converted,role
.globl \name
\name:
  pushfl
  cmpl \identity,%ecx
  je 1f
  popfl
  jmp *\original
1:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  movl $\role,4(%esp)
  call _WfcResolveDownedController
  testl %eax,%eax
  jz 2f
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  jmp *\converted
2:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  ret
.endm
DOWNED_ENTRY _WfcDownedAiStart,_WfcDownedPlayerPawn,_WfcDownedAiStartOriginal,_WfcDownedPlayerStartOriginal,0
DOWNED_ENTRY _WfcDownedAiStop,_WfcDownedPlayerPawn,_WfcDownedAiStopOriginal,_WfcDownedPlayerStopOriginal,0
DOWNED_ENTRY _WfcDownedPlayerStart,_WfcDownedAiPawn,_WfcDownedPlayerStartOriginal,_WfcDownedAiStartOriginal,1
DOWNED_ENTRY _WfcDownedPlayerStop,_WfcDownedAiPawn,_WfcDownedPlayerStopOriginal,_WfcDownedAiStopOriginal,1

// Replace only the three AI-tail controller reads. The former physical player
// has no BoundController field; do not read or write its sibling-only tail.
// Validate again after base/state/dialog callbacks before resuming native code.
.macro DOWNED_BOUND name,original,continuation,saveoffset,start,drop=0
.globl \name
\name:
  pushfl
  cmpl _WfcDownedAiPawn,%esi
  je 3f
  popfl
  jmp *\original
3:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %esi,(%esp)
  movl $1,4(%esp)
  call _WfcResolveDownedController
  .if \start
  testl %eax,%eax
  jz 4f
  .endif
  movl %eax,\saveoffset(%ebx)
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  jmp *\continuation
  .if \start
4:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  .if \drop
  // The second start read occurs before the caller removes its two class-
  // lookup arguments. Its failure epilogue must remove them as well.
  addl $\drop,%esp
  .endif
  jmp *_WfcDownedAiStartReturn
  .endif
.endm
DOWNED_BOUND _WfcDownedBoundStart,_WfcDownedBoundStartOriginal,_WfcDownedBoundStartContinue,28,1
DOWNED_BOUND _WfcDownedBoundRevive,_WfcDownedBoundReviveOriginal,_WfcDownedBoundReviveContinue,24,1,8
DOWNED_BOUND _WfcDownedBoundStop,_WfcDownedBoundStopOriginal,_WfcDownedBoundStopContinue,4,0
