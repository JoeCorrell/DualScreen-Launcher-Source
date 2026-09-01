using UnityEngine;
using UnityEngine.UI;

namespace ValheimSecondScreen.UI
{
    /// <summary>A resolution-independent vertical colour ramp for a UI graphic.</summary>
    internal sealed class VitalGradient : BaseMeshEffect
    {
        private Color _bottom = Color.black;
        private Color _top = Color.white;

        public void SetColors(Color bottom, Color top)
        {
            if (_bottom == bottom && _top == top) return;
            _bottom = bottom;
            _top = top;
            if (graphic != null) graphic.SetVerticesDirty();
        }

        public override void ModifyMesh(VertexHelper vertices)
        {
            if (!IsActive() || vertices == null || vertices.currentVertCount == 0) return;

            var vertex = new UIVertex();
            float minY = float.MaxValue;
            float maxY = float.MinValue;
            for (int i = 0; i < vertices.currentVertCount; i++)
            {
                vertices.PopulateUIVertex(ref vertex, i);
                minY = Mathf.Min(minY, vertex.position.y);
                maxY = Mathf.Max(maxY, vertex.position.y);
            }

            float height = Mathf.Max(0.0001f, maxY - minY);
            for (int i = 0; i < vertices.currentVertCount; i++)
            {
                vertices.PopulateUIVertex(ref vertex, i);
                float amount = Mathf.Clamp01((vertex.position.y - minY) / height);
                Color tint = Color.Lerp(_bottom, _top, amount);
                vertex.color = (Color32)((Color)vertex.color * tint);
                vertices.SetUIVertex(vertex, i);
            }
        }
    }
}
