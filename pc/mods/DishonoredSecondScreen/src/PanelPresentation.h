#pragma once
#include "../../DeadIslandSecondScreen/src/Canvas.h"

namespace dishonored {
// One completed image reaches the window DC. In particular, never expose a
// black clear between frames to Wine's surface-update/compositor path.
inline bool PresentPanelFrame(HDC target, const dis::Canvas& source, dis::Canvas& staging,
    const RECT& client, const RECT& fit) {
    if (!target || !source.ready() || client.right <= 0 || client.bottom <= 0 ||
        client.right > 4096 || client.bottom > 4096 || fit.left < 0 || fit.top < 0 ||
        fit.right > client.right || fit.bottom > client.bottom ||
        fit.right <= fit.left || fit.bottom <= fit.top) return false;
    if (client.right == source.width() && client.bottom == source.height() &&
        fit.left == 0 && fit.top == 0 && fit.right == client.right && fit.bottom == client.bottom)
        return BitBlt(target, 0, 0, client.right, client.bottom, source.dc(), 0, 0, SRCCOPY) != FALSE;
    if ((!staging.ready() || staging.width() != client.right || staging.height() != client.bottom) &&
        !staging.Create(client.right, client.bottom)) return false;
    staging.Clear(dis::Color(0, 0, 0));
    SetStretchBltMode(staging.dc(), HALFTONE);
    SetBrushOrgEx(staging.dc(), 0, 0, nullptr);
    if (!StretchBlt(staging.dc(), fit.left, fit.top, fit.right - fit.left, fit.bottom - fit.top,
        source.dc(), 0, 0, source.width(), source.height(), SRCCOPY)) return false;
    GdiFlush();
    return BitBlt(target, 0, 0, client.right, client.bottom, staging.dc(), 0, 0, SRCCOPY) != FALSE;
}
}
