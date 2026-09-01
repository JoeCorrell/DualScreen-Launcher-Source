namespace ValheimSecondScreen.UI
{
    /// <summary>Shared zoom step for the lower-screen map toolbar.</summary>
    internal static class MapZoom
    {
        private const float Step = 1.35f;

        /// <summary>Positive steps out; negative steps in.</summary>
        public static string Zoom(int direction)
        {
            return LargeMap.Scale(direction > 0 ? Step : 1f / Step);
        }
    }
}
