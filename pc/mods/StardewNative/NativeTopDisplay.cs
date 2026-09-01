using System;
using Microsoft.Xna.Framework;
using StardewValley;

namespace StardewSecondScreen
{
    /// <summary>
    /// Keeps Stardew's live SDL window on the upper monitor of WEMU's stacked
    /// guest desktop.
    ///
    /// The lower panel makes the guest desktop 1280x1800. Wine/SDL otherwise
    /// centres Stardew's decorated 1280x720 window in that entire rectangle,
    /// leaving only its bottom edge inside the 1280x720 upper monitor. The
    /// other native dual-screen mods solve the same problem by normalizing the
    /// live game window, rather than relying on a remembered fullscreen flag.
    /// MonoGame's GameWindow API is used here because DesktopGL exposes an SDL
    /// handle, not a Win32 HWND.
    /// </summary>
    internal sealed class NativeTopDisplay
    {
        private static readonly Point PrimaryOrigin = new(0, 0);

        private long _nextCheck;
        private bool _reported;
        private string _lastFailure = "";

        public void Ensure(Action<string> log)
        {
            var now = Environment.TickCount64;
            if (now < _nextCheck) return;
            _nextCheck = now + 1000L;

            try
            {
                var window = Game1.game1?.Window;
                if (window == null) return;

                var changed = false;
                if (!window.IsBorderless)
                {
                    window.IsBorderless = true;
                    changed = true;
                }

                if (window.Position != PrimaryOrigin)
                {
                    window.Position = PrimaryOrigin;
                    changed = true;
                }

                var bounds = window.ClientBounds;
                if (changed || !_reported)
                {
                    _reported = true;
                    _lastFailure = "";
                    log($"Top display normalized: borderless={window.IsBorderless}, " +
                        $"position={window.Position.X},{window.Position.Y}, " +
                        $"client={bounds.Width}x{bounds.Height}.");
                }
            }
            catch (Exception failure)
            {
                var message = failure.GetType().Name + ": " + failure.Message;
                if (message == _lastFailure) return;
                _lastFailure = message;
                log("Top display normalization failed; WEMU client fitting remains active: " +
                    message);
            }
        }
    }
}
