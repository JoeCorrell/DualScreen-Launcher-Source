using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace ValheimSecondScreen.Hooks
{
    public sealed class UIHider : MonoBehaviour
    {
        public static UIHider Instance { get; private set; }

        private static readonly FieldInfo _fSmallRoot =
            AccessTools.Field(typeof(Minimap), "m_smallRoot");
        private static readonly FieldInfo _fHealthPanel =
            AccessTools.Field(typeof(Hud), "m_healthPanel");
        private static readonly FieldInfo _fStaminaBar =
            AccessTools.Field(typeof(Hud), "m_staminaBar2Root");

        private GameObject _minimapRoot;
        private GameObject _healthPanel;
        private GameObject _staminaRoot;
        private GameObject _hotkeyBar;
        private Hud _resolvedHud;
        private Minimap _resolvedMinimap;

        private bool _isHidden;
        private bool _resolved;
        private float _nextResolve;

        private void Awake() => Instance = this;

        private void LateUpdate()
        {
            /*
             * Nothing is hidden once the HUD has been moved to a second display.
             *
             * Hiding exists because the socket path draws a replacement elsewhere,
             * so leaving the original on the game screen would show it twice. When
             * SecondDisplayMover has reparented the real elements there is no
             * replacement and no duplicate — hiding them would simply switch off
             * the very thing that was moved.
             */
            if (SecondDisplayMover.SecondDisplayLive || HudBandMover.Moved)
            {
                if (_isHidden) { _isHidden = false; SetVisible(true); }
                return;
            }

            bool shouldHide = Plugin.Instance?.Server?.HasClients() == true;

            bool stale = !_resolved
                         || _resolvedHud != Hud.instance
                         || _resolvedMinimap != Minimap.instance
                         || _minimapRoot == null
                         || _healthPanel == null
                         || _staminaRoot == null
                         || _hotkeyBar == null;
            if (stale && Time.unscaledTime >= _nextResolve)
            {
                _nextResolve = Time.unscaledTime + 1f;
                GameObject previousMinimap = _minimapRoot;
                GameObject previousHealth = _healthPanel;
                GameObject previousStamina = _staminaRoot;
                GameObject previousHotbar = _hotkeyBar;
                TryResolve();
                // A scene can replace the HUD while the socket fallback is
                // already active. Hide the newly acquired roots immediately;
                // waiting for the boolean state to change would leave them on
                // the main display for the rest of that connection.
                bool acquiredNewRoot = previousMinimap != _minimapRoot
                    || previousHealth != _healthPanel
                    || previousStamina != _staminaRoot
                    || previousHotbar != _hotkeyBar;
                if (_isHidden && _resolved && acquiredNewRoot) SetVisible(false);
                if (!_resolved) return;
            }

            if (shouldHide != _isHidden)
            {
                _isHidden = shouldHide;
                SetVisible(!shouldHide);
            }
        }

        private void TryResolve()
        {
            Minimap minimap = Minimap.instance;
            Hud hud = Hud.instance;
            if (minimap == null)
            {
                _resolved = false;
                return;
            }

            bool instanceChanged = minimap != _resolvedMinimap || hud != _resolvedHud;
            if (instanceChanged)
            {
                _minimapRoot = null;
                _healthPanel = null;
                _staminaRoot = null;
                _hotkeyBar = null;
                _resolvedMinimap = minimap;
                _resolvedHud = hud;
            }

            if (_fSmallRoot != null)
                _minimapRoot = _fSmallRoot.GetValue(minimap) as GameObject;

            if (hud != null)
            {
                if (_fHealthPanel != null)
                    _healthPanel = ToGameObject(_fHealthPanel.GetValue(hud));

                if (_fStaminaBar != null)
                    _staminaRoot = ToGameObject(_fStaminaBar.GetValue(hud));

                var hkb = hud.GetComponentInChildren<HotkeyBar>(true);
                if (hkb != null) _hotkeyBar = hkb.gameObject;
            }

            bool wasResolved = _resolved;
            _resolved = _minimapRoot != null && hud != null;
            if (_resolved && (!wasResolved || instanceChanged))
                Plugin.Log.LogInfo(
                    $"[SecondScreen] UIHider ready: minimap={_minimapRoot != null}, " +
                    $"hp={_healthPanel != null}, sta={_staminaRoot != null}, " +
                    $"hotbar={_hotkeyBar != null}");
        }

        private void SetVisible(bool visible)
        {
            _minimapRoot?.SetActive(visible);
            _healthPanel?.SetActive(visible);
            _staminaRoot?.SetActive(visible);
            _hotkeyBar?.SetActive(visible);
            Plugin.Log.LogInfo($"[SecondScreen] In-game UI {(visible ? "shown" : "hidden")}");
        }

        private static GameObject ToGameObject(object val)
        {
            if (val is GameObject go) return go;
            if (val is Component c) return c.gameObject;
            return null;
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
            SetVisible(true);
        }
    }
}
