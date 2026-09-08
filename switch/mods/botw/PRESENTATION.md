# BOTW item presentation, 2026-09-06

Scope: external bottom-screen renderer only. Native Eat/Hold hooks, pouch mutation,
effects, save state, RomFS and existing model caches are unchanged.

## Evidence

- Supported raw main CD57B23FA4BBAD65803D9788C01821EE: D4279C..D42A58
  selects pause-player notifications using ordered ActorInfo tags and the
  CookResult profile. 9C7540..9C7730 translates notifications 08000092..98 into
  EatFood, EatCookingDish, EatCookingSkewer, EatBizarre, EatTooHard, Drink,
  UseFairy. `trace_item_presentation.py` locates the string references read-only.
- Owner's PauseMenuPlayer.sbactorpack, AS/PauseMenuPlayer_UseItem.bas: raw food
  uses Eat_OneHand_R, with repeat L/R branches during UseItem. Dishes use
  Eat_Normal; skewers Eat_BBQ; other branches Eat_LookBad, Eat_Hard, Eat_Drink,
  Eat_Fairy. Previous preview always selected Eat_Normal for every item.
- FaceUseItem supplies the voice-event frames. Owner's Bootup.pack SLink2DB,
  PauseMenuPlayer user, supplies chewing/drinking/swallowing trigger windows,
  gain, pitch and delayed blend layers. TitleBG.pack supplies original samples.
  SLink decoded with https://github.com/dt-12345/xlink2 (UKing/NX); extracted
  BARS FWAV samples decoded using https://github.com/vgmstream/vgmstream.
  These are development tools, not app/runtime dependencies.
- Raw Prime Meat Item_Meat_02 and Gourmet Meat Item_Meat_11 GLB bounds match the
  original ActorInfo bounds to half-float precision. No recentering or arbitrary
  per-item Y correction is justified. The existing stationary attachment cache
  updated scale but retained the spawn translation/rotation. It now recaptures
  the complete native hand-relative placement on a real scale transition.
  Once settled, movement alone cannot move the portrait attachments.

## Implementation

- Module-only asset overlays keep the shared/legacy host assets unchanged.
- Export original skeletal clips at native 30-Hz samples; renderer interpolates
  them at display cadence and keeps original duration. No looping Eat clip.
- Export 399 actor presentation mappings, 35 timed sound cues and 14 original
  PCM samples (under 0.9 MB total). Sound decoding is offline, loading async.
- Sound cues advance once on the same clock as the portrait animation, not on
  each render retry. Replacing/cancelling/hiding/releasing a preview stops its
  streams. Late loads are not played out of sync. No audio-focus request that
  would duck or stop emulator audio.
- Remove the fixed inventory-icon overlay drawn over the eating hands.

## Validation and remaining device test

`test_item_presentation.py`: actor selections, clip times/matrices, sound files
and cue bounds; executes production attachment math for two meat models,
201 movement samples, single/multiple/reorder/release. Passed.

`test_migration.py --typecheck`: complete external UI against actual Android,
Filament and companion API, unchanged gameplay hook/state contracts. Passed.

Not yet device-verified: final meat placement, sound synchronization/no doubled
native audio, all eating variants, cancellation and multiple held items. User's
running game has not been paused/restarted by this work. Do not describe these
fixes as on-device verified until the new package is loaded and tested.
