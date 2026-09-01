using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using ValheimSecondScreen.UI;

namespace ValheimSecondScreen.Hooks
{
    /// <summary>
    /// Puts each moved HUD element into its slot in the second screen's frame.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Reparenting the HUD is only half the job. Every one of those
    /// RectTransforms is anchored for a wide game screen — health at the bottom
    /// left, minimap in the top right corner, everything hugging an edge that is
    /// no longer there. Dropped onto a nearly square panel, that reads as a HUD
    /// scattered around a large hole.
    /// </para>
    /// <para>
    /// The anchors are rewritten rather than the positions nudged. A prefab
    /// anchored to a corner and merely moved would drift the moment the panel's
    /// size changed; re-anchoring inside a slot makes the arrangement hold at any
    /// resolution, and the slot is what moves if the frame is ever rearranged.
    /// Sizes are left alone and only the scale is raised, so the game's own art
    /// stays crisp rather than being stretched into a layout it was not drawn
    /// for.
    /// </para>
    /// <para>
    /// Nothing here activates anything. What is shown is the game's decision, and
    /// the mount panel, the ship's controls, the boss bar and the save icon are
    /// all things Valheim switches on exactly when they apply.
    /// </para>
    /// </remarks>
    internal static class SecondScreenLayout
    {
        /// <summary>Valheim's small map is 250 units across before scaling.</summary>
        private const float MinimapSize = 250f;

        /// <summary>
        /// How much room the map leaves inside its page.
        /// </summary>
        /// <remarks>
        /// The scale is computed from the page rather than typed in, and that is
        /// the fix for the panel that looked too big for what was in it: a fixed
        /// multiplier left a 250-unit map sitting inside a page drawn at the size
        /// the map should have been, so the backdrop read as an empty frame
        /// around a small picture. Now the map is whatever multiple of 250 fills
        /// the page, less this margin.
        /// </remarks>
        private const float MinimapMargin = 10f;

        private static readonly FieldInfo SmallRootField =
            AccessTools.Field(typeof(Minimap), "m_smallRoot");

        public static void Apply(SecondScreenUI ui, Hud hud, IEnumerable<Transform> roots)
        {
            if (ui == null || hud == null) return;

            var placed = new List<string>();
            var unplaced = new List<string>();

            var minimap = Minimap.instance != null
                ? SmallRootField?.GetValue(Minimap.instance) as GameObject
                : null;

            foreach (var root in roots)
            {
                var rect = root as RectTransform;
                if (rect == null) continue;

                if (minimap != null && root == minimap.transform)
                {
                    /*
                     * Measured, not assumed.
                     *
                     * The 250 units this used to divide by is Valheim's small
                     * map at its default size, and it is not what every build
                     * ships -- a root that is actually larger came out scaled
                     * too little, which is why the map sat small in a page drawn
                     * at the size it should have filled. rect.rect is valid here
                     * because a prefab carries its own sizeDelta; the constant
                     * remains only as a fallback for a root with none.
                     */
                    float own = Mathf.Max(rect.rect.width, rect.rect.height);
                    if (own < 1f) own = MinimapSize;
                    float room = Mathf.Min(ui.PageSize.x, ui.PageSize.y) - MinimapMargin;
                    float scale = Mathf.Max(1f, room / own);
                    // Superseded by the full map, which is the one that pans.
                    Park(rect, ui);
                    placed.Add("parked:minimap");
                }
                else if (Is(root, hud.m_healthBarRoot)
                         || Is(root, hud.m_foodBarRoot)
                         || Is(root, hud.m_staminaBar2Root)
                         || Is(root, hud.m_eitrBarRoot)
                         || Is(root, hud.m_adrenalineBarRoot))
                {
                    /*
                     * Parked, because the panel draws these itself now.
                     *
                     * Valheim's bars are wide and short because they live along
                     * the bottom of a 16:9 screen; this panel has height and no
                     * width, so they are redrawn as columns. Leaving the
                     * originals on the game screen would show the same four
                     * readings in two places, a frame apart.
                     */
                    Park(rect, ui);
                    placed.Add("parked:" + root.name);
                }
                else if (Is(root, hud.m_gpRoot))
                {
                    Park(rect, ui);
                    placed.Add("parked:guardian power");
                }
                else if (Is(root, hud.m_statusEffectListRoot))
                {
                    // The dashboard's BUFFS card renders the same live status
                    // effects in the reference layout.
                    Park(rect, ui);
                    placed.Add("parked:status effects");
                }
                else if (root.GetComponent<HotkeyBar>() != null)
                {
                    /*
                     * Hidden, not parked, and the difference is the whole bug.
                     *
                     * Parking deactivates, and an inactive MonoBehaviour does
                     * not Update -- which is where Valheim reads the number keys
                     * and the controller's hotbar bindings. Switching the real
                     * hotbar off to draw our own therefore took hotbar input
                     * away from the game entirely. It stays alive and running,
                     * with nothing drawn.
                     */
                    Hide(rect, ui);
                    placed.Add("hidden:hotbar");
                }
                else if (Is(root, hud.m_eventBar))
                {
                    // The boss and raid banner, along the strip under the tabs
                    // where it pushes nothing else out of place when it appears.
                    Centre(rect, ui.TopStrip, Vector2.zero, 1.3f);
                    placed.Add("event bar");
                }
                else if (hud.m_staggerAnimator != null && root == hud.m_staggerAnimator.transform)
                {
                    Centre(rect, ui.TopStrip, new Vector2(0f, -6f), 1.4f);
                    placed.Add("stagger");
                }
                else if (Is(root, hud.m_mountPanel))
                {
                    Anchor(rect, ui.Root, new Vector2(1f, 0f), new Vector2(1f, 0f),
                           new Vector2(-24f, 190f), 1.4f);
                    placed.Add("mount");
                }
                else if (Is(root, hud.m_shipHudRoot))
                {
                    Anchor(rect, ui.Root, new Vector2(0f, 0f), new Vector2(0f, 0f),
                           new Vector2(24f, 190f), 1.4f);
                    placed.Add("ship");
                }
                else if (Is(root, hud.m_saveIcon) || Is(root, hud.m_badConnectionIcon))
                {
                    Anchor(rect, ui.Root, new Vector2(1f, 1f), new Vector2(1f, 1f),
                           new Vector2(-24f, -96f), 1.1f);
                    placed.Add(root.name);
                }
                else
                {
                    // Left where the game put it rather than parked somewhere
                    // arbitrary: an unplaced element in its original corner is
                    // obvious in a screenshot and named in the line below,
                    // whereas one quietly stacked under another is neither.
                    unplaced.Add(root.name);
                }
            }

            // Several parts of the stock vital HUD do not consistently live
            // below the corresponding *BarRoot.  In particular, some prefab
            // versions leave the health ornament on m_healthPanel, put the
            // stamina ornament on the animator object, and keep food overlays
            // beside (rather than below) m_foodBarRoot.  Moving/parking only the
            // bar roots consequently leaves those decorations on display zero.
            // Suppress every stock vital visual explicitly.  CanvasGroup keeps
            // the objects active, so Hud can continue updating food timers and
            // bar animators while our Dashboard draws the replacement UI.
            foreach (var visual in CollectStockVitalVisuals(hud))
            {
                var group = visual.GetComponent<CanvasGroup>()
                            ?? visual.gameObject.AddComponent<CanvasGroup>();
                group.alpha = 0f;
                group.interactable = false;
                group.blocksRaycasts = false;
            }
            placed.Add("hidden:stock health/food/stamina visuals");

            Plugin.Log.LogInfo("[SecondScreen] Laid out: " + string.Join(", ", placed.ToArray()));
            if (unplaced.Count > 0)
            {
                Plugin.Log.LogInfo("[SecondScreen] Left where they were: "
                                   + string.Join(", ", unplaced.ToArray()));
            }
        }

        /// <summary>
        /// All stock visual objects represented by the custom status card.
        /// </summary>
        /// <remarks>
        /// These are deliberately collected by the fields Hud itself updates,
        /// rather than by prefab names.  That covers both the current hierarchy
        /// and builds where an icon/overlay is a sibling of its nominal bar
        /// root.  Distinct avoids stacking multiple zero-alpha CanvasGroups when
        /// two fields point into the same object.
        /// </remarks>
        internal static IEnumerable<Transform> CollectStockVitalVisuals(Hud hud)
        {
            if (hud == null) yield break;

            var visuals = new List<Transform>();
            AddVisual(visuals, hud.m_healthPanel);
            AddVisual(visuals, hud.m_healthBarRoot);
            AddVisual(visuals, hud.m_healthAnimator);
            AddVisual(visuals, hud.m_healthBarFast);
            AddVisual(visuals, hud.m_healthBarSlow);
            AddVisual(visuals, hud.m_healthText);

            AddVisual(visuals, hud.m_foodBarRoot);
            AddVisual(visuals, hud.m_foodBaseBar);
            AddVisual(visuals, hud.m_foodIcon);
            AddVisual(visuals, hud.m_foodText);
            AddVisuals(visuals, hud.m_foodBars);
            AddVisuals(visuals, hud.m_foodIcons);
            AddVisuals(visuals, hud.m_foodTime);

            AddVisual(visuals, hud.m_staminaBar2Root);
            AddVisual(visuals, hud.m_staminaAnimator);
            AddVisual(visuals, hud.m_staminaBar2Fast);
            AddVisual(visuals, hud.m_staminaBar2Slow);
            AddVisual(visuals, hud.m_staminaText);

            AddVisual(visuals, hud.m_eitrBarRoot);
            AddVisual(visuals, hud.m_eitrAnimator);
            AddVisual(visuals, hud.m_eitrBarFast);
            AddVisual(visuals, hud.m_eitrBarSlow);
            AddVisual(visuals, hud.m_eitrText);

            AddVisual(visuals, hud.m_adrenalineBarRoot);
            AddVisual(visuals, hud.m_adrenalineAnimator);
            AddVisual(visuals, hud.m_adrenalineBarFast);
            AddVisual(visuals, hud.m_adrenalineBarSlow);
            AddVisual(visuals, hud.m_adrenalineText);

            foreach (var visual in visuals.Distinct())
                if (visual != null) yield return visual;
        }

        private static void AddVisual(ICollection<Transform> visuals, Component value)
        {
            if (value != null) visuals.Add(value.transform);
        }

        private static void AddVisuals<T>(ICollection<Transform> visuals, T[] values)
            where T : Component
        {
            if (values == null) return;
            foreach (var value in values) AddVisual(visuals, value);
        }

        /// <summary>
        /// Moves a root out of sight without destroying it.
        /// </summary>
        /// <remarks>
        /// The parked container is inactive, so anything inside it stops being
        /// drawn and stops costing a layout pass — but it still exists, which
        /// matters because these objects belong to Valheim and its own scripts
        /// go on holding references to them.
        /// </remarks>
        private static void Park(RectTransform rect, SecondScreenUI ui)
        {
            if (ui.Parked == null) return;
            rect.SetParent(ui.Parked, false);
        }

        /// <summary>How far apart stacked rail entries sit, in reference units.</summary>
        private const float RailStep = 96f;
        private const float RailTop = -28f;

        /// <summary>
        /// Places an element down one of the side rails.
        /// </summary>
        /// <remarks>
        /// Anchored to the rail's top centre and stepped down by index, so the
        /// order is decided by one number at the call site and adding something
        /// between two existing entries does not mean recomputing offsets.
        /// </remarks>
        private static void Stack(RectTransform rect, RectTransform rail, int index, float scale)
        {
            if (rail == null) return;
            rect.SetParent(rail, false);
            rect.anchorMin = new Vector2(0.5f, 1f);
            rect.anchorMax = new Vector2(0.5f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.anchoredPosition = new Vector2(0f, RailTop - index * RailStep);
            Scale(rect, scale);
        }

        /// <summary>
        /// Makes a root invisible without stopping its scripts.
        /// </summary>
        /// <remarks>
        /// For the elements whose behaviour is still wanted and whose picture is
        /// not. A CanvasGroup at zero alpha with raycasts off draws nothing and
        /// blocks nothing, while the object stays active and its Update keeps
        /// running -- which for the hotbar is where the game reads the keys that
        /// select a slot.
        /// </remarks>
        private static void Hide(RectTransform rect, SecondScreenUI ui)
        {
            rect.SetParent(ui.Root, false);
            var group = rect.GetComponent<CanvasGroup>() ?? rect.gameObject.AddComponent<CanvasGroup>();
            group.alpha = 0f;
            group.interactable = false;
            group.blocksRaycasts = false;
        }

        /// <summary>Stretches a root to fill a slot, corner to corner.</summary>
        private static void Fill(RectTransform rect, RectTransform slot)
        {
            if (slot == null) return;
            rect.SetParent(slot, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            rect.localScale = Vector3.one;
            rect.localRotation = Quaternion.identity;
        }

        private static void Centre(RectTransform rect, RectTransform slot,
                                   Vector2 offset, float scale)
        {
            if (slot == null) return;
            rect.SetParent(slot, false);
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = offset;
            Scale(rect, scale);
        }

        private static void Anchor(RectTransform rect, RectTransform parent,
                                   Vector2 anchor, Vector2 pivot,
                                   Vector2 offset, float scale)
        {
            if (parent == null) return;
            rect.SetParent(parent, false);
            rect.anchorMin = anchor;
            rect.anchorMax = anchor;
            rect.pivot = pivot;
            rect.anchoredPosition = offset;
            Scale(rect, scale);
        }

        private static void Scale(RectTransform rect, float scale)
        {
            rect.localScale = new Vector3(scale, scale, 1f);
            rect.localRotation = Quaternion.identity;
        }

        private static bool Is(Transform candidate, Component other) =>
            other != null && candidate == other.transform;

        private static bool Is(Transform candidate, GameObject other) =>
            other != null && candidate == other.transform;
    }
}
