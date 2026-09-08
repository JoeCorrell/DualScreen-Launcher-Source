.text
// cdecl(function, compiled receiver, output). output: PUSHAD registers,
// EFLAGS, then 16-aligned FXSAVE at +48. Preserve the test host's FP state.
.macro ROLE_TEST_CALL symbol, buddy=0
.globl \symbol
\symbol:
  pushl %ebp
  movl %esp,%ebp
  pushl %ebx
  pushl %esi
  pushl %edi
  subl $544,%esp
  andl $-16,%esp
  fxsave (%esp)
  movl %esp,-16(%ebp)
  movl 16(%ebp),%eax
  leal -4(%esp),%edx
  movl %edx,40(%eax)
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
  movl $0x22334455,%ecx
  movl $0x33445566,%edx
  movl $0x44556677,%esi
  movl $0x55667788,%edi
  .if \buddy == 1 || \buddy == 3 || \buddy == 5
  movl 12(%ebp),%edi
  movl $0x66778899,%eax
  .elseif \buddy == 2 || \buddy == 4 || \buddy == 6 || \buddy == 8
  movl 12(%ebp),%ecx
  movl $0x66778899,%eax
  .else
  movl 12(%ebp),%eax
  .endif
  .if \buddy == 7
  movl 24(%ebp),%ecx
  .endif
  .if \buddy == 9
  movl 12(%ebp),%esi
  movl 20(%ebp),%eax
  .endif
  .if \buddy == 8
  pushl 24(%ebp)
  pushl 20(%ebp)
  .elseif \buddy == 3
  pushl 28(%ebp)
  pushl 24(%ebp)
  pushl 20(%ebp)
  .elseif \buddy == 4 || \buddy == 5 || \buddy == 6 || \buddy == 7
  pushl 20(%ebp)
  .endif
  call *8(%ebp)
  pushfl
  pushal
  movl 16(%ebp),%edi
  movl %esp,%esi
  movl $9,%ecx
  rep movsl
  movl 16(%ebp),%edi
  fxsave 48(%edi)
  .if \buddy == 4
  fstps 36(%edi)
  .endif
  addl $36,%esp
  movl -16(%ebp),%esp
  fxrstor (%esp)
  leal -12(%ebp),%esp
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
.endm
ROLE_TEST_CALL _RoleTestCall
ROLE_TEST_CALL _RoleTestBuddyStateCall,1
ROLE_TEST_CALL _RoleTestControllerCall,2
// cdecl(function, compiled controller, output, DeltaTime, StickX*, StickY*)
ROLE_TEST_CALL _RoleTestVehicleStickCall,3
// cdecl(function, compiled targeting, output, compiled selected actor).
// Native x87 return is also stored as float at output+36.
ROLE_TEST_CALL _RoleTestTargetAngleCall,4
// cdecl(function, compiled victim, output, compiled targeting), native RET4.
ROLE_TEST_CALL _RoleTestMeleeTargetCall,5
// cdecl(function, compiled behavior, output, compiled actor); ECX, RET4.
ROLE_TEST_CALL _RoleTestCameraSetCall,6
// cdecl(function, compiled behavior, output, DeltaTime, CI*); EAX/ECX, RET4.
ROLE_TEST_CALL _RoleTestCameraFlyingCall,7
// cdecl(function, compiled receiver, output, native argument1, argument2); RET8.
ROLE_TEST_CALL _RoleTestOwnerCall,8
// cdecl(function, compiled controller, output, compiled pawn); ESI/EAX, RET.
ROLE_TEST_CALL _RoleTestAssignmentCall,9

.globl _RoleTestBuddyCall
_RoleTestBuddyCall:
  pushl %edi
  movl 12(%esp),%edi
  call *8(%esp)
  popl %edi
  ret
