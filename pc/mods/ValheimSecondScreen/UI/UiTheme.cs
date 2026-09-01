using UnityEngine;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// One visual vocabulary for every lower-screen page. Keeping these values
    /// together prevents a new page from becoming a slightly different brown,
    /// font hierarchy, or disabled state than the pages beside it.
    /// </summary>
    internal static class UiTheme
    {
        public static readonly Color Panel =
            new Color(0.042f, 0.030f, 0.020f, 0.62f);
        public static readonly Color PanelRaised =
            new Color(0.052f, 0.035f, 0.021f, 0.72f);
        public static readonly Color PageWash =
            new Color(0.025f, 0.016f, 0.009f, 0.11f);
        public static readonly Color Control =
            new Color(0.075f, 0.052f, 0.031f, 0.70f);
        public static readonly Color ControlSelected =
            new Color(0.52f, 0.27f, 0.050f, 0.76f);
        public static readonly Color ControlDisabled =
            new Color(0.045f, 0.036f, 0.028f, 0.66f);
        public static readonly Color Slot =
            new Color(0.025f, 0.018f, 0.012f, 0.64f);
        public static readonly Color SlotEquipped =
            new Color(0.34f, 0.18f, 0.046f, 0.70f);
        public static readonly Color Gold =
            new Color(0.91f, 0.78f, 0.57f, 1f);
        public static readonly Color GoldBright =
            new Color(1f, 0.88f, 0.66f, 1f);
        public static readonly Color Text =
            new Color(0.88f, 0.82f, 0.72f, 1f);
        public static readonly Color Muted =
            new Color(0.66f, 0.61f, 0.53f, 1f);
        public static readonly Color Good =
            new Color(0.58f, 0.86f, 0.48f, 1f);
        public static readonly Color Bad =
            new Color(0.88f, 0.38f, 0.30f, 1f);
        public static readonly Color Divider =
            new Color(0.73f, 0.43f, 0.15f, 0.34f);
        public static readonly Color Shadow =
            new Color(0.008f, 0.005f, 0.003f, 0.72f);

        public const float PageMargin = 12f;
        public const float SectionGap = 8f;
        public const float HeaderHeight = 76f;
        public const float MinimumTouch = 52f;
    }
}
