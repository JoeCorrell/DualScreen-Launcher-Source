using UnityEngine;
using UnityEngine.EventSystems;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Prevents a touch-selected lower-screen button from becoming a controller
    /// submit target on a later frame. Pointer clicks still run normally, while
    /// gamepad focus remains owned by Valheim's gameplay and top-screen menus.
    /// </summary>
    internal sealed class TouchSelectionGuard : MonoBehaviour
    {
        private void LateUpdate()
        {
            EventSystem? events = EventSystem.current;
            GameObject? selected = events != null ? events.currentSelectedGameObject : null;
            if (events != null && selected != null
                && selected.transform.IsChildOf(transform))
                events.SetSelectedGameObject(null);
        }
    }
}
