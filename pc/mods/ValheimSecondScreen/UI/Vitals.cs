using TMPro;
using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// The two vital columns: health at the map's left edge, stamina at its right.
    /// </summary>
    /// <remarks>
    /// Food and eitr were here and are not any more. Food does not move except
    /// when you eat, eitr is permanently zero until the Mistlands, and a column
    /// that never moves teaches you to stop looking at that side of the panel.
    /// What is left is the pair you read while something is hitting you, hard
    /// against the thing you were already looking at.
    /// </remarks>
    internal sealed class Vitals
    {
        private static readonly Color HealthColour = new Color(0.78f, 0.20f, 0.18f, 1f);
        private static readonly Color FoodColour = new Color(0.85f, 0.63f, 0.22f, 1f);
        private static readonly Color StaminaColour = new Color(0.85f, 0.78f, 0.28f, 1f);
        private static readonly Color EitrColour = new Color(0.45f, 0.60f, 0.88f, 1f);

        private readonly VitalBar _health;
        private readonly VitalBar _stamina;

        public Vitals(RectTransform leftRail, RectTransform rightRail,
                      TMP_FontAsset font, float width)
        {
            _health = new VitalBar(leftRail, "HP", HealthColour, font, width);
            _stamina = new VitalBar(rightRail, "STAM", StaminaColour, font, width);
        }

        public void Refresh()
        {
            var player = Player.m_localPlayer;
            if (player == null) return;

            _health.Set(player.GetHealth(), player.GetMaxHealth());
            _stamina.Set(player.GetStamina(), player.GetMaxStamina());
        }
    }
}
