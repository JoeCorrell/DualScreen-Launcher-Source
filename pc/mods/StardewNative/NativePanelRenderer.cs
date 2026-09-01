using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using StardewValley;

namespace StardewSecondScreen
{
    /// <summary>
    /// Stardew-styled renderer for the private lower-panel guest window.
    ///
    /// It deliberately consumes the same messages and sprite ids as the former
    /// Android view. That makes page contents, game-derived art, and mutations a
    /// shared contract instead of a second interpretation of the save.
    /// </summary>
    internal sealed class NativePanelRenderer : IDisposable
    {
        private const int Width = NativePanelWindow.LogicalWidth;
        private const int Height = NativePanelWindow.LogicalHeight;
        // Compose rendered its dp-sized controls at the Thor panel's 369 dpi.
        // Author the native chrome at the equivalent physical size instead of
        // treating those dp values as one-for-one render-target pixels.
        private const int HeaderHeight = 96;
        private const int Outer = 12;
        private const int ContentTop = Outer + HeaderHeight + 6;
        private const int ContentBottom = Height - Outer;
        private const int VisibleTabs = 3;
        private const int TextureCacheLimit = 512;
        private const float TextDisplayScale = 1.65f;

        private static readonly Color Ink = new(0x32, 0x1B, 0x0F);
        private static readonly Color MutedInk = new(0x72, 0x4B, 0x2C);
        private static readonly Color Parchment = new(0xF6, 0xE5, 0xAD);
        private static readonly Color ParchmentLight = new(0xFF, 0xF2, 0xC7);
        private static readonly Color ParchmentWash = new(0xE8, 0xCE, 0x91);
        private static readonly Color Border = new(0xA4, 0x6D, 0x35);
        private static readonly Color Line = new(0xC2, 0x94, 0x55);
        private static readonly Color Cursor = new(0x2E, 0x91, 0x45);
        private static readonly Color Error = new(0xB8, 0x32, 0x2F);
        private static readonly Color Ready = new(0xD7, 0xA8, 0x2D);

        private static readonly PanelPage[] Pages =
        {
            new("Backpack", "ui:tab_backpack"),
            new("Crafting", "ui:tab_crafting"),
            new("Progress", "ui:coin"),
            new("Map", "ui:tab_map"),
            new("Journal", "ui:tab_journal"),
            new("Relationships", "ui:tab_relationships"),
            new("Gift Guide", "ui:heart"),
            new("Fields", "ui:season"),
            new("Machines", "ui:tab_crafting"),
            new("Bundles", "ui:tab_skills"),
            new("Calendar", "ui:season"),
            new("Settings", "ui:tab_settings"),
        };

        private readonly Action<string> _log;
        private readonly object _stateGate = new();
        private readonly Dictionary<string, string> _messages = new(StringComparer.Ordinal);
        private readonly Dictionary<string, SpritePayload> _spritePayloads = new(StringComparer.Ordinal);
        private readonly Dictionary<string, Texture2D> _textures = new(StringComparer.Ordinal);
        private readonly Dictionary<string, string> _textureSources = new(StringComparer.Ordinal);
        private readonly Dictionary<string, long> _textureAccess = new(StringComparer.Ordinal);
        private readonly Dictionary<string, string> _spriteFailures = new(StringComparer.Ordinal);
        private readonly List<PanelHit> _hits = new();
        private readonly int[] _scroll = new int[Pages.Length];

        private GraphicsDevice? _device;
        private RenderTarget2D? _target;
        private Texture2D? _pixel;
        private SpriteBatch? _batch;
        private Color[]? _pixels;
        private byte[]? _output;
        private int _page;
        private bool _dirty = true;
        private long _nextFrame;
        private float _pointerDownX;
        private float _pointerDownY;
        private bool _pointerDown;
        private int _renderFailureCount;
        private long _nextFailureLogAt;
        private bool _restoreFailureLogged;
        private long _textureClock;

        public NativePanelRenderer(Action<string> log) => _log = log;

        public void Accept(string payload)
        {
            if (string.IsNullOrWhiteSpace(payload)) return;
            try
            {
                using var document = JsonDocument.Parse(payload);
                var root = document.RootElement;
                var type = Text(root, "type");
                if (type.Length == 0) return;

                lock (_stateGate)
                {
                    if (type == "sdv_sprite")
                    {
                        var id = Text(root, "id");
                        var png = Text(root, "png");
                        if (id.Length == 0 || png.Length == 0) return;
                        _spritePayloads[id] = new SpritePayload(
                            png,
                            Number(root, "insetX", Number(root, "inset", 0)),
                            Number(root, "insetY", Number(root, "inset", 0)));
                    }
                    else
                    {
                        _messages[type] = payload;
                    }
                    _dirty = true;
                }
            }
            catch (JsonException failure)
            {
                _log("Native panel ignored malformed state: " + failure.Message);
            }
        }

        public void ResetWorldState()
        {
            lock (_stateGate)
            {
                _messages.Clear();
                _spritePayloads.Clear();
                _dirty = true;
                _nextFrame = 0;
            }

            foreach (var texture in _textures.Values)
                SafeDispose(texture);
            _textures.Clear();
            _textureSources.Clear();
            _textureAccess.Clear();
            _spriteFailures.Clear();
            _hits.Clear();
            System.Array.Clear(_scroll, 0, _scroll.Length);
            _page = 0;
            _pointerDown = false;
        }

        public string? HandleInput(NativePanelInput input)
        {
            switch (input.Kind)
            {
                case NativePanelInputKind.PreviousPage:
                case NativePanelInputKind.Left:
                    ChangePage(-1);
                    return null;
                case NativePanelInputKind.NextPage:
                case NativePanelInputKind.Right:
                    ChangePage(1);
                    return null;
                case NativePanelInputKind.UpKey:
                    if (_page == 0) return SelectRelativeSlot(-1);
                    ChangeScroll(-1);
                    return null;
                case NativePanelInputKind.DownKey:
                    if (_page == 0) return SelectRelativeSlot(1);
                    ChangeScroll(1);
                    return null;
                case NativePanelInputKind.Sort:
                    return _page == 0 ? "{\"type\":\"sort_bag\"}" : null;
                case NativePanelInputKind.Use:
                    return _page == 0
                        ? $"{{\"type\":\"eat_slot\",\"slot\":{SelectedSlot()}}}"
                        : null;
                case NativePanelInputKind.PreviousToolbar:
                    return "{\"type\":\"shift_toolbar\",\"value\":0}";
                case NativePanelInputKind.NextToolbar:
                    return "{\"type\":\"shift_toolbar\",\"value\":1}";
                case NativePanelInputKind.Scroll:
                    ChangeScroll(input.Value < 0 ? -1 : 1);
                    return null;
                case NativePanelInputKind.Down:
                    _pointerDown = true;
                    (_pointerDownX, _pointerDownY) = Logical(input);
                    return null;
                case NativePanelInputKind.Move:
                    return null;
                case NativePanelInputKind.Up:
                    var up = Logical(input);
                    if (_pointerDown && Math.Abs(up.x - _pointerDownX) +
                        Math.Abs(up.y - _pointerDownY) > 42f)
                    {
                        ChangeScroll(up.y > _pointerDownY ? -1 : 1);
                        _pointerDown = false;
                        return null;
                    }
                    _pointerDown = false;
                    return Activate(up.x, up.y, held: false);
                case NativePanelInputKind.Hold:
                    var held = Logical(input);
                    return Activate(held.x, held.y, held: true);
                case NativePanelInputKind.Tap:
                case NativePanelInputKind.Activate:
                    if (input.Kind == NativePanelInputKind.Activate)
                    {
                        ChangeScroll(1);
                        return null;
                    }
                    var tap = Logical(input);
                    return Activate(tap.x, tap.y, held: false);
                default:
                    return null;
            }
        }

        public void RenderIfNeeded(Action<byte[]> present)
        {
            var now = Environment.TickCount64;
            lock (_stateGate)
            {
                if (!_dirty || now < _nextFrame) return;
                _dirty = false;
                _nextFrame = now + 80;
            }

            GraphicsDevice? device = null;
            RenderTargetBinding[] previousTargets = System.Array.Empty<RenderTargetBinding>();
            Viewport previousViewport = default;
            BlendState? previousBlend = null;
            DepthStencilState? previousDepth = null;
            RasterizerState? previousRasterizer = null;
            SamplerState? previousSampler = null;
            bool stateCaptured = false;
            bool batchBegun = false;
            try
            {
                device = Game1.graphics?.GraphicsDevice;
                if (device == null || device.IsDisposed)
                    throw new InvalidOperationException("MonoGame graphics device is unavailable");
                EnsureGraphics(device);
                if (_target == null || _batch == null || _pixel == null ||
                    _pixels == null || _output == null)
                    throw new InvalidOperationException("Native panel graphics resources are incomplete");

                previousTargets = device.GetRenderTargets();
                previousViewport = device.Viewport;
                previousBlend = device.BlendState;
                previousDepth = device.DepthStencilState;
                previousRasterizer = device.RasterizerState;
                previousSampler = device.SamplerStates[0];
                stateCaptured = true;
                device.SetRenderTarget(_target);
                device.Clear(new Color(0x17, 0x16, 0x11));
                _batch.Begin(
                    SpriteSortMode.Deferred,
                    BlendState.AlphaBlend,
                    SamplerState.PointClamp,
                    DepthStencilState.None,
                    RasterizerState.CullNone);
                batchBegun = true;
                _hits.Clear();
                DrawBackground();
                DrawNineSlice("ui:panel", new PanelRect(Outer, Outer, Width - Outer * 2,
                    Height - Outer * 2), Parchment);
                DrawHeader();
                DrawRule(ContentTop - 4);
                DrawPage(new PanelRect(Outer + 14, ContentTop + 6,
                    Width - (Outer + 14) * 2, ContentBottom - ContentTop - 12));
                _batch.End();
                batchBegun = false;

                _target.GetData(_pixels);
                for (var index = 0; index < _pixels.Length; index++)
                {
                    var pixel = _pixels[index];
                    var at = index * 4;
                    _output[at] = pixel.B;
                    _output[at + 1] = pixel.G;
                    _output[at + 2] = pixel.R;
                    _output[at + 3] = 0xFF;
                }
                present(_output);
                _renderFailureCount = 0;
            }
            catch (Exception failure)
            {
                _renderFailureCount++;
                if (_renderFailureCount == 1 || now >= _nextFailureLogAt)
                {
                    _nextFailureLogAt = now + 60_000;
                    _log($"Native panel render failed (attempt {_renderFailureCount}): {failure}");
                }
                lock (_stateGate)
                {
                    _dirty = true;
                    _nextFrame = now + Math.Min(5000, 250 * _renderFailureCount);
                }
            }
            finally
            {
                if (batchBegun && _batch != null)
                {
                    try { _batch.End(); }
                    catch { }
                }
                try
                {
                    if (device != null && stateCaptured)
                    {
                        if (previousTargets.Length > 0) device.SetRenderTargets(previousTargets);
                        else device.SetRenderTarget(null);
                        device.Viewport = previousViewport;
                        if (previousBlend != null) device.BlendState = previousBlend;
                        if (previousDepth != null) device.DepthStencilState = previousDepth;
                        if (previousRasterizer != null) device.RasterizerState = previousRasterizer;
                        if (previousSampler != null) device.SamplerStates[0] = previousSampler;
                    }
                }
                catch (Exception failure)
                {
                    if (!_restoreFailureLogged)
                    {
                        _restoreFailureLogged = true;
                        _log("Native panel could not restore MonoGame state: " + failure);
                    }
                }
            }
        }

        private void EnsureGraphics(GraphicsDevice device)
        {
            if (ReferenceEquals(_device, device) && _target is { IsDisposed: false } &&
                _pixel is { IsDisposed: false }) return;
            DisposeGraphics();
            _device = device;
            _target = new RenderTarget2D(
                device, Width, Height, false, SurfaceFormat.Color, DepthFormat.None,
                0, RenderTargetUsage.DiscardContents);
            _batch = new SpriteBatch(device);
            _pixel = new Texture2D(device, 1, 1);
            _pixel.SetData(new[] { Color.White });
            _pixels = new Color[Width * Height];
            _output = new byte[Width * Height * 4];
            _textures.Clear();
            _textureSources.Clear();
            _textureAccess.Clear();
            _spriteFailures.Clear();
            _renderFailureCount = 0;
            _restoreFailureLogged = false;
        }

        private void DrawBackground()
        {
            var ground = Sprite("ui:ground");
            if (ground == null)
            {
                Fill(new PanelRect(0, 0, Width, Height), new Color(0x40, 0x50, 0x2C));
                return;
            }
            var tileWidth = Math.Max(24, ground.Width * 3);
            var tileHeight = Math.Max(24, ground.Height * 3);
            for (var y = 0; y < Height; y += tileHeight)
                for (var x = 0; x < Width; x += tileWidth)
                    _batch!.Draw(ground, new Rectangle(x, y, tileWidth, tileHeight), Color.White);
            Fill(new PanelRect(0, 0, Width, Height), Color.Black * 0.30f);
        }

        private void DrawHeader()
        {
            var previous = new PanelRect(Outer + 12, Outer + 10, 72, 72);
            var next = new PanelRect(Width - Outer - 84, Outer + 10, 72, 72);
            DrawButton(previous, "ui:arrow_left", false);
            DrawButton(next, "ui:arrow_right", false);
            _hits.Add(PanelHit.Page(previous, -1));
            _hits.Add(PanelHit.Page(next, 1));

            var railLeft = previous.Right + 10;
            var railRight = next.X - 10;
            var gap = 7;
            var tabWidth = (railRight - railLeft - gap * (VisibleTabs - 1)) / VisibleTabs;
            var start = Math.Clamp(_page - 1, 0, Pages.Length - VisibleTabs);
            for (var offset = 0; offset < VisibleTabs; offset++)
            {
                var page = start + offset;
                var rect = new PanelRect(railLeft + offset * (tabWidth + gap), Outer + 8,
                    tabWidth, 76);
                DrawTabButton(rect, Pages[page], page == _page);
                if (page == _page)
                    Fill(new PanelRect(rect.X + rect.Width * 3 / 10, rect.Bottom - 5,
                        rect.Width * 4 / 10, 3), Cursor);
                _hits.Add(PanelHit.AbsolutePage(rect, page));
            }
        }

        private void DrawPage(PanelRect area)
        {
            switch (_page)
            {
                case 0: DrawBackpack(area); break;
                case 1: DrawCrafting(area); break;
                case 2: DrawProgress(area); break;
                case 3: DrawMap(area); break;
                case 4: DrawJournal(area); break;
                case 5: DrawRelationships(area); break;
                case 6: DrawGiftGuide(area); break;
                case 7: DrawFields(area); break;
                case 8: DrawMachines(area); break;
                case 9: DrawBundles(area); break;
                case 10: DrawCalendar(area); break;
                case 11: DrawSettings(area); break;
            }
        }

        private void DrawBackpack(PanelRect area)
        {
            using var day = Document("sdv_day");
            using var inventory = Document("sdv_inventory");
            using var skills = Document("sdv_skills");
            var d = day?.RootElement ?? default;
            var bag = inventory?.RootElement ?? default;
            var skill = skills?.RootElement ?? default;
            var capacity = Math.Clamp(Number(bag, "capacity", 12), 12, 36);
            var rows = (capacity + 11) / 12;
            var items = Array(bag, "items");

            // The reference rail is 128dp, or about 295 physical pixels on the
            // Thor. The previous 132-pixel rail was less than half its size.
            var sidebar = new PanelRect(area.X, area.Y, 290, area.Height);
            var main = new PanelRect(sidebar.Right + 18, area.Y, area.Width - sidebar.Width - 18,
                area.Height);
            DrawText(FarmTitle(Text(d, "farmName")), sidebar.X + 5, sidebar.Y + 3,
                0.76f, Ink, sidebar.Width - 10, 1);
            DrawLocalRule(sidebar.X, sidebar.Right, sidebar.Y + 40);
            DrawInfoLine(sidebar, sidebar.Y + 48, "ui:season",
                Text(d, "season", "Season"), DayLabel(Text(d, "weekday"), Number(d, "day")));
            DrawLocalRule(sidebar.X, sidebar.Right, sidebar.Y + 112);
            DrawInfoLine(sidebar, sidebar.Y + 120, "ui:time", Clock(Number(d, "timeOfDay", 600)),
                Text(d, "weatherToday"));
            DrawLocalRule(sidebar.X, sidebar.Right, sidebar.Y + 184);
            DrawSprite("ui:coin", new PanelRect(sidebar.X + 10, sidebar.Y + 196, 34, 30));
            DrawText(LongNumber(d, "gold").ToString("N0", CultureInfo.InvariantCulture) + "g",
                sidebar.X + 54, sidebar.Y + 195, 0.68f, Ink, sidebar.Width - 62, 1);
            DrawLocalRule(sidebar.X, sidebar.Right, sidebar.Y + 236);
            var farmerTop = sidebar.Y + 248;
            var farmerBottom = sidebar.Bottom - 170;
            var farmerArea = new PanelRect(sidebar.X + 10, farmerTop,
                sidebar.Width - 20, Math.Max(120, farmerBottom - farmerTop));
            Fill(farmerArea, ParchmentWash * 0.68f);
            DrawSprite(SpriteExists("ui:farmer") ? "ui:farmer" : "ui:player",
                farmerArea.Inset(8), fit: true);
            DrawText(Text(d, "playerName", "Farmer"), sidebar.X + 4, farmerArea.Bottom + 7,
                0.78f, Ink, sidebar.Width - 8, 1, centered: true);
            DrawText("Level " + Number(d, "playerLevel") + "  •  Farmer",
                sidebar.X + 4, farmerArea.Bottom + 38, 0.48f, MutedInk,
                sidebar.Width - 8, 1, centered: true);

            var vitalsY = farmerArea.Bottom + 68;
            DrawVital(sidebar, vitalsY, "Health", Number(d, "health"),
                Number(d, "maxHealth"), new Color(0xC8, 0x38, 0x38));
            DrawVital(sidebar, vitalsY + 30, "Stamina", Number(d, "energy"),
                Number(d, "maxEnergy"), new Color(0x2E, 0xA8, 0x65));
            DrawLocalRule(sidebar.X, sidebar.Right, vitalsY + 59);
            DrawText($"BACKPACK LEVEL {rows}", sidebar.X + 4, vitalsY + 68,
                0.45f, Ink, sidebar.Width - 8, 1, centered: true);
            DrawText(capacity >= 36 ? $"{capacity} slots - Maximum" :
                    $"{capacity} slots - Upgrade at Pierre's",
                sidebar.X + 4, vitalsY + 91, 0.40f, MutedInk,
                sidebar.Width - 8, 1, centered: true);

            DrawSprite("ui:tab_backpack", new PanelRect(main.X + 2, main.Y + 5, 52, 52));
            DrawText("Backpack", main.X + 66, main.Y + 2, 1.08f, Ink, 300, 1);
            var used = items.Count(element => Text(element, "id").Length > 0);
            DrawText($"{used}/{capacity} Slots Used", main.Right - 270, main.Y + 16,
                0.62f, Ink, 190, 1, right: true);
            var sort = new PanelRect(main.Right - 64, main.Y + 2, 58, 56);
            DrawButton(sort, "ui:sort", false);
            _hits.Add(PanelHit.Action(sort, "{\"type\":\"sort_bag\"}"));
            DrawLocalRule(main.X, main.Right, main.Y + 66);

            var selected = Number(bag, "selected");
            var firstRowY = main.Y + 80;
            var slotGap = 4;
            var slotSize = Math.Min(78, (main.Width - slotGap * 11) / 12);
            var rowStride = slotSize + 40;
            for (var row = 0; row < 3; row++)
            {
                // Inventory rows are consecutive. The previous implementation
                // divided all unused page height between them, placing locked
                // rows hundreds of pixels apart.
                var y = firstRowY + row * rowStride;
                if (row >= rows)
                {
                    var locked = new PanelRect(main.X, y + 14, main.Width, 58);
                    DrawNineSlice("ui:button", locked, ParchmentWash);
                    DrawSprite("ui:tab_backpack",
                        new PanelRect(locked.X + 12, locked.Y + 12, 34, 34));
                    DrawText($"Row {row + 1} (Locked)", locked.X + 58, locked.Y + 10,
                        0.52f, Ink, 210, 1);
                    DrawText(row == rows ? "Next backpack upgrade unlocks this hotbar row" :
                            "Unlocks after another backpack upgrade",
                        locked.X + 270, locked.Y + 12, 0.40f, MutedInk,
                        locked.Width - 282, 1);
                    continue;
                }

                DrawText(row == 0 ? "Row 1 (Hotbar)" : $"Row {row + 1}", main.X, y,
                    0.52f, Ink, 220, 1);
                for (var column = 0; column < 12; column++)
                {
                    var slot = row * 12 + column;
                    var box = new PanelRect(main.X + column * (slotSize + slotGap), y + 30,
                        slotSize, slotSize);
                    DrawSlot(box, ItemAt(items, slot), slot == selected, slot, row == 0);
                }
            }

            var skillsY = main.Bottom - 166;
            DrawLocalRule(main.X, main.Right, skillsY - 8);
            DrawSprite("ui:tab_skills", new PanelRect(main.X, skillsY, 34, 34));
            DrawText("Skills", main.X + 43, skillsY + 1, 0.58f, Ink, 130, 1);
            var names = new[] { "Farming", "Mining", "Foraging", "Fishing", "Combat" };
            var keys = new[] { "farming", "mining", "foraging", "fishing", "combat" };
            var skillWidth = (main.Width - 180) / 5;
            for (var index = 0; index < names.Length; index++)
            {
                var x = main.X + 170 + index * skillWidth;
                DrawText(names[index], x, skillsY + 4, 0.44f, MutedInk,
                    skillWidth, 1, centered: true);
                DrawText(Number(skill, keys[index]).ToString(), x, skillsY + 42, 0.65f, Ink,
                    skillWidth, 1, centered: true);
            }
            DrawLocalRule(main.X, main.Right, main.Bottom - 78);
            var selectedItem = ItemAt(items, selected);
            var selectedName = Text(selectedItem, "name");
            DrawText(selectedName.Length == 0 ? $"Slot {selected + 1} is empty" : selectedName,
                main.X + 4, main.Bottom - 68, 0.55f, Ink, main.Width - 8, 1);
            DrawText("L/R cycles rows • tap a slot to move items • hold to use",
                main.X + 4, main.Bottom - 32, 0.40f, MutedInk, main.Width - 8, 1);
        }

        private void DrawCrafting(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_crafting", "Crafting",
                "Recipes use Stardew's live inventory and crafting rules");
            using var document = Document("sdv_crafting");
            var recipes = document == null ? new List<JsonElement>() :
                Array(document.RootElement, "recipes");
            DrawCardGrid(area, recipes, 3, "No known crafting recipes", (recipe, rect) =>
            {
                DrawSprite(Text(recipe, "id"), new PanelRect(rect.X + 10, rect.Y + 12, 54, 54));
                DrawText(Text(recipe, "name", "Recipe"), rect.X + 74, rect.Y + 10,
                    0.64f, Ink, rect.Width - 84, 1);
                var ready = Flag(recipe, "ready");
                DrawText(ready ? "Ready to craft" : "Missing ingredients", rect.X + 74,
                    rect.Y + 39, 0.49f, ready ? Cursor : MutedInk, rect.Width - 84, 1);
                DrawText(Text(recipe, "description"), rect.X + 10, rect.Y + 78,
                    0.48f, MutedInk, rect.Width - 20, 2);
                var ingredients = Array(recipe, "ingredients");
                var summary = string.Join("  •  ", ingredients.Take(3).Select(item =>
                    $"{Text(item, "name")} {Number(item, "have")}/{Number(item, "required")}"));
                DrawText(summary, rect.X + 10, rect.Bottom - 39, 0.43f, Ink,
                    rect.Width - 20, 2);
                if (ready)
                {
                    var key = Text(recipe, "key");
                    _hits.Add(PanelHit.Action(rect,
                        "{\"type\":\"craft_recipe\",\"key\":" +
                        JsonSerializer.Serialize(key) + "}"));
                }
            });
        }

        private void DrawProgress(PanelRect area)
        {
            DrawPageTitle(area, "ui:coin", "Progress", "Skills, mine, museum, and shipping");
            using var skills = Document("sdv_skills");
            using var mines = Document("sdv_mines");
            using var museum = Document("sdv_collections");
            using var shipping = Document("sdv_shipping");
            var content = PageBody(area);
            var top = new PanelRect(content.X, content.Y, content.Width, 240);
            DrawSection(top, "Skills");
            var skillNames = new[] { "Farming", "Mining", "Foraging", "Fishing", "Combat" };
            var skillKeys = new[] { "farming", "mining", "foraging", "fishing", "combat" };
            var root = skills?.RootElement ?? default;
            for (var index = 0; index < skillNames.Length; index++)
            {
                var width = (top.Width - 20) / 5;
                var box = new PanelRect(top.X + 10 + index * width, top.Y + 56, width - 8, 145);
                Fill(box, ParchmentWash * 0.55f);
                DrawText(skillNames[index], box.X, box.Y + 12, 0.58f, Ink, box.Width, 1,
                    centered: true);
                DrawText(Number(root, skillKeys[index]).ToString(), box.X, box.Y + 52,
                    1.12f, Ink, box.Width, 1, centered: true);
                var next = Number(root, skillKeys[index] + "Next", -1);
                DrawText(next <= 0 ? "Max" : next.ToString("N0") + " XP",
                    box.X, box.Bottom - 35, 0.46f, MutedInk, box.Width, 1, centered: true);
            }
            var lowerY = top.Bottom + 12;
            var column = (content.Width - 24) / 3;
            var lowerHeight = content.Bottom - lowerY;
            DrawMetricCard(new PanelRect(content.X, lowerY, column, lowerHeight),
                "Mine Progress", "Deepest floor", Number(mines?.RootElement ?? default, "deepest"),
                "Skull Cavern " + Number(mines?.RootElement ?? default, "skull"));
            DrawMetricCard(new PanelRect(content.X + column + 12, lowerY, column, lowerHeight),
                "Museum", "Donated", Number(museum?.RootElement ?? default, "donated"),
                "of " + Number(museum?.RootElement ?? default, "total", 95));
            DrawMetricCard(new PanelRect(content.X + (column + 12) * 2, lowerY, column, lowerHeight),
                "Shipping Bin", "Today's value",
                LongNumber(shipping?.RootElement ?? default, "total"),
                "gold estimated");
        }

        private void DrawMap(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_map", "Map", "Farmer and villager tracking");
            using var day = Document("sdv_day");
            using var villagers = Document("sdv_villagers");
            var body = PageBody(area);
            Fill(body, new Color(0xC9, 0xB1, 0x75));
            var map = Sprite("ui:map");
            var mapRect = body.Inset(12);
            if (map != null)
            {
                var fitted = Fit(mapRect, map.Width, map.Height);
                _batch!.Draw(map, fitted.ToRectangle(), Color.White);
                var root = day?.RootElement ?? default;
                DrawMapMarker(fitted, Number(root, "mapX", -1), Number(root, "mapY", -1),
                    "ui:player", Cursor, 42);
                if (villagers != null)
                {
                    foreach (var person in Array(villagers.RootElement, "villagers"))
                        DrawMapMarker(fitted, Number(person, "mapX", -1),
                            Number(person, "mapY", -1), "npc:" + Text(person, "npc"),
                            Ready, 28);
                }
                DrawText(Text(root, "location", "Valley"), fitted.X + 12,
                    fitted.Bottom - 44, 0.62f, Ink, fitted.Width - 24, 1);
            }
            else
            {
                DrawText("Map artwork is loading from Stardew Valley", body.X, body.Y + 80,
                    0.72f, MutedInk, body.Width, 2, centered: true);
            }
        }

        private void DrawJournal(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_journal", "Journal", "Quests and special orders");
            using var questsDoc = Document("sdv_quests");
            using var ordersDoc = Document("sdv_orders");
            var quests = questsDoc == null ? new List<JsonElement>() :
                Array(questsDoc.RootElement, "quests").Where(q => !Flag(q, "complete")).ToList();
            var orders = ordersDoc == null ? new List<JsonElement>() :
                Array(ordersDoc.RootElement, "orders");
            var body = PageBody(area);
            var left = new PanelRect(body.X, body.Y, body.Width * 2 / 3 - 6, body.Height);
            var right = new PanelRect(left.Right + 12, body.Y, body.Right - left.Right - 12,
                body.Height);
            DrawSection(left, $"Quests  {quests.Count}");
            DrawRows(left.Inset(10, 48), quests, 114, (quest, rect) =>
            {
                DrawText(Text(quest, "title", "Quest"), rect.X + 9, rect.Y + 8,
                    0.62f, Ink, rect.Width - 18, 1);
                DrawText(Text(quest, "objective", Text(quest, "detail")), rect.X + 9,
                    rect.Y + 40, 0.48f, MutedInk, rect.Width - 18, 2);
                var days = Number(quest, "daysLeft", -1);
                var reward = Number(quest, "reward");
                DrawText((days >= 0 ? days + " days  •  " : "") +
                    (reward > 0 ? reward.ToString("N0") + "g" : "Story quest"),
                    rect.X + 9, rect.Bottom - 26, 0.45f,
                    days is 0 or 1 ? Error : MutedInk, rect.Width - 100, 1);
                if (Flag(quest, "cancellable"))
                {
                    var cancel = new PanelRect(rect.Right - 78, rect.Bottom - 35, 66, 27);
                    DrawButton(cancel, "ui:check_off", false);
                    DrawText("Cancel", cancel.X, cancel.Y + 5, 0.42f, Error,
                        cancel.Width, 1, centered: true);
                    _hits.Add(PanelHit.Action(cancel,
                        $"{{\"type\":\"cancel_quest\",\"value\":{Number(quest, "index")}}}"));
                }
            });
            DrawSection(right, $"Special Orders  {orders.Count}");
            DrawRows(right.Inset(10, 48), orders, 132, (order, rect) =>
            {
                DrawText(Text(order, "title", "Special Order"), rect.X + 9, rect.Y + 8,
                    0.58f, Ink, rect.Width - 18, 2);
                DrawText(Text(order, "objective"), rect.X + 9, rect.Y + 60,
                    0.46f, MutedInk, rect.Width - 18, 2);
                DrawText(Number(order, "daysLeft") + " days left", rect.X + 9,
                    rect.Bottom - 28, 0.45f, Ink, rect.Width - 18, 1);
            });
        }

        private void DrawRelationships(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_relationships", "Relationships",
                "Friendship, birthdays, gifts, and locations");
            using var document = Document("sdv_villagers");
            var villagers = document == null ? new List<JsonElement>() :
                Array(document.RootElement, "villagers");
            DrawCardGrid(area, villagers, 4, "No villagers in this save", (person, rect) =>
            {
                DrawSprite("npc:" + Text(person, "npc"),
                    new PanelRect(rect.X + 10, rect.Y + 10, 66, 66));
                DrawText(Text(person, "name", "Villager"), rect.X + 86, rect.Y + 8,
                    0.61f, Ink, rect.Width - 96, 1);
                DrawHearts(rect.X + 86, rect.Y + 38, Number(person, "hearts"),
                    Number(person, "maxHearts", 10), rect.Width - 96);
                DrawText(Text(person, "location"), rect.X + 10, rect.Y + 84,
                    0.47f, MutedInk, rect.Width - 20, 1);
                var birthday = Number(person, "birthdayIn", -1);
                var detail = birthday == 0 ? "Birthday today" :
                    birthday > 0 ? $"Birthday in {birthday} days" :
                    $"{Text(person, "birthdaySeason")} {Number(person, "birthdayDay")}";
                DrawText(detail, rect.X + 10, rect.Bottom - 30, 0.46f,
                    birthday == 0 ? Error : Ink, rect.Width - 20, 1);
            });
        }

        private void DrawGiftGuide(PanelRect area)
        {
            DrawPageTitle(area, "ui:heart", "Gift Guide",
                "Loved gifts from the live game data; gold marks items in your bag");
            using var document = Document("sdv_gifts");
            var people = document == null ? new List<JsonElement>() :
                Array(document.RootElement, "villagers");
            DrawCardGrid(area, people, 3, "Gift tastes are unavailable", (person, rect) =>
            {
                DrawText(Text(person, "name", "Villager"), rect.X + 10, rect.Y + 8,
                    0.65f, Ink, rect.Width - 20, 1);
                var loves = Array(person, "loves");
                var iconSize = Math.Min(58, (rect.Width - 30) / Math.Max(1, Math.Min(5, loves.Count)));
                for (var index = 0; index < Math.Min(5, loves.Count); index++)
                {
                    var gift = loves[index];
                    var box = new PanelRect(rect.X + 10 + index * (iconSize + 4), rect.Y + 48,
                        iconSize, iconSize);
                    if (Flag(gift, "carried")) Fill(box, Ready * 0.42f);
                    DrawSprite(Text(gift, "id"), box.Inset(5));
                }
                var names = string.Join(", ", loves.Take(4).Select(g => Text(g, "name")));
                DrawText(names, rect.X + 10, rect.Y + 116, 0.46f, MutedInk,
                    rect.Width - 20, 2);
                DrawText(Flag(person, "carrying") ? "Loved gift in backpack" : "Not carried",
                    rect.X + 10, rect.Bottom - 29, 0.46f,
                    Flag(person, "carrying") ? Cursor : MutedInk, rect.Width - 20, 1);
            });
        }

        private void DrawFields(PanelRect area)
        {
            DrawPageTitle(area, "ui:season", "Fields",
                "Crops, ready produce, watering, and fruit trees");
            using var cropsDoc = Document("sdv_crops");
            using var treesDoc = Document("sdv_trees");
            var crops = cropsDoc == null ? new List<JsonElement>() : Array(cropsDoc.RootElement, "crops");
            var trees = treesDoc == null ? new List<JsonElement>() : Array(treesDoc.RootElement, "trees");
            var body = PageBody(area);
            var first = new PanelRect(body.X, body.Y, body.Width / 3 - 8, body.Height);
            var second = new PanelRect(first.Right + 12, body.Y, body.Width / 3 - 8, body.Height);
            var third = new PanelRect(second.Right + 12, body.Y, body.Right - second.Right - 12,
                body.Height);
            DrawFieldColumn(first, "Ready to harvest", crops.Where(c => Flag(c, "ready")).ToList());
            DrawFieldColumn(second, "Needs water",
                crops.Where(c => Flag(c, "needsWater") && !Flag(c, "ready")).ToList());
            DrawSection(third, $"Growing & Trees  {crops.Count + trees.Count}");
            var combined = crops.Where(c => !Flag(c, "ready") && !Flag(c, "needsWater"))
                .Select(c => new FieldRow(Text(c, "id"), Text(c, "name", "Crop"),
                    Text(c, "location"), Number(c, "daysLeft") + " days"))
                .Concat(trees.Select(t => new FieldRow(Text(t, "id"),
                    Text(t, "name", "Fruit tree"), Text(t, "location"),
                    Number(t, "count") + " fruit"))).ToList();
            DrawFieldRows(third.Inset(10, 48), combined);
        }

        private void DrawMachines(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_crafting", "Machines",
                "Ready machines, animals, fruit, and pet chores");
            using var machinesDoc = Document("sdv_machines");
            using var animalsDoc = Document("sdv_animals");
            using var treesDoc = Document("sdv_trees");
            using var petDoc = Document("sdv_pet");
            var machines = machinesDoc == null ? new List<JsonElement>() :
                Array(machinesDoc.RootElement, "machines");
            var animals = animalsDoc == null ? new List<JsonElement>() :
                Array(animalsDoc.RootElement, "animals");
            var trees = treesDoc == null ? new List<JsonElement>() :
                Array(treesDoc.RootElement, "trees");
            var body = PageBody(area);
            var col = (body.Width - 24) / 3;
            var machineArea = new PanelRect(body.X, body.Y, col, body.Height);
            var animalArea = new PanelRect(body.X + col + 12, body.Y, col, body.Height);
            var farmArea = new PanelRect(body.X + (col + 12) * 2, body.Y, col, body.Height);
            DrawSection(machineArea, $"Machines  {machines.Count(m => Flag(m, "ready"))} ready");
            DrawRows(machineArea.Inset(9, 48), machines, 92, (machine, rect) =>
            {
                DrawSprite(Text(machine, "id"), new PanelRect(rect.X + 7, rect.Y + 9, 46, 46));
                DrawSprite(Text(machine, "outputId"), new PanelRect(rect.X + 50, rect.Y + 31, 34, 34));
                DrawText(Text(machine, "name"), rect.X + 92, rect.Y + 8, 0.51f, Ink,
                    rect.Width - 100, 1);
                DrawText(Text(machine, "output"), rect.X + 92, rect.Y + 35, 0.46f,
                    MutedInk, rect.Width - 100, 1);
                DrawText(Flag(machine, "ready") ? "READY" : Minutes(Number(machine, "minutes")),
                    rect.X + 92, rect.Bottom - 25, 0.45f,
                    Flag(machine, "ready") ? Cursor : MutedInk, rect.Width - 100, 1);
            });
            DrawSection(animalArea, $"Animals  {animals.Count(a => !Flag(a, "petted"))} unpetted");
            DrawRows(animalArea.Inset(9, 48), animals, 94, (animal, rect) =>
            {
                DrawSprite(Text(animal, "spriteId"), new PanelRect(rect.X + 7, rect.Y + 10, 58, 58));
                DrawText(Text(animal, "name", Text(animal, "type")), rect.X + 75, rect.Y + 8,
                    0.55f, Ink, rect.Width - 84, 1);
                DrawHearts(rect.X + 75, rect.Y + 36, Number(animal, "hearts"), 5,
                    rect.Width - 84);
                DrawText(Flag(animal, "petted") ? "Petted" : "Needs petting",
                    rect.X + 75, rect.Bottom - 26, 0.44f,
                    Flag(animal, "petted") ? Cursor : Error, rect.Width - 84, 1);
            });
            DrawSection(farmArea, "Farm chores");
            var pet = petDoc?.RootElement ?? default;
            DrawSprite("ui:pet", new PanelRect(farmArea.X + 15, farmArea.Y + 58, 68, 68));
            DrawText(Text(pet, "name", "Pet"), farmArea.X + 95, farmArea.Y + 62,
                0.60f, Ink, farmArea.Width - 110, 1);
            DrawText((Flag(pet, "petted") ? "Petted" : "Needs petting") + "  •  " +
                (Flag(pet, "bowl") ? "Bowl watered" : "Bowl needs water"),
                farmArea.X + 95, farmArea.Y + 96, 0.44f, MutedInk,
                farmArea.Width - 110, 2);
            DrawLocalRule(farmArea.X + 10, farmArea.Right - 10, farmArea.Y + 150);
            var rows = trees.Select(tree => new FieldRow(Text(tree, "id"),
                Text(tree, "name", "Fruit tree"), Text(tree, "location"),
                Number(tree, "count") + " fruit")).ToList();
            DrawFieldRows(new PanelRect(farmArea.X + 10, farmArea.Y + 164,
                farmArea.Width - 20, farmArea.Height - 174), rows);
        }

        private void DrawBundles(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_skills", "Bundles",
                "Community Center rooms and carried donations");
            using var document = Document("sdv_bundles");
            var bundles = document == null ? new List<JsonElement>() :
                Array(document.RootElement, "bundles");
            DrawCardGrid(area, bundles, 3, "All Community Center bundles are complete",
                (bundle, rect) =>
                {
                    var have = Number(bundle, "have");
                    var required = Math.Max(1, Number(bundle, "required", 1));
                    DrawText(Text(bundle, "room", "Community Center"), rect.X + 10, rect.Y + 7,
                        0.45f, MutedInk, rect.Width - 20, 1);
                    DrawText(Text(bundle, "name", "Bundle"), rect.X + 10, rect.Y + 33,
                        0.62f, Ink, rect.Width - 20, 1);
                    DrawProgressBar(new PanelRect(rect.X + 10, rect.Y + 70, rect.Width - 20, 12),
                        have / (float)required);
                    DrawText($"{have}/{required}", rect.Right - 60, rect.Y + 87, 0.45f,
                        Ink, 50, 1, right: true);
                    var missing = Array(bundle, "missing");
                    for (var index = 0; index < Math.Min(6, missing.Count); index++)
                    {
                        var item = missing[index];
                        var icon = new PanelRect(rect.X + 10 + index * 57, rect.Y + 115, 50, 50);
                        if (Flag(item, "carried")) Fill(icon, Ready * 0.45f);
                        DrawSprite(Text(item, "id"), icon.Inset(5));
                        DrawText(Number(item, "count", 1).ToString(), icon.X, icon.Bottom - 14,
                            0.39f, Ink, icon.Width, 1, right: true);
                    }
                    var carried = missing.Count(item => Flag(item, "carried"));
                    DrawText(carried > 0 ? $"{carried} donation{(carried == 1 ? "" : "s")} in bag" :
                        "Missing items shown above", rect.X + 10, rect.Bottom - 30, 0.45f,
                        carried > 0 ? Cursor : MutedInk, rect.Width - 20, 1);
                });
        }

        private void DrawCalendar(PanelRect area)
        {
            DrawPageTitle(area, "ui:season", "Calendar", "Birthdays, festivals, and merchant days");
            using var calendar = Document("sdv_calendar");
            using var cart = Document("sdv_cart");
            var root = calendar?.RootElement ?? default;
            var events = Array(root, "days")
                .Where(day => Number(day, "day") is >= 1 and <= 28)
                .GroupBy(day => Number(day, "day"))
                .ToDictionary(group => group.Key, group => group.Last());
            var body = PageBody(area);
            var gap = 7;
            var cellWidth = (body.Width - gap * 6) / 7;
            var cellHeight = (body.Height - gap * 3) / 4;
            var today = Number(root, "today");
            for (var day = 1; day <= 28; day++)
            {
                var column = (day - 1) % 7;
                var row = (day - 1) / 7;
                var rect = new PanelRect(body.X + column * (cellWidth + gap),
                    body.Y + row * (cellHeight + gap), cellWidth, cellHeight);
                DrawNineSlice("ui:button", rect, day == today ? ParchmentLight : ParchmentWash);
                if (day == today) Stroke(rect.Inset(2), Cursor, 3);
                DrawText(day.ToString(), rect.X + 8, rect.Y + 7, 0.67f, Ink, 38, 1);
                if (events.TryGetValue(day, out var entry))
                {
                    var npc = Text(entry, "npc");
                    if (npc.Length > 0)
                        DrawSprite("npc:" + npc,
                            new PanelRect(rect.X + 42, rect.Y + 24, 64, 64));
                    var festival = Text(entry, "festival");
                    var names = Text(entry, "names");
                    DrawText(festival.Length > 0 ? festival : names, rect.X + 8,
                        rect.Bottom - 55, 0.43f, festival.Length > 0 ? Error : Ink,
                        rect.Width - 16, 2, centered: true);
                }
                if (Flag(cart?.RootElement ?? default, "open") && (day % 7 == 5 || day % 7 == 0))
                    DrawText("Cart", rect.Right - 46, rect.Y + 9, 0.40f, Cursor, 38, 1,
                        right: true);
            }
        }

        private void DrawSettings(PanelRect area)
        {
            DrawPageTitle(area, "ui:tab_settings", "Settings",
                "Native in-game panel • no SMAPI • no WebSocket");
            using var config = Document("sdv_config");
            var root = config?.RootElement ?? default;
            var body = PageBody(area);
            var settings = new[]
            {
                new Setting("hideGameHud", "Hide the duplicate HUD on the gameplay screen",
                    "The lower panel keeps inventory, time, gold, and status visible."),
                new Setting("allowInventoryEdits", "Allow inventory and crafting controls",
                    "Enables slot selection, row swaps, sorting, eating, and crafting."),
                new Setting("allowQuestCancel", "Allow quest cancellation",
                    "Shows the cancel control on cancellable journal entries."),
                new Setting("farmerMarker", "Show the farmer on the valley map",
                    "Uses Stardew's rendered farmer portrait as the marker."),
                new Setting("sendCrops", "Show crop and fruit-tree status",
                    "Keeps the Fields page synchronized with loaded locations."),
                new Setting("sendMachines", "Show machine readiness",
                    "Tracks machine output and remaining processing time."),
                new Setting("sendAnimals", "Show animal chores",
                    "Tracks petting and ready produce."),
                new Setting("sendBundles", "Show Community Center bundles",
                    "Highlights missing items already carried in the backpack."),
                new Setting("sendVillagers", "Show relationships and gift guide",
                    "Tracks birthdays, friendship, gifts, and villager locations."),
                new Setting("sendMap", "Show map and live positions",
                    "Uses Stardew's own world-map art and position data."),
                new Setting("sendCrafting", "Show and enable crafting recipes",
                    "Uses the game's ingredient checks and crafting transaction."),
            };
            var rowHeight = 73;
            var first = Math.Clamp(_scroll[_page], 0, Math.Max(0, settings.Length - 9));
            for (var index = first; index < settings.Length && index < first + 9; index++)
            {
                var setting = settings[index];
                var y = body.Y + (index - first) * rowHeight;
                var row = new PanelRect(body.X, y, body.Width, rowHeight - 5);
                Fill(row, index % 2 == 0 ? ParchmentWash * 0.38f : Color.Transparent);
                DrawSprite(Flag(root, setting.Key) ? "ui:check_on" : "ui:check_off",
                    new PanelRect(row.X + 11, row.Y + 17, 36, 36));
                DrawText(setting.Title, row.X + 61, row.Y + 8, 0.59f, Ink,
                    row.Width - 72, 1);
                DrawText(setting.Detail, row.X + 61, row.Y + 37, 0.45f, MutedInk,
                    row.Width - 72, 1);
                _hits.Add(PanelHit.Action(row,
                    "{\"type\":\"set_option\",\"key\":" +
                    JsonSerializer.Serialize(setting.Key) + ",\"value\":" +
                    (Flag(root, setting.Key) ? "0" : "1") + "}"));
            }
        }

        private void DrawPageTitle(PanelRect area, string icon, string title, string subtitle)
        {
            DrawSprite(icon, new PanelRect(area.X + 2, area.Y + 3, 54, 54));
            DrawText(title, area.X + 68, area.Y - 2, 0.92f, Ink, 520, 1);
            DrawText(subtitle, area.X + 69, area.Y + 42, 0.40f, MutedInk,
                area.Width - 74, 1);
            DrawLocalRule(area.X, area.Right, area.Y + 72);
        }

        private static PanelRect PageBody(PanelRect area) =>
            new(area.X, area.Y + 84, area.Width, area.Height - 84);

        private void DrawCardGrid(
            PanelRect area,
            IReadOnlyList<JsonElement> entries,
            int columns,
            string empty,
            Action<JsonElement, PanelRect> draw)
        {
            var body = PageBody(area);
            if (entries.Count == 0)
            {
                DrawText(empty, body.X, body.Y + 100, 0.76f, MutedInk,
                    body.Width, 2, centered: true);
                return;
            }
            var gap = 9;
            var rowsVisible = 3;
            var width = (body.Width - gap * (columns - 1)) / columns;
            var height = (body.Height - gap * (rowsVisible - 1)) / rowsVisible;
            var totalRows = (entries.Count + columns - 1) / columns;
            var maxFirstRow = Math.Max(0, totalRows - rowsVisible);
            var firstRow = Math.Clamp(_scroll[_page], 0, maxFirstRow);
            var start = firstRow * columns;
            for (var index = 0; index < columns * rowsVisible && start + index < entries.Count;
                 index++)
            {
                var column = index % columns;
                var row = index / columns;
                var rect = new PanelRect(body.X + column * (width + gap),
                    body.Y + row * (height + gap), width, height);
                DrawNineSlice("ui:button", rect, ParchmentWash);
                draw(entries[start + index], rect);
            }
        }

        private void DrawRows(
            PanelRect area,
            IReadOnlyList<JsonElement> entries,
            int rowHeight,
            Action<JsonElement, PanelRect> draw)
        {
            var visible = Math.Max(1, area.Height / rowHeight);
            var first = Math.Clamp(_scroll[_page], 0, Math.Max(0, entries.Count - visible));
            for (var index = 0; index < visible && first + index < entries.Count; index++)
            {
                var rect = new PanelRect(area.X, area.Y + index * rowHeight,
                    area.Width, rowHeight - 7);
                DrawNineSlice("ui:button", rect, ParchmentWash);
                draw(entries[first + index], rect);
            }
        }

        private void DrawFieldColumn(PanelRect area, string title, List<JsonElement> crops)
        {
            DrawSection(area, $"{title}  {crops.Count}");
            var rows = crops.Select(crop => new FieldRow(
                Text(crop, "id"), Text(crop, "name", "Crop"), Text(crop, "location"),
                Flag(crop, "ready") ? "Ready" : Number(crop, "daysLeft") + " days"))
                .ToList();
            DrawFieldRows(area.Inset(10, 48), rows);
        }

        private void DrawFieldRows(PanelRect area, IReadOnlyList<FieldRow> rows)
        {
            if (rows.Count == 0)
            {
                DrawText("All clear", area.X, area.Y + 40, 0.57f, MutedInk,
                    area.Width, 1, centered: true);
                return;
            }
            var rowHeight = 78;
            var visible = Math.Max(1, area.Height / rowHeight);
            var first = Math.Clamp(_scroll[_page], 0, Math.Max(0, rows.Count - visible));
            for (var index = 0; index < visible && first + index < rows.Count; index++)
            {
                var row = rows[first + index];
                var rect = new PanelRect(area.X, area.Y + index * rowHeight,
                    area.Width, rowHeight - 5);
                Fill(rect, index % 2 == 0 ? ParchmentWash * 0.38f : Color.Transparent);
                DrawSprite(row.Id, new PanelRect(rect.X + 6, rect.Y + 8, 50, 50));
                DrawText(row.Name, rect.X + 65, rect.Y + 6, 0.52f, Ink,
                    rect.Width - 74, 1);
                DrawText(row.Place, rect.X + 65, rect.Y + 32, 0.43f, MutedInk,
                    rect.Width - 74, 1);
                DrawText(row.Status, rect.X + 65, rect.Bottom - 22, 0.43f,
                    row.Status == "Ready" ? Cursor : Ink, rect.Width - 74, 1);
            }
        }

        private void DrawSection(PanelRect rect, string title)
        {
            DrawNineSlice("ui:panel", rect, Parchment);
            DrawText(title, rect.X + 12, rect.Y + 11, 0.66f, Ink, rect.Width - 24, 1);
            DrawLocalRule(rect.X + 10, rect.Right - 10, rect.Y + 43);
        }

        private void DrawMetricCard(
            PanelRect rect, string title, string label, long value, string footer)
        {
            DrawSection(rect, title);
            DrawText(label, rect.X + 15, rect.Y + 85, 0.58f, MutedInk,
                rect.Width - 30, 1, centered: true);
            DrawText(value.ToString("N0"), rect.X + 15, rect.Y + 145, 1.45f, Ink,
                rect.Width - 30, 1, centered: true);
            DrawText(footer, rect.X + 15, rect.Y + 235, 0.54f, MutedInk,
                rect.Width - 30, 2, centered: true);
        }

        private void DrawSlot(
            PanelRect rect, JsonElement item, bool selected, int slot, bool hotbar)
        {
            DrawNineSlice(selected ? "ui:slot_selected" : "ui:slot", rect,
                selected ? ParchmentLight : ParchmentWash);
            var id = Text(item, "id");
            if (id.Length > 0)
            {
                DrawSprite(id, rect.Inset(8));
                var count = Number(item, "count", 1);
                if (count > 1)
                    DrawText(count.ToString(), rect.X + 4, rect.Bottom - 23,
                        0.44f, Color.White, rect.Width - 9, 1, right: true, shadow: true);
                var water = Number(item, "water", -1);
                var waterMax = Number(item, "waterMax");
                if (water >= 0 && waterMax > 0)
                    DrawProgressBar(new PanelRect(rect.X + 8, rect.Bottom - 8,
                        rect.Width - 16, 5), water / (float)waterMax, new Color(0x35, 0x8B, 0xD1));
            }
            if (hotbar)
                DrawText(slot == 9 ? "0" : slot == 10 ? "-" : slot == 11 ? "=" :
                    (slot + 1).ToString(), rect.X + 4, rect.Y + 3, 0.38f, Ink,
                    rect.Width - 8, 1);
            var command = hotbar
                ? $"{{\"type\":\"select_slot\",\"slot\":{slot}}}"
                : $"{{\"type\":\"move_item\",\"from\":{slot},\"to\":{SelectedSlot()}}}";
            _hits.Add(PanelHit.Action(rect, command,
                id.Length > 0 ? $"{{\"type\":\"eat_slot\",\"slot\":{slot}}}" : null));
        }

        private void DrawInfoLine(PanelRect sidebar, int y, string icon, string primary,
            string secondary)
        {
            DrawSprite(icon, new PanelRect(sidebar.X + 8, y + 4, 42, 42));
            DrawText(primary, sidebar.X + 62, y, 0.56f, Ink, sidebar.Width - 70, 1);
            DrawText(secondary, sidebar.X + 62, y + 32, 0.40f, MutedInk,
                sidebar.Width - 70, 1);
        }

        private void DrawVital(PanelRect sidebar, int y, string label, int value,
            int maximum, Color tint)
        {
            DrawText(label, sidebar.X + 4, y, 0.38f, Ink, 66, 1);
            var progress = new PanelRect(sidebar.X + 72, y + 5,
                sidebar.Width - 154, 14);
            DrawProgressBar(progress, maximum > 0 ? value / (float)maximum : 0f, tint);
            DrawText($"{value}/{maximum}", sidebar.Right - 76, y, 0.36f, Ink, 72, 1,
                right: true);
        }

        private void DrawTabButton(PanelRect rect, PanelPage page, bool selected)
        {
            DrawNineSlice("ui:button", rect, selected ? ParchmentLight : ParchmentWash);
            if (selected) Fill(rect.Inset(6), Cursor * 0.14f);
            DrawSprite(page.Icon, new PanelRect(rect.X + 15, rect.Y + 16, 44, 44));
            DrawText(page.Title, rect.X + 72, rect.Y + 18, 0.62f, Ink,
                rect.Width - 84, 1);
        }

        private void DrawButton(PanelRect rect, string icon, bool selected)
        {
            DrawNineSlice("ui:button", rect, selected ? ParchmentLight : ParchmentWash);
            if (selected) Fill(rect.Inset(5), Cursor * 0.14f);
            DrawSprite(icon, rect.Inset(Math.Max(7, rect.Height / 5)), fit: true);
        }

        private void DrawNineSlice(string id, PanelRect destination, Color fallback)
        {
            var texture = Sprite(id);
            if (texture == null)
            {
                Fill(destination, fallback);
                Stroke(destination, Border, 2);
                return;
            }
            SpritePayload payload;
            lock (_stateGate) _spritePayloads.TryGetValue(id, out payload);
            var left = Math.Clamp(payload.InsetX, 1, Math.Max(1, texture.Width / 2));
            var top = Math.Clamp(payload.InsetY, 1, Math.Max(1, texture.Height / 2));
            var right = left;
            var bottom = top;
            var dx = Math.Min(left * 2, destination.Width / 2);
            var dy = Math.Min(top * 2, destination.Height / 2);
            DrawSlice(texture, new Rectangle(0, 0, left, top),
                new PanelRect(destination.X, destination.Y, dx, dy));
            DrawSlice(texture, new Rectangle(left, 0, texture.Width - left - right, top),
                new PanelRect(destination.X + dx, destination.Y,
                    destination.Width - dx * 2, dy));
            DrawSlice(texture, new Rectangle(texture.Width - right, 0, right, top),
                new PanelRect(destination.Right - dx, destination.Y, dx, dy));
            DrawSlice(texture, new Rectangle(0, top, left, texture.Height - top - bottom),
                new PanelRect(destination.X, destination.Y + dy, dx,
                    destination.Height - dy * 2));
            DrawSlice(texture, new Rectangle(left, top, texture.Width - left - right,
                    texture.Height - top - bottom),
                new PanelRect(destination.X + dx, destination.Y + dy,
                    destination.Width - dx * 2, destination.Height - dy * 2));
            DrawSlice(texture, new Rectangle(texture.Width - right, top, right,
                    texture.Height - top - bottom),
                new PanelRect(destination.Right - dx, destination.Y + dy, dx,
                    destination.Height - dy * 2));
            DrawSlice(texture, new Rectangle(0, texture.Height - bottom, left, bottom),
                new PanelRect(destination.X, destination.Bottom - dy, dx, dy));
            DrawSlice(texture, new Rectangle(left, texture.Height - bottom,
                    texture.Width - left - right, bottom),
                new PanelRect(destination.X + dx, destination.Bottom - dy,
                    destination.Width - dx * 2, dy));
            DrawSlice(texture, new Rectangle(texture.Width - right, texture.Height - bottom,
                    right, bottom),
                new PanelRect(destination.Right - dx, destination.Bottom - dy, dx, dy));
        }

        private void DrawSlice(Texture2D texture, Rectangle source, PanelRect destination)
        {
            if (source.Width <= 0 || source.Height <= 0 || destination.Width <= 0 ||
                destination.Height <= 0) return;
            _batch!.Draw(texture, destination.ToRectangle(), source, Color.White);
        }

        private void DrawSprite(string id, PanelRect destination, bool fit = true)
        {
            if (id.Length == 0) return;
            var texture = Sprite(id);
            if (texture == null) return;
            var target = fit ? Fit(destination, texture.Width, texture.Height) : destination;
            _batch!.Draw(texture, target.ToRectangle(), Color.White);
        }

        private Texture2D? Sprite(string id)
        {
            if (_device == null || id.Length == 0) return null;
            SpritePayload payload;
            lock (_stateGate)
            {
                if (!_spritePayloads.TryGetValue(id, out payload)) return null;
            }
            if (_textures.TryGetValue(id, out var loaded) &&
                !loaded.IsDisposed && _textureSources.TryGetValue(id, out var source) &&
                source == payload.Png)
            {
                _textureAccess[id] = ++_textureClock;
                return loaded;
            }
            if (loaded?.IsDisposed == true)
            {
                _textures.Remove(id);
                _textureSources.Remove(id);
                _textureAccess.Remove(id);
            }
            if (_spriteFailures.TryGetValue(id, out var failedSource) &&
                failedSource == payload.Png)
                return null;
            try
            {
                var bytes = Convert.FromBase64String(payload.Png);
                using var stream = new MemoryStream(bytes, writable: false);
                var texture = Texture2D.FromStream(_device, stream);
                if (_textures.Remove(id, out var old)) SafeDispose(old);
                _textures[id] = texture;
                _textureSources[id] = payload.Png;
                _textureAccess[id] = ++_textureClock;
                _spriteFailures.Remove(id);
                TrimTextureCache(id);
                return texture;
            }
            catch (Exception failure)
            {
                if (!_spriteFailures.TryGetValue(id, out failedSource) ||
                    failedSource != payload.Png)
                {
                    _spriteFailures[id] = payload.Png;
                    _log($"Native panel could not decode sprite {id}: {failure.Message}");
                }
                return null;
            }
        }

        private bool SpriteExists(string id)
        {
            lock (_stateGate) return _spritePayloads.ContainsKey(id);
        }

        private void TrimTextureCache(string keep)
        {
            while (_textures.Count > TextureCacheLimit)
            {
                string? oldest = null;
                var oldestAccess = long.MaxValue;
                foreach (var entry in _textureAccess)
                {
                    if (entry.Key == keep || entry.Key.StartsWith("ui:", StringComparison.Ordinal))
                        continue;
                    if (entry.Value >= oldestAccess) continue;
                    oldest = entry.Key;
                    oldestAccess = entry.Value;
                }
                if (oldest == null) return;
                if (_textures.Remove(oldest, out var texture)) SafeDispose(texture);
                _textureSources.Remove(oldest);
                _textureAccess.Remove(oldest);
            }
        }

        private void DrawText(
            string text,
            int x,
            int y,
            float scale,
            Color color,
            int maxWidth,
            int maxLines,
            bool centered = false,
            bool right = false,
            bool shadow = false)
        {
            if (_batch == null || string.IsNullOrEmpty(text) || maxWidth <= 0) return;
            var font = Game1.smallFont;
            if (font == null) return;
            scale *= TextDisplayScale;
            var lines = Wrap(font, text.Replace('\n', ' '), maxWidth, scale, maxLines);
            for (var index = 0; index < lines.Count; index++)
            {
                var line = lines[index];
                var measured = font.MeasureString(line).X * scale;
                var at = new Vector2(
                    right ? x + maxWidth - measured : centered ? x + (maxWidth - measured) / 2f : x,
                    y + index * font.LineSpacing * scale);
                if (shadow)
                    _batch.DrawString(font, line, at + new Vector2(2, 2), Color.Black * 0.75f,
                        0, Vector2.Zero, scale, SpriteEffects.None, 0);
                _batch.DrawString(font, line, at, color, 0, Vector2.Zero, scale,
                    SpriteEffects.None, 0);
            }
        }

        private static List<string> Wrap(
            SpriteFont font, string text, int width, float scale, int maxLines)
        {
            var result = new List<string>();
            var words = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            var current = "";
            foreach (var word in words)
            {
                var candidate = current.Length == 0 ? word : current + " " + word;
                if (font.MeasureString(candidate).X * scale <= width || current.Length == 0)
                {
                    current = candidate;
                    continue;
                }
                result.Add(current);
                current = word;
                if (result.Count >= maxLines) break;
            }
            if (result.Count < maxLines && current.Length > 0) result.Add(current);
            if (result.Count == 0) result.Add(Truncate(font, text, width, scale));
            if (result.Count == maxLines && string.Join(" ", result).Length < text.Length)
                result[^1] = Truncate(font, result[^1] + "…", width, scale);
            return result;
        }

        private static string Truncate(SpriteFont font, string text, int width, float scale)
        {
            if (font.MeasureString(text).X * scale <= width) return text;
            var value = text;
            while (value.Length > 1 && font.MeasureString(value + "…").X * scale > width)
                value = value[..^1];
            return value + "…";
        }

        private void Fill(PanelRect rect, Color color)
        {
            if (_pixel == null || rect.Width <= 0 || rect.Height <= 0) return;
            _batch!.Draw(_pixel, rect.ToRectangle(), color);
        }

        private void Stroke(PanelRect rect, Color color, int thickness)
        {
            Fill(new PanelRect(rect.X, rect.Y, rect.Width, thickness), color);
            Fill(new PanelRect(rect.X, rect.Bottom - thickness, rect.Width, thickness), color);
            Fill(new PanelRect(rect.X, rect.Y, thickness, rect.Height), color);
            Fill(new PanelRect(rect.Right - thickness, rect.Y, thickness, rect.Height), color);
        }

        private void DrawRule(int y) => DrawLocalRule(Outer + 18, Width - Outer - 18, y);

        private void DrawLocalRule(int left, int right, int y) =>
            Fill(new PanelRect(left, y, Math.Max(0, right - left), 2), Line);

        private void DrawProgressBar(PanelRect rect, float fraction, Color? color = null)
        {
            Fill(rect, new Color(0x78, 0x58, 0x38) * 0.30f);
            Fill(new PanelRect(rect.X, rect.Y,
                (int)(rect.Width * Math.Clamp(fraction, 0f, 1f)), rect.Height), color ?? Cursor);
            Stroke(rect, Border, 1);
        }

        private void DrawHearts(int x, int y, int hearts, int maximum, int width)
        {
            var count = Math.Min(maximum, Math.Max(1, width / 19));
            for (var index = 0; index < count; index++)
                DrawSprite(index < hearts ? "ui:heart" : "ui:heart_empty",
                    new PanelRect(x + index * 18, y, 16, 14));
        }

        private void DrawMapMarker(
            PanelRect map, int x, int y, string sprite, Color fallback, int size)
        {
            if (x is < 0 or > 1000 || y is < 0 or > 1000) return;
            var cx = map.X + x * map.Width / 1000;
            var cy = map.Y + y * map.Height / 1000;
            var rect = new PanelRect(cx - size / 2, cy - size / 2, size, size);
            if (SpriteExists(sprite)) DrawSprite(sprite, rect);
            else
            {
                Fill(rect.Inset(size / 3), fallback);
                Stroke(rect.Inset(size / 3), Color.White, 2);
            }
        }

        private string? Activate(float x, float y, bool held)
        {
            for (var index = _hits.Count - 1; index >= 0; index--)
            {
                var hit = _hits[index];
                if (!hit.Bounds.Contains(x, y)) continue;
                switch (hit.Kind)
                {
                    case HitKind.RelativePage:
                        ChangePage(hit.Value);
                        return null;
                    case HitKind.AbsolutePage:
                        _page = Math.Clamp(hit.Value, 0, Pages.Length - 1);
                        _hits.Clear();
                        MarkDirty();
                        return null;
                    case HitKind.Command:
                        return held ? hit.HoldCommand ?? hit.Command : hit.Command;
                }
            }
            return null;
        }

        private (float x, float y) Logical(NativePanelInput input) => input.Normalized
            ? (Math.Clamp(input.X, 0, 1) * Width, Math.Clamp(input.Y, 0, 1) * Height)
            : (input.X, input.Y);

        private void ChangePage(int delta)
        {
            _page = (_page + delta + Pages.Length) % Pages.Length;
            _hits.Clear();
            MarkDirty();
        }

        private void ChangeScroll(int delta)
        {
            _scroll[_page] = Math.Clamp(_scroll[_page] + delta, 0, 10_000);
            MarkDirty();
        }

        private void MarkDirty()
        {
            lock (_stateGate)
            {
                _dirty = true;
                _nextFrame = 0;
            }
        }

        private string SelectRelativeSlot(int delta)
        {
            var selected = (SelectedSlot() + delta + 12) % 12;
            return $"{{\"type\":\"select_slot\",\"slot\":{selected}}}";
        }

        private int SelectedSlot()
        {
            using var inventory = Document("sdv_inventory");
            return Math.Clamp(Number(inventory?.RootElement ?? default, "selected"), 0, 11);
        }

        private JsonDocument? Document(string type)
        {
            string? raw;
            lock (_stateGate) _messages.TryGetValue(type, out raw);
            if (raw == null) return null;
            try { return JsonDocument.Parse(raw); }
            catch { return null; }
        }

        private static string Text(JsonElement root, string key, string fallback = "")
        {
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty(key, out var value))
                return fallback;
            return value.ValueKind == JsonValueKind.String ? value.GetString() ?? fallback :
                value.ToString();
        }

        private static int Number(JsonElement root, string key, int fallback = 0)
        {
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty(key, out var value))
                return fallback;
            if (value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var number))
                return number;
            return int.TryParse(value.ToString(), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out number) ? number : fallback;
        }

        private static long LongNumber(JsonElement root, string key, long fallback = 0)
        {
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty(key, out var value))
                return fallback;
            if (value.ValueKind == JsonValueKind.Number && value.TryGetInt64(out var number))
                return number;
            return long.TryParse(value.ToString(), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out number) ? number : fallback;
        }

        private static bool Flag(JsonElement root, string key, bool fallback = false)
        {
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty(key, out var value))
                return fallback;
            return value.ValueKind switch
            {
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                JsonValueKind.Number => value.TryGetInt32(out var number) && number != 0,
                JsonValueKind.String => value.GetString() is string text &&
                    (text == "1" || bool.TryParse(text, out var flag) && flag),
                _ => fallback,
            };
        }

        private static List<JsonElement> Array(JsonElement root, string key)
        {
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty(key, out var value) ||
                value.ValueKind != JsonValueKind.Array) return new List<JsonElement>();
            return value.EnumerateArray().ToList();
        }

        private static JsonElement ItemAt(IReadOnlyList<JsonElement> items, int slot)
        {
            foreach (var item in items)
                if (Number(item, "slot", -1) == slot) return item;
            return default;
        }

        private static PanelRect Fit(PanelRect destination, int width, int height)
        {
            if (width <= 0 || height <= 0) return destination;
            var scale = Math.Min(destination.Width / (float)width,
                destination.Height / (float)height);
            var fittedWidth = Math.Max(1, (int)(width * scale));
            var fittedHeight = Math.Max(1, (int)(height * scale));
            return new PanelRect(destination.X + (destination.Width - fittedWidth) / 2,
                destination.Y + (destination.Height - fittedHeight) / 2,
                fittedWidth, fittedHeight);
        }

        private static string FarmTitle(string farm)
        {
            if (string.IsNullOrWhiteSpace(farm)) return "Farm";
            return farm.EndsWith(" Farm", StringComparison.OrdinalIgnoreCase) ? farm : farm + " Farm";
        }

        private static string DayLabel(string weekday, int day) =>
            string.Join(" • ", new[] { weekday, day > 0 ? "Day " + day : "" }
                .Where(value => value.Length > 0));

        private static string Clock(int time)
        {
            var hour24 = (time / 100) % 24;
            var minute = time % 100;
            var hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;
            return $"{hour12}:{minute:00} {(hour24 < 12 ? "am" : "pm")}";
        }

        private static string Minutes(int minutes) => minutes <= 0 ? "Ready" :
            minutes >= 60 ? $"{minutes / 60}h {minutes % 60}m" : $"{minutes}m";

        private void DisposeGraphics()
        {
            foreach (var texture in _textures.Values) SafeDispose(texture);
            _textures.Clear();
            _textureSources.Clear();
            _textureAccess.Clear();
            _spriteFailures.Clear();
            SafeDispose(_batch);
            SafeDispose(_pixel);
            SafeDispose(_target);
            _batch = null;
            _pixel = null;
            _target = null;
            _pixels = null;
            _output = null;
            _device = null;
        }

        private static void SafeDispose(IDisposable? resource)
        {
            try { resource?.Dispose(); }
            catch { }
        }

        public void Dispose() => DisposeGraphics();

        private readonly record struct PanelPage(string Title, string Icon);
        private readonly record struct Setting(string Key, string Title, string Detail);
        private readonly record struct FieldRow(string Id, string Name, string Place, string Status);
        private readonly record struct SpritePayload(string Png, int InsetX, int InsetY);

        private enum HitKind { RelativePage, AbsolutePage, Command }

        private readonly record struct PanelHit(
            PanelRect Bounds,
            HitKind Kind,
            int Value,
            string? Command,
            string? HoldCommand)
        {
            public static PanelHit Page(PanelRect bounds, int delta) =>
                new(bounds, HitKind.RelativePage, delta, null, null);
            public static PanelHit AbsolutePage(PanelRect bounds, int page) =>
                new(bounds, HitKind.AbsolutePage, page, null, null);
            public static PanelHit Action(
                PanelRect bounds, string command, string? holdCommand = null) =>
                new(bounds, HitKind.Command, 0, command, holdCommand);
        }

        private readonly record struct PanelRect(int X, int Y, int Width, int Height)
        {
            public int Right => X + Width;
            public int Bottom => Y + Height;
            public bool Contains(float x, float y) =>
                x >= X && y >= Y && x < Right && y < Bottom;
            public PanelRect Inset(int all) => Inset(all, all);
            public PanelRect Inset(int horizontal, int vertical) =>
                new(X + horizontal, Y + vertical,
                    Math.Max(0, Width - horizontal * 2), Math.Max(0, Height - vertical * 2));
            public Rectangle ToRectangle() => new(X, Y, Width, Height);
        }
    }
}
