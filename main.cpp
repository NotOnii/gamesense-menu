// Gamesense menu rebuild - D3D9, pixel-matched against ref/*.png captures.
// Text: GDI ClearType-natural glyphs rendered one at a time, alpha = gamesense's (54R+183G+19B)>>8 of the RGB
// coverage, glyph cell clipped to abcB (matches the reference capture to +-1 per channel).
// Run: gsmenu.exe [--capture out.bmp]   (test hooks: GS_EVENTS input replay, GS_RESIZE backbuffer size)
// Build: build.bat. Regression check: node diff.js out.bmp ref/misc.bgra
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <d3d9.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <map>
#include <cmath>
#include <string>
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

static IDirect3DDevice9* dev;
static const int WW = 669, WH = 564;

// DPI: "DPI scale" index -> factor table 0x434745cc {1, 1.25, 1.5, 1.75, 2}, current factor DAT_4346d490.
// Every scaled constant goes through FUN_433cc0d6 = cvtss2si(v * dpi): round to nearest, ties to even.
static const float DPI_TABLE[5] = {1.f, 1.25f, 1.5f, 1.75f, 2.f};
static float dpi = 1.f;
static int S(float v) { return (int)lrintf(v * dpi); }

// ---------------------------------------------------------------- draw primitives
// Same pipeline as gamesense (Dear ImGui DX9 backend, FUN_433c95ce): untransformed XYZ verts, FVF 0x142,
// identity world/view, ortho projection with +0.5 offsets, LINEAR sampling.
struct Vtx { float x, y, z; D3DCOLOR c; float u, v; };
#define VFVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
static D3DCOLOR rgb(int r, int g, int b, int a = 255) { return D3DCOLOR_ARGB(a, r, g, b); }
static D3DCOLOR gray(int v, int a = 255) { return rgb(v, v, v, a); }

static float globalAlpha = 1.f;  // CMenu fade (IRenderer::SetAlpha)
static D3DCOLOR fa(D3DCOLOR c) { return globalAlpha >= 1.f ? c : (c & 0xffffff) | ((D3DCOLOR)((c >> 24) * globalAlpha) << 24); }
static void quad(float x, float y, float w, float h, D3DCOLOR c, IDirect3DTexture9* t = nullptr,
                 float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    c = fa(c);
    Vtx v[4] = {{x, y, 0, c, u0, v0}, {x + w, y, 0, c, u1, v0}, {x, y + h, 0, c, u0, v1}, {x + w, y + h, 0, c, u1, v1}};
    dev->SetTexture(0, t);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, t ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, t ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
}
static void rect(int x, int y, int w, int h, D3DCOLOR c) { quad((float)x, (float)y, (float)w, (float)h, c); }
static void outline(int x, int y, int w, int h, D3DCOLOR c) {
    rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c); rect(x, y + 1, 1, h - 2, c); rect(x + w - 1, y + 1, 1, h - 2, c);
}
// vertical gradient as one vertex-colored quad: the GPU interpolates at pixel centers, which is what the
// reference shows (e.g. off-checkbox 80->50 over 6 rows lands on .5 ties resolved by GPU float precision)
static void gradV(int x, int y, int w, int h, D3DCOLOR t, D3DCOLOR b) {
    float X = (float)x, Y = (float)y; t = fa(t); b = fa(b);
    Vtx v[4] = {{X, Y, 0, t, 0, 0}, {X + w, Y, 0, t, 0, 0}, {X, Y + h, 0, b, 0, 0}, {X + w, Y + h, 0, b, 0, 0}};
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
}
static void gradH(int x, int y, int w, int h, D3DCOLOR l, D3DCOLOR r) {  // one quad, like ImGui AddRectFilledMultiColor
    float X = (float)x, Y = (float)y; l = fa(l); r = fa(r);
    Vtx v[4] = {{X, Y, 0, l, 0, 0}, {X + w, Y, 0, r, 0, 0}, {X, Y + h, 0, l, 0, 0}, {X + w, Y + h, 0, r, 0, 0}};
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
}

// ---------------------------------------------------------------- textures
static IDirect3DTexture9* makeTex(int w, int h, const uint32_t* argb) {
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
    D3DLOCKED_RECT lr;
    t->LockRect(0, &lr, nullptr, 0);
    for (int y = 0; y < h; y++) memcpy((char*)lr.pBits + y * lr.Pitch, argb + y * w, w * 4);
    t->UnlockRect(0);
    return t;
}
struct Image { IDirect3DTexture9* tex = nullptr; int w = 0, h = 0; };
#include "assets_data.h"
static Image loadPng(const char* name) {  // decodes an embedded PNG (assets_data.h) - nothing is read from disk
    Image im;
    const EmbeddedAsset* a = nullptr;
    for (auto& e : EMBEDDED_ASSETS) if (!strcmp(e.name, name)) a = &e;
    if (!a) { fprintf(stderr, "missing embedded asset %s\n", name); return im; }
    IWICImagingFactory* f = nullptr; IWICStream* st = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    if (f && SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)a->data, a->size)) &&
        SUCCEEDED(f->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnLoad, &d)) &&
        SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w, h; cv->GetSize(&w, &h);
        std::vector<uint32_t> px(w * h);
        cv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)px.data());
        im.tex = makeTex(w, h, px.data()); im.w = w; im.h = h;
    } else {
        fprintf(stderr, "bad embedded asset %s\n", name);
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (d) d->Release(); if (st) st->Release(); if (f) f->Release();
    return im;
}

// 4x4 dither tile used for the window background, phase is absolute to the window origin
static IDirect3DTexture9* bgTile;
static int bgOX = 0, bgOY = 0;  // pattern origin = menu position (texture is locked to the menu)
static void texBg(int x, int y, int w, int h) {
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    quad((float)x, (float)y, (float)w, (float)h, 0xffffffff, bgTile, (x - bgOX) / 4.f, (y - bgOY) / 4.f, (x - bgOX + w) / 4.f, (y - bgOY + h) / 4.f);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

// ---------------------------------------------------------------- fonts
// ClearType coverage -> alpha. GDI emits 7 levels per channel on the capture machine; those use gamesense's
// FUN_4332f22f weighting, any other level (other ClearType tuning) falls back to Rec.709 luma.
static int ctAlpha(int r, int g, int b) {
    static const int LEVELS[7] = {0, 58, 102, 144, 182, 219, 255};
    auto known = [](int v) { for (int l : LEVELS) if (l == v) return true; return false; };
    if (!known(r) || !known(g) || !known(b)) return (int)(0.2126f * r + 0.7152f * g + 0.0722f * b);
    return (54 * r + 183 * g + 19 * b) >> 8;  // gamesense FUN_4332f22f
}
enum { FX_NONE = 0, FX_SHADOW = 1, FX_OUTLINE = 2 };
struct Glyph { IDirect3DTexture9* tex; int a, b, c, w, h, ox, oy; };
struct Font {
    HFONT hf = nullptr; HDC dc = nullptr; HBITMAP bm = nullptr; uint32_t* bits = nullptr; int quality = 0, fx = 0, outlineAlpha = 0, height = 0;
    std::map<wchar_t, Glyph> cache;
    static const int BW = 96, BH = 48, PAD = 16;
    void release() {  // DPI change: the renderer rebuilds its fonts (vtable +0x80 from 0x4339f0ad)
        if (!dc) return;
        for (auto& [ch, g] : cache) if (g.tex) g.tex->Release();
        cache.clear();
        DeleteObject(hf); DeleteObject(bm); DeleteDC(dc); dc = nullptr;
    }
    void init(const char* face, int h, int weight, int q, int effects, int olA = 0) {
        release();
        quality = q; fx = effects; outlineAlpha = olA;
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), BW, -BH, 1, 32}};
        bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        SelectObject(dc, bm);
        // mirrors gamesense FUN_4334e194: LOGFONTW zeroed, lfHeight = -size (em px), weight 400/700,
        // quality 3 (non-AA) or 6 (ClearType natural), everything else 0 (ANSI charset, default precision/pitch)
        LOGFONTW lf = {};
        lf.lfHeight = -h; lf.lfWeight = weight; lf.lfQuality = (BYTE)q;
        MultiByteToWideChar(CP_ACP, 0, face, -1, lf.lfFaceName, 32);
        hf = CreateFontIndirectW(&lf);
        SetMapMode(dc, MM_TEXT); SetTextAlign(dc, TA_TOP | TA_LEFT);
        SelectObject(dc, hf);
        SetTextColor(dc, 0xffffff); SetBkMode(dc, TRANSPARENT);
        TEXTMETRICA tm; GetTextMetricsA(dc, &tm); height = tm.tmHeight;
    }
    const Glyph& get(wchar_t ch) {
        auto it = cache.find(ch);
        if (it != cache.end()) return it->second;
        ABC abc; GetCharABCWidthsW(dc, ch, ch, &abc);
        memset(bits, 0, BW * BH * 4);
        ExtTextOutW(dc, PAD - abc.abcA, 0, 0, nullptr, &ch, 1, nullptr);
        GdiFlush();
        const int fr = 0;  // glyph cell = abcB exactly (GS keeps no ClearType fringe outside it)
        int gw = abc.abcB + 2 * fr, gh = height, e = fx ? 1 : 0;
        int tw = gw + 2 * e, th = gh + 2 * e;  // shadow/outline grow the cell by 1px
        std::vector<int> al(tw * th, 0);
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                uint32_t p = bits[y * BW + PAD - fr + x];
                int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
                al[(y + e) * tw + x + e] = quality == NONANTIALIASED_QUALITY ? (g ? 255 : 0) : ctAlpha(r, g, b);
            }
        std::vector<uint32_t> px(tw * th);
        for (int i = 0; i < tw * th; i++) px[i] = al[i] ? (al[i] << 24) | 0xffffff : 0;
        // the scaled (ClearType) hotkey font shows its effect as a +1,+1 shadow weighted by coverage, not the flat
        // 8-neighbour outline the 100% bitmap font has (pixel-checked against a GS 125% capture)
        const int eff = (fx == FX_OUTLINE && quality != NONANTIALIASED_QUALITY) ? FX_SHADOW : fx;
        if (eff == FX_SHADOW)  // Source-style: empty pixel takes alpha of (x-1,y-1), colored black
            for (int y = th - 1; y >= 1; y--)
                for (int x = tw - 1; x >= 1; x--)
                    if (!al[y * tw + x] && al[(y - 1) * tw + x - 1]) px[y * tw + x] = (uint32_t)al[(y - 1) * tw + x - 1] << 24;
        if (eff == FX_OUTLINE)
            for (int y = 0; y < th; y++)
                for (int x = 0; x < tw; x++) {
                    if (al[y * tw + x]) continue;
                    bool any = false;  // FUN_4332f22f flag 4: empty pixel next to any glyph pixel -> 0xDC000000 (flat)
                    for (int dy = -1; dy <= 1 && !any; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int X = x + dx, Y = y + dy;
                            if (X >= 0 && Y >= 0 && X < tw && Y < th && al[Y * tw + X]) { any = true; break; }
                        }
                    if (any) px[y * tw + x] = (uint32_t)outlineAlpha << 24;
                }
        Glyph g{gw > 0 ? makeTex(tw, th, px.data()) : nullptr, abc.abcA, (int)abc.abcB, abc.abcC, tw, th, e + fr, e};
        if (eff == FX_SHADOW) { g.ox = fr; g.oy = 0; }  // shadow only grows right/bottom
        return cache[ch] = g;
    }
    int width(const std::wstring& s) { int w = 0; for (wchar_t c : s) { auto& g = get(c); w += g.a + g.b + g.c; } return w; }
    void draw(int x, int y, const std::wstring& s, D3DCOLOR col, int limit = 0x7fffffff) {
        int pen = x;
        for (wchar_t c : s) {
            auto& g = get(c);
            if (limit != 0x7fffffff && pen + g.a + g.b >= limit) break;
            if (g.tex) quad((float)(pen + g.a - g.ox), (float)(y - g.oy), (float)g.w, (float)g.h, col, g.tex);
            pen += g.a + g.b + g.c;
        }
    }
};
static Font fNormal, fBold, fSmall, fValue;

// ---------------------------------------------------------------- widgets (geometry measured from ref captures)
struct Accent { int r, g, b; } accent = {158, 202, 42};  // menu color; widgets offset it (fit on GPU: centers-interpolated quads)

#include "ui.h"

// ---------------------------------------------------------------- menu contents: generated from the image's builder (0x4348704a)
static Menu menu;
static void resetMenuLayout() {  // groupbox positions/sizes only; menu size and DPI untouched
    for (Tab* t : menu.tabs) for (Child* c : t->children) c->resetLayout();
    menu.relayout();
}
static void initFonts() {  // renderer font slots; pixel size = (int)(em size * dpi + 0.5) (FUN_4334e194)
    auto px = [](float size) { return (int)(size * dpi + 0.5f); };
    fNormal.init("Verdana", px(9), FW_NORMAL, CLEARTYPE_NATURAL_QUALITY, FX_NONE);
    fBold.init("Verdana", px(9), FW_BOLD, CLEARTYPE_NATURAL_QUALITY, FX_NONE);
    // Hotkey slot: text picks the unscaled set (backend +0x108) at 100% and the rebuilt scaled set (+0x15C) otherwise
    // (0x434172EC). Measured: the unscaled set has the bitmap Small Fonts 7 (exact vs ref), the scaled one ClearType
    // Verdana at (int)(7 * dpi + 0.5) (glyph values match a GS 125% capture to +-2).
    if (dpi == 1.f) fSmall.init("Small Fonts", 7, FW_NORMAL, NONANTIALIASED_QUALITY, FX_OUTLINE, 0xDC);
    else fSmall.init("Verdana", px(7), FW_NORMAL, CLEARTYPE_NATURAL_QUALITY, FX_OUTLINE, 0xDC);
    fValue.init("Verdana", px(9), FW_BOLD, CLEARTYPE_NATURAL_QUALITY, FX_NONE);  // font slot 1 (slider values, flags 0x31): fits ref exactly
}
// "DPI scale" changed (callback 0x433379d4 -> 0x4339f0ad -> per menu 0x433b23fc): new factor, fonts rebuilt, menu size reset to
// S(default), width clamps S(40/200/100), every tab/groupbox/element rescaled (HandleDPI), tabs restacked from y = 20.
static void applyDpi(int step) {
    if (step < 0 || step > 4) step = 0;
    dpiStep = step; dpi = DPI_TABLE[step];
    initFonts();
    menu.minW = menu.sw = S(WW); menu.minH = menu.sh = S(WH);
    int ty = 20;
    for (Tab* t : menu.tabs) { t->HandleDPI(); t->x = t->lp + 6; t->y = ty; ty += t->h; }
    menu.relayout();
}
#include "menu_data.h"
#include "menu_rules.h"
static void buildMenu() {
    for (int i = 0; i < 8; i++) menu.tabs.push_back(new Tab(i, &menu, 6, 20 + 64 * i));
    buildMenuData();
    applyRules();
    E[376]->visible = true;  // shortcut: "Fix animations" is hidden by the builder but shown in the real menu (ref); whatever unhides it at runtime is not found yet
    applyDpi(0);  // first pass at 100% records every element's default size
    on(360, [] { applyDpi(cov(360)); });  // "DPI scale" combo (value at 0x43475a94)
    if (cov(360)) applyDpi(cov(360));
}

// ---------------------------------------------------------------- app
static void render() {
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0);
    dev->BeginScene();
    menu.render();
    dev->EndScene();
}
// GS_EVENTS="down:x,y;up:x,y;rdown:x,y;rup:x,y;move:x,y;key:vk;wheel:x,y,delta[,ctrl]" - replay input through the menu before --capture
static void replayEvents() {
    const char* e = getenv("GS_EVENTS"); if (!e) return;
    std::string all(e); size_t p = 0; WPARAM held = 0;
    while (p < all.size()) {
        size_t q = all.find(";", p); std::string t = all.substr(p, q == std::string::npos ? std::string::npos : q - p); p = q == std::string::npos ? all.size() : q + 1;
        int a = 0, b = 0, c = 0, d = 0; char kind[16] = {}; sscanf(t.c_str(), "%15[a-z]:%d,%d,%d,%d", kind, &a, &b, &c, &d);
        std::string k(kind);
        if (k == "down") { held |= MK_LBUTTON; menu.handle({WM_LBUTTONDOWN, held, MAKELPARAM(a, b)}); }
        else if (k == "up") { held &= ~(WPARAM)MK_LBUTTON; menu.handle({WM_LBUTTONUP, held, MAKELPARAM(a, b)}); }
        else if (k == "move") menu.handle({WM_MOUSEMOVE, held, MAKELPARAM(a, b)});
        else if (k == "rdown") { held |= MK_RBUTTON; menu.handle({WM_RBUTTONDOWN, held, MAKELPARAM(a, b)}); }
        else if (k == "rup") { held &= ~(WPARAM)MK_RBUTTON; menu.handle({WM_RBUTTONUP, held, MAKELPARAM(a, b)}); }
        else if (k == "key") menu.handle({WM_KEYDOWN, (WPARAM)a, 0});
        else if (k == "wheel") { POINT pt{a, b}; ClientToScreen(menu.hwnd, &pt); menu.handle({WM_MOUSEWHEEL, MAKEWPARAM(d ? MK_CONTROL : 0, (short)c), MAKELPARAM(pt.x, pt.y)}); }  // wheel lParam is in screen coords
    }
}
static int bbW = WW, bbH = WH;  // current backbuffer size (capture mode stays at the ref size unless GS_RESIZE)
static bool saveBmp(const char* path) {
    IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    dev->CreateOffscreenPlainSurface(bbW, bbH, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    if (FAILED(dev->GetRenderTargetData(bb, sys))) return false;
    D3DLOCKED_RECT lr; sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    FILE* f = fopen(path, "wb");
    BITMAPFILEHEADER fh = {0x4d42, (DWORD)(54 + bbW * bbH * 4), 0, 0, 54};
    BITMAPINFOHEADER ih = {sizeof(ih), bbW, -bbH, 1, 32};
    fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
    for (int y = 0; y < bbH; y++) fwrite((char*)lr.pBits + y * lr.Pitch, 4, bbW, f);
    fclose(f); sys->UnlockRect(); sys->Release(); bb->Release();
    return true;
}
// ---------------------------------------------------------------- device state / resize
static int pendW = 0, pendH = 0;         // client size from WM_SIZE, applied by resetDevice() in the loop
static D3DPRESENT_PARAMETERS gpp = {};
static void setupState() {
    dev->SetFVF(VFVF);
    {  // ImGui_ImplDX9 projection: L=T=0.5, R=W+0.5, B=H+0.5 (DisplayPos 0,0)
        float L = 0.5f, R = bbW + 0.5f, T = 0.5f, B = bbH + 0.5f;
        D3DMATRIX id = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        D3DMATRIX pr = {2.f / (R - L), 0, 0, 0, 0, 2.f / (T - B), 0, 0, 0, 0, 0.5f, 0, (L + R) / (L - R), (T + B) / (B - T), 0.5f, 1};
        dev->SetTransform(D3DTS_WORLD, &id); dev->SetTransform(D3DTS_VIEW, &id); dev->SetTransform(D3DTS_PROJECTION, &pr);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_CLIPPING, TRUE);
    }
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}
static void resetDevice() {  // all textures are D3DPOOL_MANAGED, so they survive Reset
    if (pendW <= 0 || pendH <= 0 || (pendW == bbW && pendH == bbH)) return;
    gpp.BackBufferWidth = pendW; gpp.BackBufferHeight = pendH;
    if (SUCCEEDED(dev->Reset(&gpp))) { bbW = pendW; bbH = pendH; setupState(); }
}
static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE && w != SIZE_MINIMIZED) { pendW = LOWORD(l); pendH = HIWORD(l); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_KEYDOWN && w == VK_INSERT) { menu.toggle(); return 0; }
    if (menu.handle({m, w, l})) return 0;
    return DefWindowProcW(h, m, w, l);
}
int main(int argc, char** argv) {
    const char* capture = (argc > 2 && !strcmp(argv[1], "--capture")) ? argv[2] : nullptr;
    SetProcessDPIAware();
    CoInitialize(nullptr);
    WNDCLASSW wc = {0, wndProc, 0, 0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"gsmenu"};
    RegisterClassW(&wc);
    RECT r = {0, 0, WW, WH}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hw = CreateWindowW(L"gsmenu", L"gamesense menu", WS_OVERLAPPEDWINDOW, 100, 100, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = WW; pp.BackBufferHeight = WH; pp.hDeviceWindow = hw; pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hw, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) { fprintf(stderr, "CreateDevice failed\n"); return 1; }
    gpp = pp;
    setupState();

    uint32_t tile[16]; const char* pat = "..x.x.x.x...x.x.";  // rows y%4 = 0..3, 'x' = 0x0c else 0x14
    for (int i = 0; i < 16; i++) tile[i] = pat[i] == 'x' ? 0xff0c0c0c : 0xff141414;
    bgTile = makeTex(4, 4, tile);

    initFonts();

    tabIcons = loadPng("tab_icons.png");
    for (int d = 0; d < 5; d++)
        for (int i = 0; i < 12; i++) weaponIcons[d][i] = loadPng(("weapons/" + std::to_string(d) + "/" + std::to_string(i) + ".png").c_str());
    menu.hwnd = hw;
    buildMenu();
    if (capture) {
        if (const char* rs = getenv("GS_RESIZE")) { sscanf_s(rs, "%dx%d", &pendW, &pendH); resetDevice(); }  // exercise the resize path
        replayEvents(); render(); return saveBmp(capture) ? 0 : 1;
    }
    ShowWindow(hw, SW_SHOW);
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
        resetDevice();
        render(); dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    return 0;
}
