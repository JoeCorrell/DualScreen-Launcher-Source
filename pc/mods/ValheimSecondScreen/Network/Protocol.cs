using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace ValheimSecondScreen.Network
{
    /// <summary>
    /// Builds JSON message strings for the Second Screen protocol.
    ///
    /// Protocol message types:
    ///   game_info    — sent once on connect: world name, seed
    ///   map_config   — sent once: texture size and pixel size (world units/pixel)
    ///   map_texture  — static terrain PNG (base64), sent once per world
    ///   fog_texture  — explored-areas PNG (base64), sent periodically
    ///   player_state — position, heading, health, stamina at 10 Hz
    ///   pins_full    — complete list of map pins
    ///   pin_added    — single pin was added
    ///   pin_removed  — single pin was removed (by stable ID)
    ///   map_ping     — ephemeral map ping from a player
    ///
    /// All floats use InvariantCulture (decimal point, not comma).
    /// Strings are JSON-escaped. No third-party JSON library required.
    /// </summary>
    public static class Protocol
    {
        public static string HudBandReady(int srcTop, int srcBottom) =>
            Obj(
                KV("type", "hud_band_ready"),
                KVI("srcTop", srcTop),
                KVI("srcBottom", srcBottom)
            );

        public static string GameInfo(string worldName, string worldSeed) =>
            Obj(
                KV("type",      "game_info"),
                KV("gameId",    "valheim"),
                KV("gameName",  "Valheim"),
                KV("worldName", worldName),
                KV("worldSeed", worldSeed)
            );

        public static string MapConfig(int textureSize, float pixelSize) =>
            Obj(
                KV("type",        "map_config"),
                KVI("textureSize", textureSize),
                KVF("pixelSize",   pixelSize)
            );

        public static string MapTexture(byte[] pngBytes) =>
            Obj(
                KV("type", "map_texture"),
                KV("data", Convert.ToBase64String(pngBytes))
            );

        public static string FogTexture(byte[] pngBytes) =>
            Obj(
                KV("type", "fog_texture"),
                KV("data", Convert.ToBase64String(pngBytes))
            );

        public static string ForestTexture(byte[] pngBytes) =>
            Obj(
                KV("type", "forest_texture"),
                KV("data", Convert.ToBase64String(pngBytes))
            );

        public static string HeightTexture(byte[] pngBytes) =>
            Obj(
                KV("type", "height_texture"),
                KV("data", Convert.ToBase64String(pngBytes))
            );

        public static string MapIcon(string iconType, byte[] pngBytes) =>
            Obj(
                KV("type",     "map_icon"),
                KV("iconType", iconType),
                KV("data",     Convert.ToBase64String(pngBytes))
            );

        /// <summary>
        /// One sprite from the game's own HUD, with the geometry needed to draw it.
        /// </summary>
        /// <remarks>
        /// The border is the reason this is not just another map_icon. Valheim's
        /// panels and slot backgrounds are nine-sliced: the four numbers say how
        /// many pixels at each edge must never stretch, so the carved frame keeps
        /// its corners at any size. Sent as pixels rather than replicated on the
        /// other side, which never looks the same and drifts every time the game
        /// is patched — and which cannot pick up a texture pack at all.
        /// </remarks>
        public static string UiSprite(string key, byte[] pngBytes, Vector4 border, float ppu) =>
            Obj(
                KV("type",          "ui_sprite"),
                KV("key",           key),
                KVF("borderLeft",   border.x),
                KVF("borderBottom", border.y),
                KVF("borderRight",  border.z),
                KVF("borderTop",    border.w),
                KVF("ppu",          ppu),
                KV("data",          Convert.ToBase64String(pngBytes))
            );

        public static string Hotbar(Inventory inventory)
        {
            var sb = new StringBuilder(1024);
            sb.Append("{\"type\":\"hotbar\",\"slots\":[");
            int count = Math.Min(8, inventory.GetWidth());
            for (int slot = 0; slot < count; slot++)
            {
                if (slot > 0) sb.Append(',');
                var item = inventory.GetItemAt(slot, 0);
                sb.Append('{');
                sb.Append("\"slot\":").Append(slot + 1);
                if (item != null)
                {
                    string displayName = Localization.instance?.Localize(item.m_shared.m_name)
                        ?? item.m_shared.m_name;
                    sb.Append(",\"name\":"); AppendStr(sb, displayName);
                    sb.Append(",\"stack\":").Append(item.m_stack);
                    sb.Append(",\"equipped\":").Append(item.m_equipped ? "true" : "false");
                    sb.Append(",\"iconKey\":"); AppendStr(sb, HotbarIconKey(item));
                }
                sb.Append('}');
            }
            sb.Append("]}");
            return sb.ToString();
        }

        public static string HotbarIconKey(ItemDrop.ItemData item) =>
            "__hotbar:" + item.m_shared.m_name + ":" + item.m_variant;

        public static string PlayerState(
            Vector3 pos, float yAngle,
            float health, float maxHealth,
            float stamina, float maxStamina,
            string playerName) =>
            Obj(
                KV("type",       "player_state"),
                KVF("x",          pos.x),
                KVF("y",          pos.y),
                KVF("z",          pos.z),
                KVF("angle",      yAngle),
                KVF("health",     health),
                KVF("maxHealth",  maxHealth),
                KVF("stamina",    stamina),
                KVF("maxStamina", maxStamina),
                KV("playerName", playerName)
            );

        public static string PinsFull(List<Minimap.PinData> pins)
        {
            var sb = new StringBuilder(1024);
            sb.Append("{\"type\":\"pins_full\",\"pins\":[");
            bool first = true;
            foreach (var pin in pins)
            {
                // Skip ephemeral runtime-only pins (pings, other player positions)
                if (pin.m_type == Minimap.PinType.Ping || pin.m_type == Minimap.PinType.Player)
                    continue;
                if (!first) sb.Append(',');
                AppendPin(sb, pin);
                first = false;
            }
            sb.Append("]}");
            return sb.ToString();
        }

        public static string PinAdded(Minimap.PinData pin)
        {
            var sb = new StringBuilder(256);
            sb.Append("{\"type\":\"pin_added\",\"pin\":");
            AppendPin(sb, pin);
            sb.Append('}');
            return sb.ToString();
        }

        public static string PinRemoved(Minimap.PinData pin) =>
            Obj(
                KV("type", "pin_removed"),
                KV("id",   PinStableId(pin))
            );

        public static string MapPing(Vector3 pos, string senderName) =>
            Obj(
                KV("type",   "map_ping"),
                KVF("x",     pos.x),
                KVF("y",     pos.y),
                KVF("z",     pos.z),
                KV("sender", senderName)
            );

        public static string StatusEffects(List<StatusEffect> effects)
        {
            var sb = new StringBuilder(512);
            sb.Append("{\"type\":\"status_effects\",\"effects\":[");
            bool first = true;
            foreach (var se in effects)
            {
                if (se == null) continue;
                if (!first) sb.Append(',');
                sb.Append('{');
                sb.Append("\"name\":"); AppendStr(sb, se.m_name); sb.Append(',');
                string display = Localization.instance?.Localize(se.m_name) ?? se.m_name;
                sb.Append("\"displayName\":"); AppendStr(sb, display); sb.Append(',');
                sb.Append("\"iconKey\":"); AppendStr(sb, "__se:" + se.m_name); sb.Append(',');
                sb.Append("\"remaining\":").Append(F(se.GetRemaningTime())); sb.Append(',');
                sb.Append("\"ttl\":").Append(F(se.m_ttl));
                sb.Append('}');
                first = false;
            }
            sb.Append("]}");
            return sb.ToString();
        }

        public static string OtherPlayers(List<Player> allPlayers, Player localPlayer)
        {
            var sb = new StringBuilder(256);
            sb.Append("{\"type\":\"other_players\",\"players\":[");
            bool first = true;
            foreach (var p in allPlayers)
            {
                if (p == null || p == localPlayer) continue;
                if (!first) sb.Append(',');
                var pos = p.transform.position;
                sb.Append('{');
                sb.Append("\"name\":"); AppendStr(sb, p.GetPlayerName()); sb.Append(',');
                sb.Append("\"x\":").Append(F(pos.x)); sb.Append(',');
                sb.Append("\"z\":").Append(F(pos.z)); sb.Append(',');
                sb.Append("\"health\":").Append(F(p.GetHealth())); sb.Append(',');
                sb.Append("\"maxHealth\":").Append(F(p.GetMaxHealth()));
                sb.Append('}');
                first = false;
            }
            sb.Append("]}");
            return sb.ToString();
        }

        public static string FullInventory(Inventory inventory)
        {
            var sb = new StringBuilder(2048);
            sb.Append("{\"type\":\"inventory\",");
            sb.Append("\"width\":").Append(inventory.GetWidth()).Append(',');
            sb.Append("\"height\":").Append(inventory.GetHeight()).Append(',');
            sb.Append("\"items\":[");
            var items = inventory.GetAllItems();
            bool first = true;
            foreach (var item in items)
            {
                if (item == null) continue;
                if (!first) sb.Append(',');
                string displayName = Localization.instance?.Localize(item.m_shared.m_name)
                    ?? item.m_shared.m_name;
                sb.Append('{');
                sb.Append("\"x\":").Append(item.m_gridPos.x); sb.Append(',');
                sb.Append("\"y\":").Append(item.m_gridPos.y); sb.Append(',');
                sb.Append("\"name\":"); AppendStr(sb, displayName); sb.Append(',');
                sb.Append("\"stack\":").Append(item.m_stack); sb.Append(',');
                sb.Append("\"maxStack\":").Append(item.m_shared.m_maxStackSize); sb.Append(',');
                sb.Append("\"equipped\":").Append(item.m_equipped ? "true" : "false"); sb.Append(',');
                sb.Append("\"quality\":").Append(item.m_quality); sb.Append(',');
                sb.Append("\"durability\":").Append(F(item.m_durability)); sb.Append(',');
                sb.Append("\"maxDurability\":").Append(F(item.m_shared.m_maxDurability)); sb.Append(',');
                sb.Append("\"iconKey\":"); AppendStr(sb, InventoryIconKey(item));
                sb.Append('}');
                first = false;
            }
            sb.Append("]}");
            return sb.ToString();
        }

        public static string InventoryIconKey(ItemDrop.ItemData item) =>
            "__inv:" + item.m_shared.m_name + ":" + item.m_variant;

        // ── Helpers ────────────────────────────────────────────────────────────

        /// <summary>
        /// Stable, deterministic ID for a pin: type + rounded world coords.
        /// Used by the Android client to match add/remove events.
        /// </summary>
        public static string PinStableId(Minimap.PinData pin) =>
            pin.m_type + "_" + Mathf.RoundToInt(pin.m_pos.x) + "_" + Mathf.RoundToInt(pin.m_pos.z);

        private static void AppendPin(StringBuilder sb, Minimap.PinData pin)
        {
            sb.Append('{');
            sb.Append("\"id\":"      ); AppendStr(sb, PinStableId(pin)); sb.Append(',');
            sb.Append("\"name\":"    ); AppendStr(sb, pin.m_name);        sb.Append(',');
            sb.Append("\"type\":"    ); AppendStr(sb, pin.m_type.ToString()); sb.Append(',');
            sb.Append("\"x\":"       ); sb.Append(F(pin.m_pos.x));        sb.Append(',');
            sb.Append("\"y\":"       ); sb.Append(F(pin.m_pos.y));        sb.Append(',');
            sb.Append("\"z\":"       ); sb.Append(F(pin.m_pos.z));        sb.Append(',');
            sb.Append("\"checked\":"); sb.Append(pin.m_checked ? "true" : "false"); sb.Append(',');
            sb.Append("\"save\":"   ); sb.Append(pin.m_save    ? "true" : "false");
            sb.Append('}');
        }

        // ── Micro JSON builder helpers ─────────────────────────────────────────

        private static string Obj(params string[] kvPairs) =>
            "{" + string.Join(",", kvPairs) + "}";

        private static string KV(string key, string value)
        {
            var sb = new StringBuilder();
            AppendStr(sb, key);
            sb.Append(':');
            AppendStr(sb, value);
            return sb.ToString();
        }

        private static string KVF(string key, float value)
        {
            var sb = new StringBuilder();
            AppendStr(sb, key);
            sb.Append(':');
            sb.Append(F(value));
            return sb.ToString();
        }

        private static string KVI(string key, int value)
        {
            var sb = new StringBuilder();
            AppendStr(sb, key);
            sb.Append(':');
            sb.Append(value);
            return sb.ToString();
        }

        private static string F(float v) =>
            v.ToString("F2", CultureInfo.InvariantCulture);

        private static void AppendStr(StringBuilder sb, string s)
        {
            sb.Append('"');
            if (s != null)
            {
                foreach (char c in s)
                {
                    switch (c)
                    {
                        case '"':  sb.Append("\\\""); break;
                        case '\\': sb.Append("\\\\"); break;
                        case '\n': sb.Append("\\n");  break;
                        case '\r': sb.Append("\\r");  break;
                        case '\t': sb.Append("\\t");  break;
                        default:
                            if (c < ' ')
                                sb.Append("\\u").Append(((int)c).ToString("x4"));
                            else
                                sb.Append(c);
                            break;
                    }
                }
            }
            sb.Append('"');
        }
    }
}
