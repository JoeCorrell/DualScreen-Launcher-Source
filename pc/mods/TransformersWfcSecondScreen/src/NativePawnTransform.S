.text
.globl _WfcPawnTransformGuard
_WfcPawnTransformGuard:
  // Native Transform: ECX=compiled pawn, stack byte-slot + delegate pointer.
  // No engine calls, reflection, allocation, or pawn memory access here.
  pushfl
  cmpl _WfcPawnTransformBlocked,%ecx
  jne 1f
  popfl
  movl $0,%eax
  ret $8
1:
  popfl
  jmp *_WfcPawnTransformOriginal
.globl _WfcPawnStartVehicleGuard
_WfcPawnStartVehicleGuard:
  // Native private StartVehicle: EAX=compiled pawn, no stack parameters.
  pushfl
  cmpl _WfcPawnTransformBlocked,%eax
  jne 1f
  popfl
  ret
1:
  popfl
  jmp *_WfcPawnStartVehicleOriginal
