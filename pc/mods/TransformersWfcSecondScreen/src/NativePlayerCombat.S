.text
.macro COMBAT_CAST name,fire,melee=0
.globl \name
\name:
  pushfl
  cmpl _WfcCombatPlayerPawn,%ecx
  je 1f
  cmpl _WfcCombatAiPawn,%ecx
  je 1f
  popfl
  jmp *_WfcCombatOriginalCast
1:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  movl $\melee,4(%esp)
  call _WfcResolvePlayerCombat
  movl %eax,28(%ebx)
  .if \fire
  testl %eax,%eax
  jz 2f
  .endif
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  ret $4
  .if \fire
2:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  // Discard this IsA CALL's return address and its class argument. EAX=0.
  // Each rejection target restores its own native locals/iterator state.
  // A normal false cast would permit firing or skip a teleport observer.
  addl $8,%esp
  .if \fire == 2
  // A rejected player observer must abort catch-up teleport, not skip that
  // observer and continue toward teleport. Native cleanup destroys iterator.
  jmp *_WfcNavigateReject
  .else
  jmp *_WfcCombatFireReject
  .endif
  .endif
.endm
COMBAT_CAST _WfcCombatFireCast,1
COMBAT_CAST _WfcCombatAbortCast,0
COMBAT_CAST _WfcCombatMeleeCast,0,1
COMBAT_CAST _WfcCombatStickCast,0
COMBAT_CAST _WfcCombatRecordCast,0
COMBAT_CAST _WfcCombatSnipingCast,0
COMBAT_CAST _WfcCombatFineHitCast,0
COMBAT_CAST _WfcCombatTargetJumpCast,0
COMBAT_CAST _WfcCombatTargetAngleCast,0
// Only the pawn-class checks are overridden. Camera setters retain their
// native GC stores; the flying wrapper retains its controller cast and the
// original mouse/device/form/DeltaTime/Clamp route in the controller.
COMBAT_CAST _WfcCameraConstrainCast,0
COMBAT_CAST _WfcCameraLocationCast,0
COMBAT_CAST _WfcCameraOrbitCast,0
COMBAT_CAST _WfcCameraFlyingCast,0
COMBAT_CAST _WfcDownedHudCast,0
COMBAT_CAST _WfcOwnerFinderCast,0
COMBAT_CAST _WfcNavigateCast,2
COMBAT_CAST _WfcSpawnFinderCast,0

// OnSetPawn additionally requires the native ESI receiver to own this pawn.
// A rejected cast cannot fall through to Initialize(None)/ResetHud(None).
.globl _WfcAssignmentCast
_WfcAssignmentCast:
  pushfl
  cmpl _WfcCombatPlayerPawn,%ecx
  je 1f
  cmpl _WfcCombatAiPawn,%ecx
  je 1f
  popfl
  jmp *_WfcCombatOriginalCast
1:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  leal -16(%esi),%eax
  movl %eax,4(%esp)
  call _WfcResolvePlayerAssignment
  movl %eax,28(%ebx)
  testl %eax,%eax
  jz 2f
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  ret $4
2:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  addl $8,%esp
  jmp *_WfcAssignmentReject

.globl _WfcCombatMeleeTargetCast
_WfcCombatMeleeTargetCast:
  pushfl
  cmpl _WfcCombatPlayerPawn,%ecx
  je 1f
  cmpl _WfcCombatAiPawn,%ecx
  je 1f
  popfl
  jmp *_WfcCombatOriginalCast
1:
  pushal
  movl %esp,%ebp
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  leal -16(%ebx),%eax
  movl %eax,4(%esp)
  leal -16(%edi),%eax
  movl %eax,8(%esp)
  call _WfcResolvePlayerMeleeTarget
  movl %eax,28(%ebp)
  addl $16,%esp
  fxrstor (%esp)
  movl %ebp,%esp
  popal
  popfl
  ret $4

// FineAim Start/Stop call a typed-field getter (EAX=&manager.Owner), not
// IsA directly. Override only these calls; all other field casts stay native.
.macro COMBAT_FINE_FIELD name,start
.globl \name
\name:
  pushfl
  pushl %ecx
  movl (%eax),%ecx
  cmpl _WfcCombatPlayerPawn,%ecx
  je 1f
  cmpl _WfcCombatAiPawn,%ecx
  je 1f
  popl %ecx
  popfl
  jmp *_WfcCombatOriginalField
1:
  pushal
  movl %esp,%ebx
  subl $528,%esp
  andl $-16,%esp
  fxsave (%esp)
  subl $16,%esp
  movl %ecx,(%esp)
  movl $0,4(%esp)
  call _WfcResolvePlayerCombat
  testl %eax,%eax
  jz 2f
  movl 24(%ebx),%eax
  addl $16,%eax
  movl %eax,28(%ebx)
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popl %ecx
  popfl
  ret
2:
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popl %ecx
  popfl
  // Before this getter WFC only changed bFineAiming. Undo that owned bit
  // and leave through the exact native epilogue: no weapon/speed/camera call
  // has run. Returning None alone would still run the camera tail.
  addl $4,%esp
  .if \start
  andl $-2,24(%edi)
  jmp *_WfcCombatFineStartReject
  .else
  orl $1,24(%edi)
  jmp *_WfcCombatFineStopReject
  .endif
.endm
COMBAT_FINE_FIELD _WfcCombatFineStartCast,1
COMBAT_FINE_FIELD _WfcCombatFineStopCast,0
