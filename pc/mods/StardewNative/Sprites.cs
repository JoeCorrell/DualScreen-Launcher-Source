using System.Collections.Generic;
using System.IO;
using Microsoft.Xna.Framework.Graphics;
using StardewValley;
using StardewValley.ItemTypeDefinitions;

namespace StardewSecondScreen
{
    internal sealed class Sprites
    {
        private readonly HashSet<string> _sent = new();

        public void Reset() => _sent.Clear();
        public bool AlreadySent(string id) => _sent.Contains(id);
        public void Remember(string id) => _sent.Add(id);

        public byte[]? EncodeRegion(Texture2D? sheet, Microsoft.Xna.Framework.Rectangle source)
        {
            try
            {
                if (sheet == null || source.Width <= 0 || source.Height <= 0 ||
                    source.Right > sheet.Width || source.Bottom > sheet.Height) return null;
                return EncodePixels(sheet, source);
            }
            catch
            {
                return null;
            }
        }

        public byte[]? EncodeId(string qualifiedId)
        {
            try
            {
                if (string.IsNullOrEmpty(qualifiedId)) return null;
                ParsedItemData? data = ItemRegistry.GetData(qualifiedId);
                if (data == null) return null;
                var png = EncodePixels(data.GetTexture(), data.GetSourceRect());
                if (png != null) _sent.Add(qualifiedId);
                return png;
            }
            catch
            {
                return null;
            }
        }

        public byte[]? Encode(Item item)
        {
            try
            {
                ParsedItemData? data = ItemRegistry.GetData(item.QualifiedItemId);
                if (data == null) return null;
                var png = EncodePixels(data.GetTexture(), data.GetSourceRect());
                if (png != null) _sent.Add(item.QualifiedItemId);
                return png;
            }
            catch
            {
                return null;
            }
        }

        private static byte[]? EncodePixels(
            Texture2D? sheet,
            Microsoft.Xna.Framework.Rectangle source)
        {
            if (sheet == null || source.Width <= 0 || source.Height <= 0 ||
                source.Right > sheet.Width || source.Bottom > sheet.Height) return null;

            var pixels = new Microsoft.Xna.Framework.Color[source.Width * source.Height];
            sheet.GetData(0, source, pixels, 0, pixels.Length);
            using var cut = new Texture2D(sheet.GraphicsDevice, source.Width, source.Height);
            cut.SetData(pixels);
            using var stream = new MemoryStream();
            cut.SaveAsPng(stream, source.Width, source.Height);
            return stream.ToArray();
        }
    }
}
