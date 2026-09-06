# Approved visual reference

The user's seven-panel mockup supplied on 2026-09-05 is the visual reference
for the native lower-screen interface. Implement its visual language at the
actual 1240 x 1080 resolution, rather than displaying the contact sheet itself.

## Shared composition

- Near-black charcoal ground with restrained blue-gray texture, fine distressed
  brass frames, cream lettering and hairline separators.
- Original Emerge BF headings and ChaletComprime-CologneEighty body type,
  subject to readable sizes on the device. Reuse extracted journal artwork,
  power symbols and selection panels from the user's game installation.
- Pale blue-white occult selection glow; blood red for health and meaningful
  warnings; parchment for readable documents and selected descriptive rows.
- Page title, persistent navigation, generous main content and contextual
  description area. Seven primary destinations remain directly reachable:
  Map, Powers, Equipment, Objectives, Journal, Bone Charms, Statistics.
- Minimum 64-pixel touch targets, visible pressed and selected states, clipped
  inertial lists, and enough room for localized labels. No hover dependency.
- Draw frames and typography at native resolution. Do not stretch a screenshot
  of the reference or bake labels and changing quantities into artwork.

## Pages

Map uses the reference's large central illustration, restrained legend,
zoom/recenter controls and selected-location detail. Only acquired map imagery
and verified discovery predicates may populate it. Position/facing require a
verified coordinate transform for that map.

Powers uses a three-column illustrated card grid with the authentic icons,
current tier, mana cost and selected power detail. Tier counts and rune prices
come from the current ability definition, not the reference's decorative dots.

Equipment uses a prominent current-loadout row, inventory grid, quantity and
capacity text, and a selected-item description. All actions target owned items.

Objectives uses a left mission/task list and a larger description/progress
pane, with distinct optional and completed states. Journal is a separate page
using the same split composition with categories, acquired entries and a
scrollable parchment reader; long text is never truncated to fit the mockup.

Bone Charms uses equipped slots above the collected grid, with effect detail
below. Collected quantity, available slots, levels and rune totals are real
values. Equip/remove controls require validated native eligibility.

Statistics uses authentic Corvo artwork where available, health/mana strips,
chaos presentation and readable grouped mission/progression values. Preserve
the real meanings and denominators of statistics; do not fabricate completion
percentages or substitute decorative numbers.

## Content and scope

The image directs appearance, not game facts. Its Grand Palace example,
sample counts, item names and hypothetical upgrades must not introduce systems
absent from this installed Dishonored build. A shop/upgrades view is contextual
additional content only if the actual game exposes it safely; it does not
replace Objectives or Journal or permit purchases outside native eligibility.

Acceptance requires device screenshots of each populated page compared with
this reference, readable text at physical size, isolated multitouch, and live
state/action checks. A matching static mockup alone is not a finished mod.
