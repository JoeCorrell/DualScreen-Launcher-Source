using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Marks item slots that already use Valheim's compact inventory-cell art.
    /// This prevents the final button-tree styling pass from replacing that
    /// exact slot treatment with the unrelated square-button frame.
    /// </summary>
    internal sealed class PlainControl : MonoBehaviour
    {
    }

    /// <summary>
    /// Applies the lower-screen control artwork without stretching its corners.
    /// </summary>
    internal static class UiSkin
    {
        private const float SquareTolerance = 0.16f;
        private const string ArtworkName = "__ControlArtwork";
        private const string InteractionName = "__InteractionTint";
        private const string SelectionName = "__SelectionAccent";
        private const string HeaderWashName = "__HeaderWash";
        private const string PageHeadingName = "__PageHeading";
        private const string PageSurfaceName = "__PageSurface";

        private static readonly Color SelectionGold =
            new Color(0.98f, 0.64f, 0.16f, 0.92f);

        public static void Rectangle(Image image, Color tint)
        {
            Apply(image, "ButtonBackground", tint, 0.76f);
        }

        public static void Square(Image image, Color tint)
        {
            Apply(image, "CategoryBackground", tint, 0.76f);
        }

        /// <summary>
        /// Uses Valheim's inventory-cell art when it is available.  There is no
        /// custom-art fallback here: a missing stock slot remains a clean dark
        /// cell instead of receiving an unrelated button texture.
        /// </summary>
        public static void Slot(Image image, Color tint)
        {
            if (image == null) return;
            if (image.GetComponent<PlainControl>() == null)
                image.gameObject.AddComponent<PlainControl>();
            Apply(image, "SlotBackground", tint, 0.74f);
        }

        /// <summary>Native selected-slot art, with a restrained tint fallback.</summary>
        public static void SlotSelection(Image image)
        {
            if (image == null) return;
            Sprite sprite = Textures.Get("SlotSelection");
            image.sprite = sprite;
            image.type = SliceType(sprite);
            image.color = sprite != null
                ? new Color(1f, 0.82f, 0.46f, 0.96f)
                : Color.clear;
            image.raycastTarget = false;
        }

        public static void Control(Image image, RectTransform rect)
        {
            if (image == null || rect == null) return;
            if (image.GetComponentInParent<PlainControl>() != null) return;
            float width = Mathf.Abs(rect.rect.width);
            float height = Mathf.Abs(rect.rect.height);
            bool square = width > 0f && height > 0f
                          && Mathf.Abs(width - height) / Mathf.Max(width, height)
                          <= SquareTolerance;
            Apply(image, square ? "CategoryBackground" : "ButtonBackground",
                  image.color, 0.76f);
        }

        /// <summary>Skins every actual Button in the custom lower-screen tree.</summary>
        public static void ControlsUnder(RectTransform root)
        {
            if (root == null) return;
            foreach (Button button in root.GetComponentsInChildren<Button>(true))
            {
                if (button == null) continue;
                Image image = button.targetGraphic as Image
                              ?? button.GetComponent<Image>();
                RectTransform? rect = button.transform as RectTransform;
                // Slots intentionally keep Valheim's inventory-cell art, but
                // they still need the same immediate pressed feedback as every
                // other touchscreen control. Only skip the artwork replacement.
                if (rect != null
                    && button.GetComponentInParent<PlainControl>() == null)
                    Control(image, rect);
                Polish(button, image);
            }
        }

        /// <summary>
        /// Gives every full page the same restrained tint while leaving the
        /// shared panel texture visible. This is deliberately an overlay, not
        /// another framed panel, so adjacent cards remain separated by tint and
        /// spacing instead of a stack of borders.
        /// </summary>
        public static void PageSurface(RectTransform page)
        {
            if (page == null || page.Find(PageSurfaceName) != null) return;
            var go = new GameObject(PageSurfaceName,
                typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(page, false);
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = Vector2.zero;
            rect.offsetMax = Vector2.zero;
            var image = go.GetComponent<Image>();
            image.color = UiTheme.PageWash;
            image.raycastTarget = false;
            rect.SetAsFirstSibling();
        }

        /// <summary>Normalises a tint-backed section and its quiet header wash.</summary>
        public static void Panel(RectTransform panel, float headerHeight = 0f,
                                 bool raised = false)
        {
            if (panel == null) return;
            var image = panel.GetComponent<Image>() ?? panel.gameObject.AddComponent<Image>();
            Apply(image, "PanelFill",
                raised ? UiTheme.PanelRaised : UiTheme.Panel, raised ? 0.60f : 0.50f);
            image.raycastTarget = false;
            if (headerHeight > 0f) HeaderWash(panel, headerHeight);
        }

        /// <summary>
        /// Full-screen stock panel grain with a stable embedded fallback. The
        /// torn inventory-panel rim is cropped away first, then the clean inner
        /// material is stretched once across the entire lower display. The art
        /// remains a separate child so brightness can be adjusted independently.
        /// </summary>
        public static void Backdrop(Image image, Color tint)
        {
            Apply(image, "PanelFill", tint, 0.88f);
            SetArtworkBrightness(image, 0.62f);
        }

        public static void SetArtworkBrightness(Image host, float brightness)
        {
            if (host == null) return;
            Transform child = host.transform.Find(ArtworkName);
            var artwork = child != null ? child.GetComponent<Image>() : null;
            if (artwork == null) return;
            float level = Mathf.Clamp(brightness, 0.30f, 1.25f);
            Color current = artwork.color;
            artwork.color = new Color(level, level, level, current.a);
        }

        /// <summary>Adds consistent pressed, hover and disabled feedback.</summary>
        private static void Polish(Button button, Image target)
        {
            if (button == null || target == null) return;
            button.targetGraphic = target;
            button.transition = Selectable.Transition.ColorTint;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            button.colors = new ColorBlock
            {
                normalColor = Color.white,
                highlightedColor = new Color(1f, 0.94f, 0.82f, 1f),
                pressedColor = new Color(0.78f, 0.62f, 0.40f, 1f),
                selectedColor = Color.white,
                disabledColor = new Color(0.46f, 0.44f, 0.40f, 0.62f),
                colorMultiplier = 1f,
                fadeDuration = 0.06f
            };

            var root = button.transform as RectTransform;
            if (root == null) return;
            Transform existing = root.Find(InteractionName);
            Image overlay;
            if (existing != null)
            {
                overlay = existing.GetComponent<Image>();
            }
            else
            {
                var go = new GameObject(InteractionName,
                    typeof(RectTransform), typeof(Image));
                var rect = (RectTransform)go.transform;
                rect.SetParent(root, false);
                rect.anchorMin = Vector2.zero;
                rect.anchorMax = Vector2.one;
                rect.offsetMin = new Vector2(3f, 3f);
                rect.offsetMax = new Vector2(-3f, -3f);
                overlay = go.GetComponent<Image>();
                overlay.raycastTarget = false;
                // Above the artwork, below the control's icon and text.
                rect.SetSiblingIndex(Mathf.Min(1, root.childCount - 1));
            }
            overlay.color = Color.clear;

            var feedback = button.GetComponent<UiInteractionFeedback>()
                           ?? button.gameObject.AddComponent<UiInteractionFeedback>();
            feedback.Configure(button, overlay);
        }

        /// <summary>
        /// Adds a slim selection cue without putting a border around the card.
        /// </summary>
        public static Image SelectionAccent(RectTransform root, bool right = false)
        {
            if (root == null) return null;
            Transform existing = root.Find(SelectionName);
            if (existing != null) return existing.GetComponent<Image>();

            var go = new GameObject(SelectionName,
                typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(root, false);
            float x = right ? 1f : 0f;
            rect.anchorMin = new Vector2(x, 0.20f);
            rect.anchorMax = new Vector2(x, 0.80f);
            rect.pivot = new Vector2(x, 0.5f);
            rect.sizeDelta = new Vector2(5f, 0f);
            rect.anchoredPosition = new Vector2(right ? -6f : 6f, 0f);
            var image = go.GetComponent<Image>();
            image.color = SelectionGold;
            image.raycastTarget = false;
            image.enabled = false;
            return image;
        }

        public static void SetSelected(Image accent, bool selected)
        {
            if (accent != null && accent.enabled != selected)
                accent.enabled = selected;
        }

        /// <summary>A faint header tint that separates sections without a frame.</summary>
        public static void HeaderWash(RectTransform panel, float height = 58f)
        {
            if (panel == null || panel.Find(HeaderWashName) != null) return;
            var go = new GameObject(HeaderWashName,
                typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(panel, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.anchoredPosition = Vector2.zero;
            rect.sizeDelta = new Vector2(0f, height);
            var image = go.GetComponent<Image>();
            image.color = new Color(0.58f, 0.30f, 0.055f, 0.075f);
            image.raycastTarget = false;
            Transform artwork = panel.Find(ArtworkName);
            rect.SetSiblingIndex(artwork != null
                ? Mathf.Min(artwork.GetSiblingIndex() + 1, panel.childCount - 1)
                : 0);
        }

        /// <summary>
        /// Gives full-page headings the same quiet hierarchy as the dashboard
        /// cards without drawing a frame around the page.
        /// </summary>
        public static void PageHeading(RectTransform page, TMP_Text title,
                                       float height = 72f)
        {
            if (page == null || title == null || page.Find(PageHeadingName) != null)
                return;

            var go = new GameObject(PageHeadingName,
                typeof(RectTransform), typeof(Image));
            var rect = (RectTransform)go.transform;
            rect.SetParent(page, false);
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(1f, 1f);
            rect.pivot = new Vector2(0.5f, 1f);
            rect.anchoredPosition = Vector2.zero;
            rect.sizeDelta = new Vector2(0f, Mathf.Max(48f, height));

            var wash = go.GetComponent<Image>();
            wash.color = new Color(0.50f, 0.27f, 0.065f, 0.055f);
            wash.raycastTarget = false;

            // Two short rules and a centre lozenge read as an ornament rather
            // than the panel border this UI deliberately avoids.
            HeadingRule(rect, 0.16f, 0.475f, height);
            HeadingRule(rect, 0.525f, 0.84f, height);

            var jewelObject = new GameObject("Jewel",
                typeof(RectTransform), typeof(Image));
            var jewelRect = (RectTransform)jewelObject.transform;
            jewelRect.SetParent(rect, false);
            jewelRect.anchorMin = new Vector2(0.5f, 1f);
            jewelRect.anchorMax = new Vector2(0.5f, 1f);
            jewelRect.pivot = new Vector2(0.5f, 0.5f);
            jewelRect.anchoredPosition = new Vector2(0f, -height + 6f);
            jewelRect.sizeDelta = new Vector2(8f, 8f);
            jewelRect.localRotation = Quaternion.Euler(0f, 0f, 45f);
            var jewel = jewelObject.GetComponent<Image>();
            jewel.color = new Color(0.92f, 0.57f, 0.15f, 0.52f);
            jewel.raycastTarget = false;

            rect.SetAsFirstSibling();
        }

        private static void HeadingRule(RectTransform parent, float left,
                                        float right, float height)
        {
            var lineObject = new GameObject("Rule",
                typeof(RectTransform), typeof(Image));
            var lineRect = (RectTransform)lineObject.transform;
            lineRect.SetParent(parent, false);
            lineRect.anchorMin = new Vector2(left, 1f);
            lineRect.anchorMax = new Vector2(right, 1f);
            lineRect.pivot = new Vector2(0.5f, 0.5f);
            lineRect.anchoredPosition = new Vector2(0f, -height + 6f);
            lineRect.sizeDelta = new Vector2(0f, 2f);
            var line = lineObject.GetComponent<Image>();
            line.color = new Color(0.92f, 0.57f, 0.15f, 0.26f);
            line.raycastTarget = false;
        }

        /// <summary>Keeps inventory-like slots free of decorative artwork.</summary>
        public static void Plain(Image image, Color tint)
        {
            if (image == null) return;
            if (image.GetComponent<PlainControl>() == null)
                image.gameObject.AddComponent<PlainControl>();

            Transform artwork = image.transform.Find(ArtworkName);
            if (artwork != null)
            {
                artwork.gameObject.SetActive(false);
                UnityEngine.Object.Destroy(artwork.gameObject);
            }

            image.sprite = null;
            image.type = Image.Type.Simple;
            image.color = tint;
        }

        private static void Apply(Image image, string texture, Color tint,
                                  float artworkAlpha)
        {
            if (image == null) return;
            Sprite sprite = Textures.Get(texture);
            var root = image.transform as RectTransform;
            if (sprite != null && root != null)
            {
                /*
                 * Keep state tint and artwork separate.
                 *
                 * Most controls use deliberately near-black idle colours. If
                 * that colour multiplies the PNG itself, its bronze border and
                 * grain disappear and the result looks like the old flat box.
                 * The root remains the Button's tint/target graphic; a
                 * non-interactive first child carries the texture at its real
                 * brightness, behind every icon and label.
                 */
                image.sprite = null;
                image.type = Image.Type.Simple;

                Transform existing = root.Find(ArtworkName);
                Image artwork;
                if (existing != null)
                {
                    artwork = existing.GetComponent<Image>();
                }
                else
                {
                    var go = new GameObject(ArtworkName,
                        typeof(RectTransform), typeof(Image));
                    var rect = (RectTransform)go.transform;
                    rect.SetParent(root, false);
                    rect.anchorMin = Vector2.zero;
                    rect.anchorMax = Vector2.one;
                    rect.offsetMin = Vector2.zero;
                    rect.offsetMax = Vector2.zero;
                    artwork = go.GetComponent<Image>();
                }

                artwork.sprite = sprite;
                artwork.type = SliceType(sprite);
                artwork.fillCenter = true;
                artwork.preserveAspect = false;
                // Valheim's inventory cell is a grayscale tint mask. Copying
                // the sprite at pure white turns every empty slot into a white
                // tile; the stock InventoryGrid supplies its darkness through
                // Image.color. Reapply that native-looking iron/brown tint here
                // while the host Image remains free to show equipped/drag state.
                Color artworkTint = texture == "SlotBackground"
                    ? new Color(0.31f, 0.245f, 0.18f, 1f)
                    : Color.white;
                artwork.color = new Color(
                    artworkTint.r, artworkTint.g, artworkTint.b,
                    Mathf.Clamp01(artworkAlpha * artworkTint.a));
                artwork.raycastTarget = false;
                artwork.transform.SetAsFirstSibling();
            }
            else
            {
                image.sprite = null;
                image.type = Image.Type.Simple;
            }
            image.color = tint;
        }

        private static Image.Type SliceType(Sprite sprite)
        {
            if (sprite == null) return Image.Type.Simple;
            Vector4 border = sprite.border;
            return border.sqrMagnitude > 0.01f
                ? Image.Type.Sliced : Image.Type.Simple;
        }
    }
}
