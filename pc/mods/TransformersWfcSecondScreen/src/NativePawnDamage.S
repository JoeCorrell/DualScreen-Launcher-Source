.text
// No C++ callback or frame polling. The original network guards have already
// run. Block's shared buff callback has also completed exactly once.
.macro DAMAGE_GATE name, identity, original, done
.globl \name
\name:
  pushfl
  cmpl \identity,%esi
  je 1f
  popfl
  jmp *\original
1:
  popfl
  jmp *\done
.endm
DAMAGE_GATE _WfcDamageFlashGate,_WfcDamagePawn,_WfcDamageFlashOriginal,_WfcDamageFlashReturn
DAMAGE_GATE _WfcDamageBlockGate,_WfcDamageInterface,_WfcDamageBlockOriginal,_WfcDamageBlockReturn
