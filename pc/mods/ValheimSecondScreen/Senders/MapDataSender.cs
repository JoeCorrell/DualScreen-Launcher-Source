using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.UI;
using ValheimSecondScreen.Hooks;
using ValheimSecondScreen.Network;

namespace ValheimSecondScreen.Senders
{
    public sealed class MapDataSender : MonoBehaviour
    {
        public static MapDataSender Instance { get; private set; }

        private const float PLAYER_INTERVAL    = 0.10f;
        private const float HOTBAR_INTERVAL    = 0.50f;
        private const float FOG_DEBOUNCE       = 3.0f;
        private const float FOG_FORCE          = 30.0f;
        private const float STATUS_INTERVAL    = 1.0f;
        private const float OTHERS_INTERVAL    = 2.0f;
        private const float INVENTORY_INTERVAL = 2.0f;

        private float _playerTimer, _hotbarTimer, _fogTimer, _fogForceTimer;
        private float _statusTimer, _othersTimer, _inventoryTimer;
        private bool _minimapWasReady;

        private static readonly FieldInfo _fMapTexture     = AccessTools.Field(typeof(Minimap), "m_mapTexture");
        private static readonly FieldInfo _fForestMask     = AccessTools.Field(typeof(Minimap), "m_forestMaskTexture");
        private static readonly FieldInfo _fHeightTex      = AccessTools.Field(typeof(Minimap), "m_heightTexture");
        private static readonly FieldInfo _fExplored       = AccessTools.Field(typeof(Minimap), "m_explored");
        private static readonly FieldInfo _fExploredOthers = AccessTools.Field(typeof(Minimap), "m_exploredOthers");
        private static readonly FieldInfo _fPins           = AccessTools.Field(typeof(Minimap), "m_pins");
        private static readonly FieldInfo _fHasGenerated   = AccessTools.Field(typeof(Minimap), "m_hasGenerated");

        private readonly HashSet<WebSocketClient> _pending = new HashSet<WebSocketClient>();
        private readonly Dictionary<WebSocketClient, HashSet<string>> _sentIcons = new Dictionary<WebSocketClient, HashSet<string>>();

        private Texture2D _fogTex;
        private bool _texturesSent;
        private bool _nativeOutputSuppressed;

        private void Awake() { Instance = this; }
        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
            if (_fogTex != null) Destroy(_fogTex);
        }

        private void Update()
        {
            /*
             * With a real second Unity display Android already has the final
             * image. Keep the WebSocket connected because it carries touch
             * commands into the game, but do not also build a legacy copy of
             * that image and its state.
             *
             * BuildFogMessage alone allocates four million Color32s, uploads a
             * texture, compresses it to PNG and Base64-encodes it on Unity's
             * main thread. Doing that for a hidden consumer was the largest
             * periodic frame-time spike in the native-panel path.
             */
            if (SecondDisplayMover.SecondDisplayLive)
            {
                if (!_nativeOutputSuppressed)
                {
                    _nativeOutputSuppressed = true;
                    ResetLegacyOutputState();
                    Plugin.Log.LogInfo(
                        "[SecondScreen] Native panel active; socket is input-only.");
                }
                return;
            }

            if (_nativeOutputSuppressed)
            {
                _nativeOutputSuppressed = false;
                Plugin.Log.LogInfo(
                    "[SecondScreen] Native panel unavailable; legacy output resumed.");
                foreach (var client in Plugin.Instance.Server.GetAllClients())
                    SendInitialData(client);
            }

            bool ready = Minimap.instance != null;
            if (ready != _minimapWasReady)
            {
                _minimapWasReady = ready;
                if (ready && Plugin.Instance.Server.HasClients())
                    foreach (var c in Plugin.Instance.Server.GetAllClients()) SendInitialData(c);
                if (!ready) { _texturesSent = false; _sentIcons.Clear(); }
            }

            if (!Plugin.Instance.Server.HasClients()) return;
            if (Minimap.instance == null || !IsMapReady(Minimap.instance)) return;
            SendPendingInitial();
            if (Player.m_localPlayer == null) return;

            float dt = Time.unscaledDeltaTime;

            _playerTimer += dt;
            if (_playerTimer >= PLAYER_INTERVAL) { _playerTimer = 0f; Broadcast(BuildPlayerState()); }

            _hotbarTimer += dt;
            if (_hotbarTimer >= HOTBAR_INTERVAL) { _hotbarTimer = 0f; BroadcastHotbar(); }

            _statusTimer += dt;
            if (_statusTimer >= STATUS_INTERVAL) { _statusTimer = 0f; BroadcastStatusEffects(); }

            _othersTimer += dt;
            if (_othersTimer >= OTHERS_INTERVAL) { _othersTimer = 0f; BroadcastOtherPlayers(); }

            _inventoryTimer += dt;
            if (_inventoryTimer >= INVENTORY_INTERVAL) { _inventoryTimer = 0f; BroadcastInventory(); }

            _fogTimer += dt; _fogForceTimer += dt;
            bool dirty = MinimapHooks.ConsumeFogDirty();
            if ((dirty && _fogTimer >= FOG_DEBOUNCE) || _fogForceTimer >= FOG_FORCE)
            {
                _fogTimer = 0f; _fogForceTimer = 0f;
                var msg = BuildFogMessage(); if (msg != null) Broadcast(msg);
            }
        }

        public void SendInitialData(WebSocketClient client)
        {
            // The connected Android client still sends native touch commands.
            // It does not need a second, network-rendered copy of the panel.
            if (SecondDisplayMover.SecondDisplayLive) return;

            var map = Minimap.instance;
            if (map == null || !IsMapReady(map)) { _pending.Add(client); return; }
            _pending.Remove(client);
            Plugin.Log.LogInfo("[SecondScreen] Sending initial data...");

            Send(client, Protocol.GameInfo(ZNet.World?.m_name ?? "Unknown", ZNet.World?.m_seedName ?? ""));
            if (HudBandMover.Moved)
                Send(client, Protocol.HudBandReady(HudBandMover.SourceTop, HudBandMover.SourceBottom));
            Send(client, Protocol.MapConfig(map.m_textureSize, map.m_pixelSize));

            // Small messages first so the UI shows quickly
            try { var msg = BuildFogMessage(); if (msg != null) Send(client, msg); } catch (Exception e) { Warn("fog", e); }
            try { if (Player.m_localPlayer != null) Send(client, BuildPlayerState()); } catch (Exception e) { Warn("player", e); }
            try { SendHotbarTo(client); } catch (Exception e) { Warn("hotbar", e); }
            try { var p = (List<Minimap.PinData>)_fPins.GetValue(map); if (p != null) Send(client, Protocol.PinsFull(p)); } catch (Exception e) { Warn("pins", e); }

            // Large static textures (terrain, forest mask, height map)
            try { SendTexture(client, "terrain",  _fMapTexture, map, Protocol.MapTexture); } catch (Exception e) { Warn("terrain", e); }
            try { SendTexture(client, "forest",   _fForestMask, map, Protocol.ForestTexture); } catch (Exception e) { Warn("forest", e); }
            try { SendHeightTexture(client, map); } catch (Exception e) { Warn("height", e); }
            try { SendMapIconsTo(client, map); } catch (Exception e) { Warn("icons", e); }
            try { SendUiSpritesTo(client, map); } catch (Exception e) { Warn("ui", e); }

            _texturesSent = true;
            Plugin.Log.LogInfo("[SecondScreen] Initial data complete.");
        }

        private void ResetLegacyOutputState()
        {
            _pending.Clear();
            _sentIcons.Clear();
            _texturesSent = false;
            _playerTimer = _hotbarTimer = _fogTimer = _fogForceTimer = 0f;
            _statusTimer = _othersTimer = _inventoryTimer = 0f;
        }

        // ── Texture sending ─────────────────────────────────────────────────────

        private void SendTexture(WebSocketClient client, string label, FieldInfo field, Minimap map, Func<byte[], string> proto)
        {
            var tex = field != null ? (Texture2D)field.GetValue(map) : null;
            if (tex == null) { Plugin.Log.LogWarning($"[SecondScreen] {label} texture null"); return; }
            byte[] png = EncodeTex(tex);
            if (png != null && png.Length > 0)
            {
                Send(client, proto(png));
                Plugin.Log.LogInfo($"[SecondScreen] Sent {label} ({png.Length / 1024} KB).");
            }
        }

        private void SendHeightTexture(WebSocketClient client, Minimap map)
        {
            var tex = _fHeightTex != null ? (Texture2D)_fHeightTex.GetValue(map) : null;
            if (tex == null) return;

            // RHalf format needs special readback → render to ARGB32, read R channel
            int size = tex.width;
            var prev = RenderTexture.active;
            RenderTexture rt = null;
            Texture2D tmp = null;
            byte[] png;
            try
            {
                rt = RenderTexture.GetTemporary(size, size, 0,
                                                RenderTextureFormat.ARGB32);
                Graphics.Blit(tex, rt);
                RenderTexture.active = rt;
                tmp = new Texture2D(size, size, TextureFormat.RGB24, false);
                tmp.ReadPixels(new Rect(0, 0, size, size), 0, 0);
                tmp.Apply();
                png = tmp.EncodeToPNG();
            }
            finally
            {
                RenderTexture.active = prev;
                if (rt != null) RenderTexture.ReleaseTemporary(rt);
                if (tmp != null) Destroy(tmp);
            }

            if (png != null && png.Length > 0)
            {
                Send(client, Protocol.HeightTexture(png));
                Plugin.Log.LogInfo($"[SecondScreen] Sent height ({png.Length / 1024} KB).");
            }
        }

        // ── Fog ─────────────────────────────────────────────────────────────────

        private string BuildFogMessage()
        {
            var map = Minimap.instance;
            if (map == null) return null;
            int size = map.m_textureSize;
            var explored = (bool[])_fExplored.GetValue(map);
            var others = _fExploredOthers != null ? (bool[])_fExploredOthers.GetValue(map) : null;
            if (explored == null || explored.Length < size * size) return null;

            if (_fogTex == null || _fogTex.width != size)
            {
                if (_fogTex != null) Destroy(_fogTex);
                _fogTex = new Texture2D(size, size, TextureFormat.RGBA32, false);
                _fogTex.wrapMode = TextureWrapMode.Clamp;
            }

            bool hasOthers = others != null && others.Length >= size * size;
            int total = size * size;
            var px = new Color32[total];
            for (int i = 0; i < total; i++)
            {
                byte s = explored[i] ? (byte)255 : (byte)0;
                byte o = (hasOthers && others[i]) ? (byte)255 : (byte)0;
                px[i] = new Color32(s, o, 0, 255);
            }
            _fogTex.SetPixels32(px);
            _fogTex.Apply(false);
            return Protocol.FogTexture(_fogTex.EncodeToPNG());
        }

        // ── Helpers ─────────────────────────────────────────────────────────────

        private static bool IsMapReady(Minimap map)
        {
            if (_fMapTexture == null || _fExplored == null || _fPins == null) return false;
            try
            {
                if (_fHasGenerated != null && !(bool)_fHasGenerated.GetValue(map)) return false;
                var t = _fMapTexture.GetValue(map) as Texture2D;
                var e = _fExplored.GetValue(map) as bool[];
                return t != null && t.width > 0 && e != null && e.Length >= map.m_textureSize * map.m_textureSize;
            }
            catch { return false; }
        }

        private void SendPendingInitial()
        {
            if (_pending.Count == 0) return;
            var live = new HashSet<WebSocketClient>(Plugin.Instance.Server.GetAllClients());
            _pending.RemoveWhere(c => !live.Contains(c));
            foreach (var c in new List<WebSocketClient>(_pending)) SendInitialData(c);
        }

        private static void Send(WebSocketClient c, string msg) => Plugin.Instance.Server.SendTo(c, msg);
        private static void Broadcast(string msg) => Plugin.Instance.Server.Broadcast(msg);
        private static void Warn(string label, Exception e) => Plugin.Log.LogWarning($"[SecondScreen] {label}: {e.Message}");

        private string BuildPlayerState()
        {
            var p = Player.m_localPlayer;
            return Protocol.PlayerState(p.transform.position, p.transform.rotation.eulerAngles.y,
                p.GetHealth(), p.GetMaxHealth(), p.GetStamina(), p.GetMaxStamina(), p.GetPlayerName());
        }

        private void BroadcastStatusEffects()
        {
            var p = Player.m_localPlayer; if (p == null) return;
            var seman = p.GetSEMan(); if (seman == null) return;
            var effects = seman.GetStatusEffects();
            if (effects == null) return;

            foreach (var c in Plugin.Instance.Server.GetAllClients())
            {
                if (!_sentIcons.TryGetValue(c, out var sent)) { sent = new HashSet<string>(); _sentIcons[c] = sent; }
                foreach (var se in effects)
                {
                    if (se?.m_icon == null) continue;
                    string key = "__se:" + se.m_name;
                    if (sent.Add(key)) SendIcon(c, key, se.m_icon);
                }
            }

            Broadcast(Protocol.StatusEffects(effects));
        }

        private void BroadcastOtherPlayers()
        {
            var local = Player.m_localPlayer; if (local == null) return;
            var all = Player.GetAllPlayers();
            Broadcast(Protocol.OtherPlayers(all, local));
        }

        private void BroadcastInventory()
        {
            var p = Player.m_localPlayer; if (p == null) return;
            var inv = p.GetInventory(); if (inv == null) return;

            foreach (var c in Plugin.Instance.Server.GetAllClients())
            {
                if (!_sentIcons.TryGetValue(c, out var sent)) { sent = new HashSet<string>(); _sentIcons[c] = sent; }
                foreach (var item in inv.GetAllItems())
                {
                    if (item == null) continue;
                    string key = Protocol.InventoryIconKey(item);
                    if (sent.Add(key)) SendIcon(c, key, item.GetIcon());
                }
            }

            Broadcast(Protocol.FullInventory(inv));
        }

        private void BroadcastHotbar()
        {
            var p = Player.m_localPlayer; if (p == null) return;
            var inv = p.GetInventory(); if (inv == null) return;
            var clients = Plugin.Instance.Server.GetAllClients();
            foreach (var c in new List<WebSocketClient>(_sentIcons.Keys))
                if (!clients.Contains(c)) _sentIcons.Remove(c);
            foreach (var c in clients) SendHotbarIcons(c, inv);
            Broadcast(Protocol.Hotbar(inv));
        }

        private void SendHotbarTo(WebSocketClient client)
        {
            var p = Player.m_localPlayer; if (p == null) return;
            var inv = p.GetInventory(); if (inv == null) return;
            SendHotbarIcons(client, inv);
            Send(client, Protocol.Hotbar(inv));
        }

        private void SendHotbarIcons(WebSocketClient c, Inventory inv)
        {
            if (!_sentIcons.TryGetValue(c, out var sent)) { sent = new HashSet<string>(); _sentIcons[c] = sent; }
            int count = Math.Min(8, inv.GetWidth());
            for (int i = 0; i < count; i++)
            {
                var item = inv.GetItemAt(i, 0); if (item == null) continue;
                string key = Protocol.HotbarIconKey(item);
                if (sent.Add(key)) SendIcon(c, key, item.GetIcon());
            }
        }

        private void SendMapIconsTo(WebSocketClient c, Minimap map)
        {
            foreach (var icon in map.m_icons) SendIcon(c, icon.m_name.ToString(), icon.m_icon);
            SendIcon(c, "__ping", map.m_pingIcon);
            var img = map.m_largeMarker?.GetComponent<Image>();
            SendIcon(c, "__player", img != null ? img.sprite : null);
        }

        /// <summary>
        /// Ships the game's own HUD art, so the second screen does not have to
        /// impersonate it.
        /// </summary>
        /// <remarks>
        /// Everything the other side draws a panel, a slot or a bar with used to be
        /// a PNG someone had pulled out of the game by hand and checked into the
        /// Android app. That is a copy that goes stale on the next patch, ignores
        /// whatever texture pack is installed, and — because a flat PNG has no
        /// nine-slice border — gets stretched out of shape the moment it is drawn
        /// at a size other than the one it was captured at.
        ///
        /// Collected by walking the live HUD rather than by naming fields. Valheim
        /// re-parents and renames these objects between patches, but the sprites
        /// keep their names, so a sweep for distinct <see cref="Image.sprite"/> is
        /// both simpler and more durable than a list of reflection paths. Roughly
        /// forty sprites, sent once per connection alongside the terrain.
        /// </remarks>
        private static void SendUiSpritesTo(WebSocketClient c, Minimap map)
        {
            var seen = new HashSet<string>();
            var roots = new List<GameObject>();

            if (Hud.instance != null) roots.Add(Hud.instance.gameObject);
            if (map != null) roots.Add(map.gameObject);
            if (InventoryGui.instance != null) roots.Add(InventoryGui.instance.gameObject);

            foreach (var root in roots)
            {
                if (root == null) continue;
                // Inactive ones included: the inventory and the crafting panel are
                // both switched off until opened, and they own most of the frames
                // the other side needs.
                var images = root.GetComponentsInChildren<Image>(true);
                foreach (var image in images)
                {
                    var sprite = image != null ? image.sprite : null;
                    if (sprite == null) continue;

                    var name = sprite.name;
                    if (string.IsNullOrEmpty(name) || !seen.Add(name)) continue;

                    byte[] png = null;
                    try { png = EncodeSprite(sprite); } catch { }
                    if (png == null || png.Length == 0) continue;

                    Send(c, Protocol.UiSprite(name, png, sprite.border, sprite.pixelsPerUnit));
                }
            }

            Plugin.Log.LogInfo("[SecondScreen] Sent " + seen.Count + " HUD sprites.");
        }

        private static void SendIcon(WebSocketClient c, string type, Sprite sprite)
        {
            if (sprite == null || string.IsNullOrEmpty(type)) return;
            byte[] png = EncodeSprite(sprite);
            if (png != null && png.Length > 0) Send(c, Protocol.MapIcon(type, png));
        }

        internal static byte[] EncodeSpritePublic(Sprite sprite) => EncodeSprite(sprite);

        private static byte[] EncodeSprite(Sprite sprite)
        {
            var texture = sprite.texture; if (texture == null) return null;
            var rect = sprite.textureRect;
            int w = Mathf.RoundToInt(rect.width), h = Mathf.RoundToInt(rect.height);
            if (w <= 0 || h <= 0) return null;
            Texture2D crop = null; RenderTexture rt = null; var prev = RenderTexture.active;
            try
            {
                crop = new Texture2D(w, h, TextureFormat.RGBA32, false);
                if (texture.isReadable) { crop.SetPixels(texture.GetPixels(Mathf.RoundToInt(rect.x), Mathf.RoundToInt(rect.y), w, h)); crop.Apply(false); }
                else
                {
                    rt = RenderTexture.GetTemporary(w, h, 0, RenderTextureFormat.ARGB32);
                    Graphics.Blit(texture, rt, new Vector2(rect.width / texture.width, rect.height / texture.height), new Vector2(rect.x / texture.width, rect.y / texture.height));
                    RenderTexture.active = rt; crop.ReadPixels(new Rect(0, 0, w, h), 0, 0); crop.Apply(false);
                }
                return crop.EncodeToPNG();
            }
            finally { RenderTexture.active = prev; if (rt != null) RenderTexture.ReleaseTemporary(rt); if (crop != null) Destroy(crop); }
        }

        private static byte[] EncodeTex(Texture2D tex)
        {
            if (tex.isReadable) { try { var d = tex.EncodeToPNG(); if (d?.Length > 0) return d; } catch { } }
            RenderTexture rt = null;
            Texture2D tmp = null;
            var prev = RenderTexture.active;
            try
            {
                rt = RenderTexture.GetTemporary(tex.width, tex.height, 0,
                                                RenderTextureFormat.ARGB32);
                Graphics.Blit(tex, rt);
                RenderTexture.active = rt;
                tmp = new Texture2D(tex.width, tex.height, TextureFormat.RGB24, false);
                tmp.ReadPixels(new Rect(0, 0, tex.width, tex.height), 0, 0); tmp.Apply();
                return tmp.EncodeToPNG();
            }
            catch { return null; }
            finally
            {
                RenderTexture.active = prev;
                if (rt != null) RenderTexture.ReleaseTemporary(rt);
                if (tmp != null) Destroy(tmp);
            }
        }
    }
}
