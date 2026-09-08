#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <cstring>
#include "State.h"

namespace ac2ds {
// GDI+ draws directly into the presentation DIB. Graphics(HDC) under Wine
// otherwise repeatedly transfers rasterized primitives through the GDI path.
class PanelSurface {
    HDC dc_{};
    HBITMAP dib_{};
    HGDIOBJ old_{};
    void* pixels_{};
    std::unique_ptr<Gdiplus::Bitmap> image_;
public:
    explicit PanelSurface(HDC reference) {
        dc_ = CreateCompatibleDC(reference);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = Width; info.bmiHeader.biHeight = -Height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels{};
        dib_ = CreateDIBSection(reference,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        pixels_=pixels;
        if (dc_ && dib_ && pixels) {
            old_ = SelectObject(dc_,dib_);
            image_.reset(new Gdiplus::Bitmap(Width,Height,Width*4,PixelFormat32bppRGB,static_cast<BYTE*>(pixels)));
            if (image_->GetLastStatus() != Gdiplus::Ok) image_.reset();
        }
    }
    PanelSurface(const PanelSurface&) = delete;
    PanelSurface& operator=(const PanelSurface&) = delete;
    ~PanelSurface() {
        image_.reset();
        if (dc_ && old_ && old_ != HGDI_ERROR) SelectObject(dc_,old_);
        if (dib_) DeleteObject(dib_);
        if (dc_) DeleteDC(dc_);
    }
    explicit operator bool() const { return image_ && old_ && old_ != HGDI_ERROR; }
    Gdiplus::Bitmap* Image() const { return image_.get(); }
    uint32_t* Pixels() const { return static_cast<uint32_t*>(pixels_); }
    void CopyFrom(const PanelSurface& source) {
        if (*this && source) std::memcpy(pixels_,source.pixels_,size_t(Width)*Height*4);
    }
    void Present(HDC target,int width,int height) const {
        if (!*this || width <= 0 || height <= 0) return;
        if (width == Width && height == Height) BitBlt(target,0,0,width,height,dc_,0,0,SRCCOPY);
        else {
            SetStretchBltMode(target,HALFTONE);
            SetBrushOrgEx(target,0,0,nullptr);
            StretchBlt(target,0,0,width,height,dc_,0,0,Width,Height,SRCCOPY);
        }
        // Finish any pending GDI read before the next direct DIB write.
        GdiFlush();
    }
};
}
