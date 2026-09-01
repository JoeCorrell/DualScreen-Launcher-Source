using System;
using System.Collections.Generic;
using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Native Valheim icons where a role has an exact counterpart, with a
    /// cohesive monochrome glyph set for the remaining navigation roles.
    /// </summary>
    /// <remarks>
    /// Fallbacks are generated once rather than shipped as unrelated pictures,
    /// which keeps every stroke the same weight and lets the UI tint them with
    /// the same warm metal colour as the panel borders.
    /// </remarks>
    internal static class UiIcons
    {
        private const int Size = 96;
        private const int Stroke = 4;
        private static readonly Dictionary<string, Sprite> Cache =
            new Dictionary<string, Sprite>();
        private static readonly HashSet<Sprite> NativeSprites =
            new HashSet<Sprite>();

        /// <summary>
        /// Resolves navigation artwork from Valheim when the relevant game
        /// subsystem is ready, with a semantic line-glyph used only during the
        /// short startup window before those live assets exist.
        /// </summary>
        public static Sprite GetPageIcon(string name)
        {
            string key = "page:" + name;
            if (Cache.TryGetValue(key, out Sprite cached)) return cached;

            Sprite sprite = NativePageIcon(name);
            if (sprite != null)
            {
                Cache[key] = sprite;
                NativeSprites.Add(sprite);
                return sprite;
            }

            /*
             * ObjectDB, the local player's skill definitions and Minimap do
             * not necessarily become available in the same frame as
             * InventoryGui.  Caching Hammer here made every unresolved page
             * look like Crafting for the rest of the session.  Return the
             * role-specific fallback without putting it in the page cache;
             * SecondScreenUI can then retry and replace it with the exact live
             * Valheim sprite as soon as that subsystem is ready.
             */
            return Get(name);
        }

        public static Sprite Get(string name)
        {
            if (Cache.TryGetValue(name, out var sprite)) return sprite;

            sprite = NativeIcon(name);
            if (sprite != null)
            {
                Cache[name] = sprite;
                NativeSprites.Add(sprite);
                return sprite;
            }

            var pixels = new Color32[Size * Size];
            var white = new Color32(255, 255, 255, 255);
            Action<int, int> dot = (x, y) => Disc(pixels, x, y, Stroke, white);

            switch (name)
            {
                case "inventory":
                    Line(dot, 27, 25, 69, 25);
                    Line(dot, 23, 31, 20, 69);
                    Line(dot, 73, 31, 76, 69);
                    Line(dot, 20, 69, 28, 78);
                    Line(dot, 76, 69, 68, 78);
                    Line(dot, 28, 78, 68, 78);
                    Arc(dot, 48, 27, 17, 195f, 345f);
                    Line(dot, 31, 52, 65, 52);
                    Rect(dot, 40, 57, 56, 70);
                    Line(dot, 30, 34, 25, 47);
                    Line(dot, 66, 34, 71, 47);
                    break;

                case "crafting":
                    Line(dot, 24, 24, 73, 73);
                    Line(dot, 73, 24, 24, 73);
                    Line(dot, 17, 18, 30, 17);
                    Line(dot, 17, 18, 16, 31);
                    Line(dot, 66, 17, 79, 18);
                    Line(dot, 79, 18, 80, 31);
                    Line(dot, 20, 70, 27, 77);
                    Line(dot, 69, 77, 76, 70);
                    break;

                case "map":
                    Line(dot, 18, 29, 39, 20);
                    Line(dot, 39, 20, 58, 29);
                    Line(dot, 58, 29, 79, 20);
                    Line(dot, 18, 29, 18, 75);
                    Line(dot, 18, 75, 39, 66);
                    Line(dot, 39, 66, 58, 75);
                    Line(dot, 58, 75, 79, 66);
                    Line(dot, 79, 20, 79, 66);
                    Line(dot, 39, 20, 39, 66);
                    Line(dot, 58, 29, 58, 75);
                    break;

                case "skills":
                    Circle(dot, 48, 48, 13);
                    for (int i = 0; i < 8; i++)
                    {
                        float a = i * Mathf.PI / 4f;
                        Line(dot,
                             48 + Mathf.RoundToInt(Mathf.Cos(a) * 23f),
                             48 + Mathf.RoundToInt(Mathf.Sin(a) * 23f),
                             48 + Mathf.RoundToInt(Mathf.Cos(a) * 34f),
                             48 + Mathf.RoundToInt(Mathf.Sin(a) * 34f));
                    }
                    break;

                case "trophies":
                    Arc(dot, 48, 37, 22, 180f, 360f);
                    Line(dot, 26, 37, 30, 57);
                    Line(dot, 70, 37, 66, 57);
                    Arc(dot, 48, 54, 18, 0f, 180f);
                    Line(dot, 48, 71, 48, 80);
                    Line(dot, 34, 82, 62, 82);
                    Line(dot, 26, 40, 17, 40);
                    Line(dot, 17, 40, 20, 55);
                    Line(dot, 70, 40, 79, 40);
                    Line(dot, 79, 40, 76, 55);
                    break;

                case "settings":
                    Circle(dot, 48, 48, 13);
                    Circle(dot, 48, 48, 30);
                    for (int i = 0; i < 8; i++)
                    {
                        float a = i * Mathf.PI / 4f;
                        Line(dot,
                             48 + Mathf.RoundToInt(Mathf.Cos(a) * 29f),
                             48 + Mathf.RoundToInt(Mathf.Sin(a) * 29f),
                             48 + Mathf.RoundToInt(Mathf.Cos(a) * 38f),
                             48 + Mathf.RoundToInt(Mathf.Sin(a) * 38f));
                    }
                    break;

                case "chat":
                    // Rounded speech bubble with a short tail and three message dots.
                    Arc(dot, 48, 49, 30, 18f, 338f);
                    Line(dot, 27, 70, 22, 82);
                    Line(dot, 22, 82, 39, 75);
                    FilledCircle(pixels, 35, 49, 4, white);
                    FilledCircle(pixels, 48, 49, 4, white);
                    FilledCircle(pixels, 61, 49, 4, white);
                    break;

                case "weight":
                    Arc(dot, 48, 28, 13, 180f, 360f);
                    Line(dot, 32, 34, 24, 75);
                    Line(dot, 64, 34, 72, 75);
                    Line(dot, 24, 75, 72, 75);
                    break;

                case "buffs":
                    Line(dot, 48, 16, 75, 27);
                    Line(dot, 75, 27, 70, 58);
                    Line(dot, 70, 58, 48, 80);
                    Line(dot, 48, 80, 26, 58);
                    Line(dot, 26, 58, 21, 27);
                    Line(dot, 21, 27, 48, 16);
                    Line(dot, 48, 32, 48, 63);
                    Line(dot, 33, 47, 63, 47);
                    break;

                case "sun":
                    FilledCircle(pixels, 48, 48, 17, white);
                    for (int i = 0; i < 8; i++)
                    {
                        float a = i * Mathf.PI / 4f;
                        Line(dot,
                             48 + Mathf.RoundToInt(Mathf.Cos(a) * 25f),
                             48 + Mathf.RoundToInt(Mathf.Sin(a) * 25f),
                             48 + Mathf.RoundToInt(Mathf.Cos(a) * 35f),
                             48 + Mathf.RoundToInt(Mathf.Sin(a) * 35f));
                    }
                    break;

                case "moon":
                    FilledCircle(pixels, 43, 49, 25, white);
                    FilledCircle(pixels, 56, 59, 23, new Color32(0, 0, 0, 0));
                    break;

                case "weather_rain":
                    Arc(dot, 37, 48, 18, 55f, 190f);
                    Arc(dot, 55, 45, 22, 20f, 155f);
                    Arc(dot, 69, 52, 14, -25f, 105f);
                    Line(dot, 22, 54, 78, 54);
                    Line(dot, 30, 66, 25, 77);
                    Line(dot, 48, 66, 43, 77);
                    Line(dot, 66, 66, 61, 77);
                    break;

                case "weather_snow":
                    Arc(dot, 37, 43, 18, 55f, 190f);
                    Arc(dot, 55, 40, 22, 20f, 155f);
                    Arc(dot, 69, 47, 14, -25f, 105f);
                    Line(dot, 22, 49, 78, 49);
                    for (int i = 0; i < 3; i++)
                    {
                        int x = 31 + i * 18;
                        Line(dot, x, 62, x, 78);
                        Line(dot, x - 7, 66, x + 7, 74);
                        Line(dot, x + 7, 66, x - 7, 74);
                    }
                    break;

                case "weather_fog":
                    Arc(dot, 37, 38, 18, 55f, 190f);
                    Arc(dot, 55, 35, 22, 20f, 155f);
                    Arc(dot, 69, 42, 14, -25f, 105f);
                    Line(dot, 22, 44, 78, 44);
                    Line(dot, 20, 60, 76, 60);
                    Line(dot, 29, 72, 82, 72);
                    break;

                default:
                    Circle(dot, 48, 48, 25);
                    break;
            }

            var texture = new Texture2D(Size, Size, TextureFormat.RGBA32, false)
            {
                name = "SecondScreen_" + name + "Icon",
                filterMode = FilterMode.Bilinear,
                wrapMode = TextureWrapMode.Clamp,
            };
            texture.SetPixels32(pixels);
            texture.Apply(false, true);
            sprite = Sprite.Create(texture, new Rect(0f, 0f, Size, Size),
                                   new Vector2(0.5f, 0.5f), Size);
            Cache[name] = sprite;
            return sprite;
        }

        public static bool IsNative(Sprite sprite) =>
            sprite != null && NativeSprites.Contains(sprite);

        /// <summary>
        /// Uses only stable, semantically exact live assets.  A random item or
        /// pin would technically be native but would make navigation harder to
        /// understand, so roles without a clear Valheim counterpart retain the
        /// cohesive generated glyphs below.
        /// </summary>
        private static Sprite NativeIcon(string name)
        {
            switch (name)
            {
                case "crafting":
                    return ItemIcon("Hammer");
                case "trophies":
                    return ItemIcon("TrophyBoar");
                case "skills":
                    return SkillIcon(Skills.SkillType.Crafting);
                case "weight":
                    return ItemIcon("BeltStrength");
                default:
                    return null;
            }
        }

        private static Sprite NativePageIcon(string name)
        {
            switch (name)
            {
                case "inventory":
                    return FindGameSprite("inventoryicon", "inventory", "backpack", "bag")
                           ?? ItemIcon("BeltStrength")
                           ?? ItemIcon("Coins");
                case "crafting":
                    return ItemIcon("Hammer")
                           ?? FindGameSprite("craftingicon", "crafting", "build");
                case "map":
                    return FindGameSprite("mapicon", "minimap", "map")
                           ?? MapIcon();
                case "skills":
                    return SkillIcon(Skills.SkillType.Crafting)
                           ?? FindGameSprite("skillsicon", "skills", "skill");
                case "trophies":
                    return ItemIcon("TrophyBoar")
                           ?? FindGameSprite("trophyicon", "trophies", "trophy");
                case "chat":
                    return FindGameSprite("chaticon", "chat", "message", "speech", "talk")
                           ?? (Minimap.instance != null ? Minimap.instance.m_pingIcon : null);
                case "settings":
                    return FindGameSprite("settingsicon", "settings", "options", "gear")
                           ?? ItemIcon("Wishbone")
                           ?? ItemIcon("DvergerCirclet");
                default:
                    return null;
            }
        }

        private static Sprite MapIcon()
        {
            var minimap = Minimap.instance;
            if (minimap == null) return null;
            if (minimap.m_selectedIcon0 != null && minimap.m_selectedIcon0.sprite != null)
                return minimap.m_selectedIcon0.sprite;
            return minimap.m_pingIcon;
        }

        /// <summary>Finds a compact loaded game sprite by semantic asset name.</summary>
        private static Sprite FindGameSprite(params string[] terms)
        {
            Sprite best = null;
            int bestScore = int.MinValue;
            foreach (Sprite candidate in Resources.FindObjectsOfTypeAll<Sprite>())
            {
                if (candidate == null || candidate.texture == null) continue;
                string value = Normalise(candidate.name);
                string texture = Normalise(candidate.texture.name);
                if (value.StartsWith("secondscreen")
                    || texture.StartsWith("secondscreen")) continue;

                int score = int.MinValue;
                foreach (string term in terms)
                {
                    string wanted = Normalise(term);
                    if (value == wanted) score = Mathf.Max(score, 220);
                    else if (value.StartsWith(wanted) || value.EndsWith(wanted))
                        score = Mathf.Max(score, 150);
                    else if (value.Contains(wanted)) score = Mathf.Max(score, 90);
                }
                if (score == int.MinValue) continue;
                if (value.Contains("background") || value.Contains("frame")
                    || value.Contains("button") || value.Contains("panel")
                    || value.Contains("mask") || value.Contains("glow")) score -= 100;

                Rect rect = candidate.rect;
                float small = Mathf.Min(rect.width, rect.height);
                float large = Mathf.Max(rect.width, rect.height);
                if (small < 12f || large <= 0f || small / large < 0.55f) continue;
                if (large <= 192f) score += 25;
                else if (large > 512f) score -= 70;
                if (score <= bestScore) continue;
                best = candidate;
                bestScore = score;
            }
            return bestScore >= 70 ? best : null;
        }

        private static string Normalise(string value) =>
            (value ?? "").ToLowerInvariant().Replace("_", "").Replace(" ", "");

        private static Sprite ItemIcon(string prefabName)
        {
            var database = ObjectDB.instance;
            if (database == null) return null;
            var prefab = database.GetItemPrefab(prefabName);
            var drop = prefab != null ? prefab.GetComponent<ItemDrop>() : null;
            return drop != null ? drop.m_itemData.GetIcon() : null;
        }

        private static Sprite SkillIcon(Skills.SkillType type)
        {
            var player = Player.m_localPlayer;
            var skills = player != null ? player.GetSkills() : null;
            if (skills == null || skills.m_skills == null) return null;
            foreach (var definition in skills.m_skills)
                if (definition != null && definition.m_skill == type)
                    return definition.m_icon;
            return null;
        }

        private static void Rect(Action<int, int> dot, int x0, int y0, int x1, int y1)
        {
            Line(dot, x0, y0, x1, y0);
            Line(dot, x1, y0, x1, y1);
            Line(dot, x1, y1, x0, y1);
            Line(dot, x0, y1, x0, y0);
        }

        private static void Circle(Action<int, int> dot, int cx, int cy, int radius) =>
            Arc(dot, cx, cy, radius, 0f, 360f);

        private static void Arc(Action<int, int> dot, int cx, int cy, int radius,
                                float start, float end)
        {
            int steps = Mathf.Max(12, Mathf.CeilToInt((end - start) * radius / 28f));
            int lastX = cx + Mathf.RoundToInt(Mathf.Cos(start * Mathf.Deg2Rad) * radius);
            int lastY = cy + Mathf.RoundToInt(Mathf.Sin(start * Mathf.Deg2Rad) * radius);
            for (int i = 1; i <= steps; i++)
            {
                float a = Mathf.Lerp(start, end, i / (float)steps) * Mathf.Deg2Rad;
                int x = cx + Mathf.RoundToInt(Mathf.Cos(a) * radius);
                int y = cy + Mathf.RoundToInt(Mathf.Sin(a) * radius);
                Line(dot, lastX, lastY, x, y);
                lastX = x;
                lastY = y;
            }
        }

        private static void Line(Action<int, int> dot, int x0, int y0, int x1, int y1)
        {
            int dx = Math.Abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            int dy = -Math.Abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int error = dx + dy;
            while (true)
            {
                dot(x0, y0);
                if (x0 == x1 && y0 == y1) break;
                int twice = error * 2;
                if (twice >= dy) { error += dy; x0 += sx; }
                if (twice <= dx) { error += dx; y0 += sy; }
            }
        }

        private static void Disc(Color32[] pixels, int cx, int cy, int radius, Color32 colour)
        {
            for (int y = -radius; y <= radius; y++)
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y <= radius * radius)
                    Set(pixels, cx + x, cy + y, colour);
            }
        }

        private static void FilledCircle(Color32[] pixels, int cx, int cy, int radius,
                                         Color32 colour) =>
            Disc(pixels, cx, cy, radius, colour);

        private static void Set(Color32[] pixels, int x, int y, Color32 colour)
        {
            if (x >= 0 && y >= 0 && x < Size && y < Size)
                pixels[y * Size + x] = colour;
        }
    }
}
