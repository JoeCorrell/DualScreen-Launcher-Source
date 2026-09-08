.text
// Stock calls incur only a flag-preserving identity comparison and tail jump.
// Mapped calls preserve GPRs and x87/MMX/SSE across the C++ resolver. The
// resolver's C ABI must not leak clobbers into this optimized private ABI.
.macro ROLE_GETTER symbol, identity, original, ai, receiver=%eax, boolean=0
.globl \symbol
\symbol:
  pushfl
  cmpl \identity,\receiver
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
  movl \receiver,(%esp)
  movl $\ai,4(%esp)
  call _WfcResolveNativeRolePawn
  .if \boolean
  cmpl $0x10,%eax
  setne %al
  movzbl %al,%eax
  .endif
  movl %eax,28(%ebx)
  addl $16,%esp
  fxrstor (%esp)
  movl %ebx,%esp
  popal
  popfl
  .if \boolean
  testl %eax,%eax
  ret
  .endif
  // Match the stock final TEST success/None flags, preserving returned EAX.
  pushl %eax
  cmpl $0x10,%eax
  setne %al
  movzbl %al,%eax
  testl %eax,%eax
  popl %eax
  ret
.endm
ROLE_GETTER _WfcRolePlayerGetter,_WfcRoleNativePlayerController,_WfcRolePlayerOriginal,0
ROLE_GETTER _WfcRoleAiGetter,_WfcRoleNativeAiController,_WfcRoleAiOriginal,1
ROLE_GETTER _WfcRoleBuddyGetter,_WfcRoleNativeAiController,_WfcRoleBuddyOriginal,1,%edi,1

// This shipping setter has been specialized to Value=true (EDI receiver,
// no stack argument). Logical former-player buddy status already comes from
// the validated mapping/getter: never write the sibling AI-only pawn tail.
// No pointer dereference or callback is needed, even for a stale/off-thread
// mapped request. Unmapped controllers keep the entire native setter.
.globl _WfcRoleBuddySetter
_WfcRoleBuddySetter:
  pushfl
  cmpl _WfcRoleNativeAiController,%edi
  je 1f
  popfl
  jmp *_WfcRoleBuddySetOriginal
1:
  popfl
  ret
