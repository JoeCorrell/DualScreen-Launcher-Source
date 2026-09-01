using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Native Valheim UI sprite resolver plus embedded compatibility fallbacks.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Valheim's live InventoryGui is the first source for scalable panel,
    /// button, tab and slot art. The same three textures the Trader Overhaul
    /// panels are built from remain compiled into this assembly so a renamed
    /// prefab child or older game build degrades gracefully instead of losing
    /// every background.
    /// </para>
    /// <para>
    /// <c>LoadImage</c> is reached by reflection. It lives in
    /// <c>UnityEngine.ImageConversionModule</c>, and binding it directly ties
    /// this assembly to that module's exact version — which is the netstandard
    /// mismatch the trader mod hit and solved the same way.
    /// </para>
    /// </remarks>
    internal static class Textures
    {
        private static readonly Dictionary<string, Sprite> Cache =
            new Dictionary<string, Sprite>();

        private static readonly Dictionary<string, Sprite> NativeCache =
            new Dictionary<string, Sprite>();

        private static readonly HashSet<string> NativeMisses =
            new HashSet<string>();

        private static InventoryGui _nativeSource;
        private static Sprite _panelFillSource;

        private static readonly Assembly Self = Assembly.GetExecutingAssembly();

        private static readonly MethodInfo LoadImage =
            Type.GetType("UnityEngine.ImageConversion, UnityEngine.ImageConversionModule")
                ?.GetMethod("LoadImage", new[] { typeof(Texture2D), typeof(byte[]) });

        /// <summary>
        /// Pins the stock InventoryGui used as the native skin source.  UI
        /// prefabs are recreated between worlds, so a new instance invalidates
        /// the small role cache while the embedded fallback cache can stay.
        /// </summary>
        public static void BindNative(InventoryGui gui)
        {
            if (_nativeSource == gui) return;
            _nativeSource = gui;
            NativeCache.Clear();
            NativeMisses.Clear();
        }

        /// <summary>
        /// Returns Valheim's live UI art for a known role, falling back to the
        /// bundled texture only when that prefab does not expose an equivalent.
        /// This keeps the lower display visually native across game updates
        /// without making a missing or renamed child break the whole UI.
        /// </summary>
        public static Sprite Get(string name)
        {
            if (string.IsNullOrEmpty(name)) return null;
            if (name == "PanelFill") return GetPanelFill();
            Sprite native = GetNative(name);
            if (native != null) return native;
            if (name == "SlotBackground" || name == "SlotSelection"
                || name == "BarBackground") return null;
            if (Cache.TryGetValue(name, out var cached)) return cached;
            if (LoadImage == null) return null;

            var resource = "ValheimSecondScreen.Resources.Textures.UI." + name + ".png";
            using (var stream = Self.GetManifestResourceStream(resource))
            {
                if (stream == null)
                {
                    Plugin.Log.LogWarning("[SecondScreen] Texture missing: " + resource);
                    return null;
                }

                var data = new byte[stream.Length];
                int read = 0;
                while (read < data.Length)
                {
                    int count = stream.Read(data, read, data.Length - read);
                    if (count <= 0) break;
                    read += count;
                }
                if (read != data.Length)
                {
                    Plugin.Log.LogWarning("[SecondScreen] Texture truncated: " + resource);
                    return null;
                }

                var texture = new Texture2D(2, 2, TextureFormat.RGBA32, false);
                if (!(bool)LoadImage.Invoke(null, new object[] { texture, data }))
                {
                    UnityEngine.Object.Destroy(texture);
                    return null;
                }

                texture.name = name;
                texture.filterMode = FilterMode.Bilinear;
                texture.wrapMode = name == "PanelBackground"
                    ? TextureWrapMode.Repeat : TextureWrapMode.Clamp;
                /*
                 * These are frames, not photographs.  The old sprite had no
                 * border metadata, so scaling the square category frame into a
                 * wide card stretched its ornament and made every edge a
                 * different thickness.  Nine-slicing preserves the bronze
                 * corners used throughout the reference layout.
                 */
                /*
                 * The source art is roughly ten times denser than Unity's
                 * default 100 pixels-per-unit UI convention.  At 100 PPU its
                 * decorative edge became wider than a whole slot, causing
                 * Unity's sliced image to magnify the PNG's outer margin and
                 * checker-like corner pixels.  Crop that export margin and
                 * give the high-resolution sprite its real UI density.
                 */
                float insetX = name == "CategoryBackground" ? 30f
                    : name == "ButtonBackground" ? 54f : 0f;
                float insetY = name == "CategoryBackground" ? 30f
                    : name == "ButtonBackground" ? 42f : 0f;
                var rect = new Rect(insetX, insetY,
                    texture.width - insetX * 2f,
                    texture.height - insetY * 2f);
                float border = name == "CategoryBackground" ? 64f
                    : name == "ButtonBackground" ? 88f : 0f;
                var sprite = Sprite.Create(
                    texture,
                    rect,
                    new Vector2(0.5f, 0.5f),
                    name == "PanelBackground" ? 100f : 1000f,
                    0u,
                    SpriteMeshType.FullRect,
                    new Vector4(border, border, border, border));
                Cache[name] = sprite;
                return sprite;
            }
        }

        /// <summary>
        /// Crops the torn ornamental rim off Valheim's stock inventory panel.
        /// The retained centre is still the game's own material, but its new
        /// sprite rectangle is fully opaque and square at all four corners.
        /// </summary>
        private static Sprite GetPanelFill()
        {
            Sprite source = Get("PanelBackground");
            if (source == null) return null;
            if (Cache.TryGetValue("PanelFill", out Sprite cached)
                && cached != null && _panelFillSource == source)
                return cached;

            Rect sourceRect;
            try
            {
                sourceRect = source.textureRect;
            }
            catch
            {
                sourceRect = source.rect;
            }

            float inset = Mathf.Clamp(
                Mathf.Min(sourceRect.width, sourceRect.height) * 0.075f,
                4f, 42f);
            if (sourceRect.width <= inset * 2f + 8f
                || sourceRect.height <= inset * 2f + 8f)
                return source;

            var crop = new Rect(
                sourceRect.x + inset,
                sourceRect.y + inset,
                sourceRect.width - inset * 2f,
                sourceRect.height - inset * 2f);
            try
            {
                var sprite = Sprite.Create(
                    source.texture,
                    crop,
                    new Vector2(0.5f, 0.5f),
                    source.pixelsPerUnit,
                    0u,
                    SpriteMeshType.FullRect,
                    Vector4.zero);
                sprite.name = source.name + "_SecondScreenPanelFill";
                _panelFillSource = source;
                Cache["PanelFill"] = sprite;
                return sprite;
            }
            catch (Exception error)
            {
                Plugin.Log.LogWarning(
                    "[SecondScreen] Could not crop native panel texture: "
                    + error.Message);
                _panelFillSource = source;
                Cache["PanelFill"] = source;
                return source;
            }
        }

        /// <summary>True when a sprite came from Valheim rather than our fallback.</summary>
        public static bool IsNative(Sprite sprite)
        {
            if (sprite == null) return false;
            foreach (var value in NativeCache.Values)
                if (value == sprite) return true;
            return false;
        }

        private static Sprite GetNative(string role)
        {
            if (NativeCache.TryGetValue(role, out var cached) && cached != null)
                return cached;
            if (NativeMisses.Contains(role)) return null;

            var gui = _nativeSource != null ? _nativeSource : InventoryGui.instance;
            if (gui == null)
            {
                NativeMisses.Add(role);
                return null;
            }

            Sprite sprite = null;
            switch (role)
            {
                case "ButtonBackground":
                    sprite = ButtonSprite(gui.m_craftButton)
                             ?? ButtonSprite(gui.m_takeAllButton)
                             ?? ButtonSprite(gui.m_dropButton)
                             ?? ButtonSprite(gui.m_splitOkButton);
                    break;

                case "CategoryBackground":
                    sprite = ButtonSprite(gui.m_tabCraft)
                             ?? ButtonSprite(gui.m_tabUpgrade)
                             ?? ButtonSprite(gui.m_qualityLevelUp)
                             ?? ButtonSprite(gui.m_craftButton);
                    break;

                case "PanelBackground":
                    sprite = FindPanelSprite(gui.m_player)
                             ?? FindPanelSprite(gui.m_crafting)
                             ?? FindPanelSprite(gui.m_info)
                             ?? FindPanelSprite(gui.m_container)
                             ?? FindPanelSprite(gui.m_inventoryRoot);
                    break;

                case "SlotBackground":
                    sprite = FindSlotSprite(gui.m_playerGrid != null
                        ? gui.m_playerGrid.m_elementPrefab : null, false);
                    break;

                case "SlotSelection":
                    sprite = FindSlotSprite(gui.m_playerGrid != null
                        ? gui.m_playerGrid.m_elementPrefab : null, true);
                    break;

                case "BarBackground":
                    sprite = BarSprite(Hud.instance != null
                        ? Hud.instance.m_healthBarFast : null);
                    break;
            }

            if (sprite != null)
            {
                NativeCache[role] = sprite;
                Plugin.Log.LogDebug("[SecondScreen] Native UI sprite: "
                                    + role + " <- " + sprite.name);
                return sprite;
            }

            NativeMisses.Add(role);
            return null;
        }

        private static Sprite ButtonSprite(UnityEngine.UI.Button button)
        {
            if (button == null) return null;
            var image = button.targetGraphic as UnityEngine.UI.Image
                        ?? button.GetComponent<UnityEngine.UI.Image>();
            if (image == null || image.sprite == null) return null;
            return image.type == UnityEngine.UI.Image.Type.Sliced
                   || image.sprite.border.sqrMagnitude > 0.01f
                ? image.sprite : null;
        }

        private static Sprite BarSprite(GuiBar bar)
        {
            if (bar == null || bar.m_bar == null) return null;
            var image = bar.m_bar.GetComponent<UnityEngine.UI.Image>();
            return image != null ? image.sprite : null;
        }

        private static Sprite FindPanelSprite(Transform root)
        {
            if (root == null) return null;
            UnityEngine.UI.Image best = null;
            int bestScore = int.MinValue;
            foreach (var image in root.GetComponentsInChildren<UnityEngine.UI.Image>(true))
            {
                if (image == null || image.sprite == null) continue;
                // A decorative stock panel is only safe at 1240x1080 when the
                // source carries nine-slice metadata.  Borderless pictures are
                // intentionally rejected instead of stretching their carvings.
                if (image.type != UnityEngine.UI.Image.Type.Sliced
                    && image.sprite.border.sqrMagnitude <= 0.01f) continue;
                string value = (image.name ?? "").ToLowerInvariant();
                bool panelName = value.Contains("panel")
                                 || value.Contains("background")
                                 || value.Contains("bkg")
                                 || value.Contains("frame");
                if (!panelName && image.transform != root) continue;
                int score = 0;
                if (value.Contains("panel")) score += 90;
                if (value.Contains("background") || value.Contains("bkg")) score += 70;
                if (value.Contains("frame")) score += 35;
                if (value.Contains("icon") || value.Contains("glow")
                    || value.Contains("selected")) score -= 100;
                var rect = image.rectTransform != null ? image.rectTransform.rect : Rect.zero;
                bool panelSized = rect.width >= 300f && rect.height >= 180f;
                bool explicitlyPanel = value.Contains("panel")
                                       && rect.width >= 160f && rect.height >= 120f;
                if (!panelSized && !explicitlyPanel) continue;
                if (panelSized) score += 45;
                score += Mathf.RoundToInt(Mathf.Min(30f,
                    Mathf.Sqrt(Mathf.Max(0f, rect.width * rect.height)) / 20f));
                if (score <= bestScore) continue;
                best = image;
                bestScore = score;
            }
            return best != null && bestScore >= 35 ? best.sprite : null;
        }

        private static Sprite FindSlotSprite(GameObject prefab, bool selection)
        {
            if (prefab == null) return null;
            UnityEngine.UI.Image best = null;
            int bestScore = int.MinValue;
            foreach (var image in prefab.GetComponentsInChildren<UnityEngine.UI.Image>(true))
            {
                if (image == null || image.sprite == null) continue;
                string value = (image.name ?? "").ToLowerInvariant();
                bool selectedName = value.Contains("selected")
                                    || value.Contains("selection")
                                    || value.Contains("highlight");
                int score = selection
                    ? (selectedName ? 150 : -100)
                    : (selectedName ? -120 : 0);
                if (!selection && (value.Contains("bkg")
                                   || value.Contains("background")
                                   || value.Contains("frame"))) score += 90;
                if (value.Contains("icon") || value.Contains("amount")
                    || value.Contains("quality") || value.Contains("durability"))
                    score -= 100;
                if (image.transform == prefab.transform) score += 55;
                if (score <= bestScore) continue;
                best = image;
                bestScore = score;
            }
            int minimum = selection ? 100 : 35;
            return best != null && bestScore >= minimum ? best.sprite : null;
        }
    }
}
