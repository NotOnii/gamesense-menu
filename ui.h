// ui.h - gamesense menu framework, ported from the dumped image (base 0x43310000).
// Function addresses in comments are the originals this code mirrors. Coordinates are menu-relative,
// like the original (FUN_4334eae8 hands children `cursor - menu.pos`).
// Included from main.cpp after the renderer + fonts.
#pragma once
#include <functional>
#include <cmath>

static Font* textFont = &fNormal;
static bool layoutLocked = false;  // "Lock menu layout" (DAT_43467e4b)
static std::vector<std::function<void()>> overlays;  // popups: drawn after everything (original raises the Z-buffer)

// ---- colour helpers (FUN_433e87bd / FUN_43383de6: per-channel saturating sub/add, alpha included)
static D3DCOLOR colSub(D3DCOLOR a, D3DCOLOR b) {
    D3DCOLOR r = 0;
    for (int s = 0; s < 32; s += 8) { int x = (int)((a >> s) & 255) - (int)((b >> s) & 255); r |= (D3DCOLOR)(x < 0 ? 0 : x) << s; }
    return r;
}
static D3DCOLOR colAdd(D3DCOLOR a, D3DCOLOR b) {
    D3DCOLOR r = 0;
    for (int s = 0; s < 32; s += 8) { int x = (int)((a >> s) & 255) + (int)((b >> s) & 255); r |= (D3DCOLOR)(x > 255 ? 255 : x) << s; }
    return r;
}
static D3DCOLOR menuColor() { return rgb(accent.r, accent.g, accent.b); }

// backend text 0x4334173d: flags 0x10 centre, 0x80 right-align, 0x08 shadow, 0x20 outline
static void drawText(Font& f, int x, int y, const std::wstring& s, D3DCOLOR col, unsigned flags, int maxlen = 0) {
    int lim = 0x7fffffff;
    if (flags & 0x90) {
        int w = f.width(s), h = f.height;
        if (maxlen && maxlen < w) w = maxlen;  // FUN_4338aa26: measured width capped at maxlen
        if (flags & 0x10) { x -= w / 2; y -= h / 2; } else if (flags & 0x80) x -= w;
    }
    if (maxlen) lim = x + maxlen;
    D3DCOLOR a = col & 0xff000000;
    if (flags & 0x08) { D3DCOLOR sa = (col >> 24) > 0x80 ? 0x80u : (col >> 24); f.draw(x + 1, y + 1, s, sa << 24 | 0x010101, lim == 0x7fffffff ? lim : lim + 1); }
    else if (flags & 0x20) { static const int o[4][2] = {{1, 1}, {-1, -1}, {-1, 1}, {1, -1}}; for (auto& d : o) f.draw(x + d[0], y + d[1], s, a | 0x0c0c0c, lim == 0x7fffffff ? lim : lim + d[0]); }
    f.draw(x, y, s, col, lim);
}

struct Msg { UINT msg; WPARAM w; LPARAM l; };
struct Pt { int x, y; };

struct Menu;
struct Child;

// ---- IElement (skeetsdk.h vtable order; only the slots the menu uses are virtual here)
struct Element {
    int x = 0, y = 0, w = 0, h = 0;  // +0x20..+0x2c
    bool visible = true;             // +0x15
    bool hovered = false;            // +0x16
    bool editable = true;            // +0x14
    Child* parent = nullptr;
    int lp = 20;       // +0x54 LeftPadding (layout x offset)
    bool same = false; // +0x18 SameLine
    int kind = 0;      // +0x19 UiType (9 hotkey, 12 colour picker: right-aligned when same-line)
    D3DCOLOR color = 0xc8ffffff;  // +0x50 text colour (ARGB here; the image stores ABGR)
    std::vector<std::function<void(Element*)>> calls;  // +0x40 callbacks, run by FUN_4334e09f on value set
    void fire() { for (auto& f : calls) f(this); }
    void setVisible(bool v);  // FUN_433e000b: +0x15, then the parent re-lays out (vtable +0x20)
    // label text (LabelText_43384344): colour +0x50, alpha forced to 0x46 when disabled. Default white@0xc8 drawn as the
    // opaque grey it blends to on the groupbox fill, which is what the Misc ref was matched with.
    D3DCOLOR textColor() const { D3DCOLOR c = color == 0xc8ffffff ? gray(0xcd) : color; return editable ? c : (c & 0xffffff) | 0x46000000; }
    virtual ~Element() {}
    virtual bool IsHovered(Pt p) { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    virtual bool HandleMouse(const Msg&, Pt) { return false; }
    virtual bool HandleInput(const Msg&) { return false; }
    virtual void Render(Pt origin) = 0;
    virtual void ResetInputState() {}
    virtual void HandleResize(int /*childW*/) {}  // slot 9
    virtual bool WantsFocus() const { return false; }
    virtual bool fills() const { return false; }  // fills its groupbox (weapon selector slot 11)  // types 5/6/9/10 get HandleInput first (FUN_433ba305)
    // Label_HandleDPI 0x4336eda0: size = S(default size +0x30/+0x34), LeftPadding = S(20) when set. Defaults are the
    // ctor sizes, captured on the first call (buildMenu runs it once at 100%).
    // config var behind the element (node: hash +8, size +0xc, type +0xe): raw value bytes as the config writer
    // (0x43417939) stores them - bool 1 byte, int 4, colour 4 (ABGR), hotkey 8 ({key << 2 | flags, mode})
    uint32_t cfgHash = 0; uint8_t cfgType = 0; uint16_t cfgSize = 0;
    virtual bool cfgRead(uint8_t*) const { return false; }
    virtual void cfgWrite(const uint8_t*) {}
    int w0 = -1, h0 = -1, lp0 = 0;
    // same-line hotkeys / colour pickers keep their unscaled LeftPadding (measured: their right edge sits S(20)-20 px
    // further left at 125% in a GS capture than a scaled one would put it)
    virtual void HandleDPI() {
        if (w0 < 0) { w0 = w; h0 = h; lp0 = lp; }
        w = S(w0); h = S(h0);
        lp = !lp0 ? 0 : (same && (kind == 9 || kind == 12)) ? lp0 : S(lp0);
    }
};

static Element* focused = nullptr;  // FUN_4336ead1 (element with keyboard focus)

// ---- Checkbox (vtable 0x434721e8)
struct Checkbox : Element {
    std::wstring label; bool* value; bool latched = false;
    std::vector<Element*> deps;  // +0x70 list: elements whose visibility follows the value (Checkbox_OnValueSet 0x4334c75f)
    Checkbox(const wchar_t* l, bool* v) : label(l), value(v) { w = 150; h = 13; lp = 0; kind = 3; }  // ctor 0x433adf35: 0x96 x 0xd with label
    bool get() const { return value && *value; }
    bool cfgRead(uint8_t* o) const override { if (!value) return false; o[0] = *value ? 1 : 0; return true; }
    void cfgWrite(const uint8_t* i) override { if (value) *value = i[0] != 0; }
    void applyDeps() { for (Element* d : deps) d->setVisible(get()); }
    void set(bool v) { if (value) *value = v; applyDeps(); fire(); }  // FUN_433ad3c8 -> OnValueSet
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x4333043a
        if (m.msg == WM_MOUSEMOVE) hovered = IsHovered(p);
        else if (m.msg == WM_LBUTTONDOWN) { if (IsHovered(p)) { latched = true; return true; } }
        else if (m.msg == WM_LBUTTONUP && latched) {
            latched = false;
            if (IsHovered(p) && value) set(!*value);  // FUN_433ad3c8(!value)
            return true;
        }
        return false;
    }
    void ResetInputState() override { latched = false; }
    static void drawBox(int bx, int by, bool on, bool hov) {  // S(8) square (Checkbox_Render 0x4338902f / FUN_433d24ec)
        int n = S(8), in = n - 2;
        outline(bx, by, n, n, gray(0x0c));
        if (on) {
            D3DCOLOR top = menuColor(), bot = colSub(top, 0x282828);
            if (hov) { top = colAdd(top, 0x0a0a0a); bot = colAdd(bot, 0x0a0a0a); }
            gradV(bx + 1, by + 1, in, in, top, bot);
        } else if (in == 6) {
            // shortcut: unchecked fill rows measured from ref (code passes a colour I haven't resolved yet); hover not applied
            static const int r[6] = {77, 72, 67, 63, 57, 52};
            for (int i = 0; i < 6; i++) rect(bx + 1, by + 1 + i, 6, 1, gray(r[i] + (hov ? 10 : 0)));
        } else gradV(bx + 1, by + 1, in, in, gray(77 + (hov ? 10 : 0)), gray(52 + (hov ? 10 : 0)));  // same ramp at other DPIs
    }
    void Render(Pt o) override {  // 0x4338902f
        drawBox(o.x + x, o.y + y + 3, value && *value, hovered);
        textFont->draw(o.x + x + S(20), o.y + y, label, textColor());  // TextPadding +0x68 = S(0x14)
    }
};

// ---- Slider (vtable 0x4346fb78)
struct Slider : Element {
    // Width from Slider_HandleResize 0x433f0a8f: groupbox width - 100, min 40, capped at MaxLen 200. x = bar border.
    std::wstring label; int* value; int mn, mx; bool dragging = false;
    unsigned unit = 0;   // +0xa0: up to two UTF-16 chars appended to the value ('%', 0xb0, "ms", ...)
    float step = 1.f;    // +0x98: display scale
    std::vector<std::pair<int, std::wstring>> tips;  // +0x70 map value -> text (FUN_43402af1)
    int hpAbove = 0;     // Minimum damage sliders (FUN_43480054): "HP+n" above 100
    int barY = 16, bh = 6;  // label row (LabelText advances +0x58 = 14) + 2; no label -> bar at +2, height 10 (ctor: 10 / 0x19)
    Slider(const wchar_t* l, int lo, int hi, int* v, unsigned u = 0, float st = 1.f) : label(l), value(v), mn(lo), mx(hi), unit(u), step(st) {
        w = 160; h = label.empty() ? 10 : 25; barY = label.empty() ? 2 : 16;
    }
    std::wstring text() const {  // FUN_4334a31c
        int v = get();
        for (auto& t : tips) if (t.first == v) return t.second;
        std::wstring s;
        if (hpAbove && v > hpAbove) s = L"HP+" + std::to_wstring(v - hpAbove);
        else if (step != 1.f && step <= 1.f) {
            wchar_t b[64]; int n = swprintf_s(b, step == 0.001f ? L"%.3f" : L"%.2f", (double)((float)v * step));  // shortcut: %.3f constant DAT_43470000 assumed 0.001
            while (n > 1 && b[n - 2] != L'.' && b[n - 1] == L'0') b[--n] = 0;
            s = b;
        } else s = std::to_wstring(step != 1.f ? (int)((float)v * step) : v);
        if (unit & 0xffff) s += (wchar_t)(unit & 0xffff);
        if (unit >> 16) s += (wchar_t)(unit >> 16);
        return s;
    }
    void HandleResize(int childW) override { w = childW - S(100); if (w < S(40)) w = S(40); if (w > S(200)) w = S(200); }
    void HandleDPI() override { Element::HandleDPI(); bh = S(6); barY = label.empty() ? 2 : S(14) + 2; }
    int bx0() const { return x; }
    int get() const { return value ? *value : 0; }
    bool cfgRead(uint8_t* o) const override { if (!value) return false; memcpy(o, value, 4); return true; }
    void cfgWrite(const uint8_t* i) override { if (value) { int v; memcpy(&v, i, 4); *value = v < mn ? mn : v > mx ? mx : v; } }  // Slider_OnConfigLoad clamps
    void set(int v) {  // FUN_43398ecf + OnValueSet 0x433a1670 (clamp)
        if (v < mn) v = mn; if (v > mx) v = mx;
        if (value && *value != v) { *value = v; fire(); }
    }
    int fromMouse(Pt p) const {  // FUN_43365af6: truncating float maths
        int v = (int)(((float)(mx - mn) / (float)w) * (float)(p.x - x) + (float)mn);
        return v < mn ? mn : v > mx ? mx : v;
    }
    int fillWidth() const {  // Slider_Render 0x43383a3c: (1 / ((max-min)/w)) * (v-min), truncated, not clamped
        return (int)((1.f / ((float)(mx - mn) / (float)w)) * (float)(get() - mn));
    }
    bool IsHovered(Pt p) override {  // shortcut: hit box around the bar; Slider_IsHovered 0x433ba3f2 not ported yet
        return p.x >= bx0() - 8 && p.x <= bx0() + w + 8 && p.y >= y + barY - 2 && p.y <= y + barY + 9;
    }
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x433e5250
        if (m.msg == WM_MOUSEMOVE) { hovered = IsHovered(p); if (!dragging) return false; set(fromMouse(p)); return true; }
        if (m.msg == WM_LBUTTONUP) { dragging = false; return false; }
        if (m.msg == WM_MOUSEWHEEL) {  // only with Ctrl held (MK_CONTROL bit of the key state)
            if (IsHovered(p) && (GET_KEYSTATE_WPARAM(m.w) & MK_CONTROL)) { set(get() + (GET_WHEEL_DELTA_WPARAM(m.w) < 1 ? -1 : 1)); return true; }
            return false;
        }
        if (m.msg != WM_LBUTTONDOWN || !IsHovered(p)) return false;
        focused = this;
        if (p.x < x) set(get() - 1);
        else if (p.x > x + w) set(get() + 1);
        else { dragging = true; set(fromMouse(p)); }
        return true;
    }
    bool HandleInput(const Msg& m) override {  // 0x4336bc61: arrows step 1, 5 with Shift
        if (m.msg != WM_KEYDOWN || !value) return false;
        int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 5 : 1;
        if (m.w == VK_LEFT) { set(get() - step); return true; }
        if (m.w == VK_RIGHT) { set(get() + step); return true; }
        return false;
    }
    bool WantsFocus() const override { return true; }
    void ResetInputState() override { dragging = false; }
    void Render(Pt o) override {  // 0x43383a3c
        int bx = o.x + bx0(), by = o.y + y + barY, v = get();
        if (!label.empty()) textFont->draw(bx, o.y + y, label, textColor());
        if (w < abs(mx - mn) && hovered) {  // -/+ markers only when the range is finer than the bar
            if (v != mn) rect(bx - 5, by + 5 - 2, 3, 1, 0xff646464);
            if (v != mx) { rect(bx + w + 4 - 2, by + 3, 3, 1, 0xff646464); rect(bx + w + 3, by + 2, 1, 3, 0xff646464); }
        }
        outline(bx, by, w, bh + 1, gray(0x0c));  // RectOut 0xff0c0c0c
        D3DCOLOR t1 = 0xff141414, t2 = 0xff282828;
        if (editable) { t1 = hovered ? 0xff3c3c3c : 0xff323232; t2 = hovered ? 0xff505050 : 0xff464646; }
        gradV(bx + 1, by + 1, w - 2, bh - 1, t1, t2);
        int fill = fillWidth();
        D3DCOLOR a = menuColor(); if (!editable) a = (a & 0xffffff) | (((a >> 24) / 3) << 24);
        gradV(bx + 1, by + 1, fill, bh - 1, a, colSub(a, 0x3c3c3c));  // not clamped: at max it runs over the border
        // Slider_DrawValue 0x43416fbd -> backend text 0x4334173d, flags 0x31: font slot 1 (Verdana 9 bold), centred (0x10),
        // outline (0x20) = 4 copies of 0x0c0c0c at the diagonals (table 0x4346f220) at the text alpha, then the text.
        std::wstring s = text();
        D3DCOLOR col = (editable ? 0xbbu : 0x50u) << 24 | 0xffffff;
        drawText(fValue, bx + fill, by + bh, s, col, 0x31);
    }
};


// ---- draw-only controls (classes decompiled, behaviour not ported yet). Heights = ctor Size.y (layout pitch = h + 5).
struct Label : Element {
    std::wstring text; Label(const wchar_t* t) : text(t) { h = 12; kind = 8; }
    void Render(Pt o) override { textFont->draw(o.x + x, o.y + y, text, textColor()); }
};
struct Button : Element {  // vtable 0x4346f134
    std::wstring text; bool pressed = false; void (*onClick)() = nullptr;  // +0x60 pressed
    Button(const wchar_t* t, void (*cb)() = nullptr) : text(t), onClick(cb) { w = 160; h = 28; kind = 2; }
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x433ada41
        if (m.msg == WM_MOUSEMOVE) { hovered = IsHovered(p); if (!hovered) pressed = false; return false; }
        if (m.msg == WM_LBUTTONDOWN) { if (!IsHovered(p)) return false; pressed = true; return true; }
        if (m.msg == WM_LBUTTONUP && pressed) { pressed = false; if (IsHovered(p) && onClick) onClick(); return true; }
        return false;
    }
    void ResetInputState() override { pressed = false; }
    void HandleResize(int cw) override { w = cw - S(100); if (w < S(40)) w = S(40); if (w > S(200)) w = S(200); }  // 0x433d65f5
    void Render(Pt o) override {  // 0x43359006
        int bx = o.x + x, by = o.y + y, bh = h - 3;
        outline(bx, by, w, bh, gray(0x0c));
        outline(bx + 1, by + 1, w - 2, bh - 2, gray(0x32));
        D3DCOLOR t, b;
        if (pressed) { t = 0xff1e1e1e; b = 0xff141414; }
        else if (hovered) { t = 0xff272727; b = 0xff232323; }
        else { t = 0xff222222; b = 0xff1e1e1e; }
        gradV(bx + 2, by + 2, w - 4, bh - 4, t, b);
        // text: backend flags 0x18 (centred + shadow 0x010101 @min(a,0x80)), white @200 (0xff3c3c3c disabled)
        drawText(fNormal, bx + w / 2, by + bh / 2, text, editable ? 0xc8ffffff : 0xff3c3c3c, 0x18, w - 10);  // shortcut: maxlen w-10 (exact value hidden by the decompiler)  // centre + drop shadow
    }
};
// combo popup scrollbar when the list holds more rows than it shows (GS capture: 7px 0x282828 track inside the popup's
// right edge, 5px 0x414141 thumb inset by 1; thumb = list height * visible / total, travel proportional to the scroll)
static void popupScrollbar(int x, int y, int w, int listH, int contentH, int scroll) {
    if (contentH <= listH) return;
    int tx = x + w - 8, ty = y + 1, th = listH - 2;
    rect(tx, ty, 7, th, 0xff282828);
    int len = listH * listH / contentH, travel = th - 2 - len, maxScroll = contentH - listH;
    int off = maxScroll > 0 ? travel * scroll / maxScroll : 0;
    rect(tx + 1, ty + 1 + off, 5, len, 0xff414141);
}
struct Combo : Element {  // vtable 0x4346dd00
    std::wstring label; std::vector<std::wstring> opts; int* value;
    std::vector<int> vals;  // item values (FUN_433f8d73 / FUN_4337cb10 take (text, value)); index when empty
    bool open = false, pressed = false; int hoverItem = -1, scroll = 0;  // +0x70, +0x71, +0x74, +0x8c
    bool multi = false;  // Multiselect (vtable 0x434724b8): value is a bitmask, clicks toggle and keep the list open
    int valOf(int i) const { return i < (int)vals.size() ? vals[i] : multi ? 1 << i : i; }
    bool cfgRead(uint8_t* o) const override { if (!value) return false; memcpy(o, value, 4); return true; }
    void cfgWrite(const uint8_t* i) override { if (value) memcpy(value, i, 4); }
    bool isSel(int i) const { return multi ? value && (*value & valOf(i)) : value && *value == valOf(i); }
    std::wstring shownText() const {
        if (!multi) { for (int i = 0; i < (int)opts.size(); i++) if (isSel(i)) return opts[i]; return L""; }
        std::wstring r; for (int i = 0; i < (int)opts.size(); i++) if (isSel(i)) r += (r.empty() ? L"" : L", ") + opts[i];
        return r.empty() ? L"-" : r;
    }
    int itemH = 20;   // +0x64 = S(20) (Combobox_HandleDPI 0x43367728 / Multi_HandleDPI 0x433f0b84)
    int labelH = 15;  // +0x6c = S(15); no label -> box at the top, height 0x14 (Combobox_ctor / Multi_ctor)
    void HandleDPI() override { Element::HandleDPI(); itemH = S(20); labelH = label.empty() ? 0 : S(15); }
    Combo(const wchar_t* l, std::vector<std::wstring> o, int* v, std::vector<int> vv = {}, bool mul = false)
        : label(l), opts(std::move(o)), value(v), vals(std::move(vv)), multi(mul) { w = 160; h = label.empty() ? 20 : 35; labelH = label.empty() ? 0 : 15; kind = 4; }
    int sel() const { for (int i = 0; i < (int)opts.size(); i++) if (isSel(i)) return i; return -1; }
    int shown() const { return (int)opts.size() < 8 ? (int)opts.size() : 8; }
    int boxY() const { return y + labelH; }
    bool IsHovered(Pt p) override {  // 0x43388257: box (+ open list)
        int hh = h - labelH + (open ? 1 + shown() * itemH : 0);
        return p.x >= x && p.x <= x + w && p.y >= boxY() && p.y <= boxY() + hh;
    }
    int popupPriority() const { return open ? 2 : 0; }  // slot 12
    void trackItem(const Msg& m, Pt p) {  // slot 13 0x43325388
        int ry = p.y - (h - labelH) - boxY();
        if (ry < 0) hoverItem = -1;
        else {
            int i = (scroll - scroll % itemH + ry) / itemH;
            if (i < (int)opts.size()) {
                hoverItem = ry / itemH;
                if (multi) { if (m.msg == WM_LBUTTONUP && value) { *value = isSel(i) ? *value & ~valOf(i) : *value | valOf(i); fire(); } }
                else if (pressed && value && *value != valOf(i)) { *value = valOf(i); fire(); }
            }
        }
        if (m.msg == WM_LBUTTONUP) { if (!multi || ry < 0) open = false; pressed = false; }
    }
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x4340c49a
        if (!editable) return false;
        if (m.msg == WM_MOUSEMOVE) {
            if (!IsHovered(p)) { hoverItem = -1; hovered = false; pressed = false; return false; }
            hovered = true; if (open) trackItem(m, p); return true;
        }
        if (m.msg == WM_LBUTTONDOWN) { if (!IsHovered(p)) { open = false; return false; } pressed = true; return true; }
        if (m.msg == WM_LBUTTONUP) {
            if (!pressed) return false;
            if (!IsHovered(p)) { pressed = false; return false; }
            if (!open) { open = true; pressed = false; return true; }
            trackItem(m, p); return true;
        }
        if (m.msg == WM_RBUTTONDOWN || m.msg == WM_RBUTTONUP) return IsHovered(p);
        if (m.msg == WM_MOUSEWHEEL && open && (int)opts.size() > 8 && IsHovered(p)) {
            scroll += GET_WHEEL_DELTA_WPARAM(m.w) > 0 ? -itemH * 4 : itemH * 4;
            int mx = ((int)opts.size() - 8) * itemH; if (scroll < 0) scroll = 0; if (scroll > mx) scroll = mx;
            return true;
        }
        return false;
    }
    void ResetInputState() override { open = false; pressed = false; hoverItem = -1; }
    void HandleResize(int cw) override { w = cw - S(100); if (w < S(40)) w = S(40); if (w > S(200)) w = S(200); }  // 0x4332bb67
    void drawItemText(int tx, int ty, int i, D3DCOLOR c) {  // slot 14 0x433b1508: vertically centred by font height
        if (i < 0 || i >= (int)opts.size()) return;
        textFont->draw(tx, ty + (itemH - textFont->height) / 2, opts[i], c);
    }
    void Render(Pt o) override {  // 0x433b8d0f
        int bx = o.x + x, ly = o.y + y, by = o.y + y + (label.empty() ? 0 : labelH - 1);  // shortcut: drawn box starts one row above +0x6c (fitted at 100%)
        if (!label.empty()) textFont->draw(bx, ly, label, textColor());  // label helper 0x43384344
        D3DCOLOR t, b;  // box, slot 15 0x43327d7a
        if (hovered || open) { t = 0xff292929; b = 0xff2e2e2e; }
        else if (editable) { t = 0xff1f1f1f; b = 0xff242424; }
        else { t = 0x501f1f1f; b = 0x50242424; }
        gradV(bx + 1, by + 1, w - 2, itemH - 2, t, b);
        outline(bx, by, w, itemH, gray(0x0c));
        D3DCOLOR vc = (editable ? 0x88u : 0x32u) << 24 | 0xffffff;
        textFont->draw(bx + S(7), by + (itemH - textFont->height) / 2, shownText(), vc, bx + w - S(20));  // Combobox_Render: S(7) inset
        int ax = bx + w - S(12), ay = by + (itemH - 2 - 1) / 2;  // arrow 0x433c9f33 (black row above)
        rect(ax, ay - 1, 5, 1, 0xff000000); rect(ax, ay, 5, 1, vc); rect(ax + 1, ay + 1, 3, 1, vc); rect(ax + 2, ay + 2, 1, 1, vc);
        if (open && editable) {
            int lx = bx, lyy = by + itemH + 1, n = shown();
            overlays.push_back([=]() {
                rect(lx + 1, lyy + 1, w - 2, itemH * n - 2, 0xff232323); outline(lx, lyy, w, itemH * n, 0xff0c0c0c);  // RectOutlined
                int first = scroll / itemH;
                for (int r = 0; r < n && first + r < (int)opts.size(); r++) {
                    int i = first + r;
                    if (r == hoverItem) rect(lx + 1, lyy + itemH * r + 1, w - 2, itemH - 2, 0xff191919);
                    // selected = menu colour, others white @200 (as the hotkey popup 0x43330648; combo font not confirmed)
                    if (isSel(i)) fBold.draw(lx + S(7), lyy + itemH * r + (itemH - fBold.height) / 2, opts[i], menuColor());  // selected: bold, menu colour
                    else drawItemText(lx + S(7), lyy + itemH * r, i, 0xc8ffffff);
                }
                popupScrollbar(lx, lyy, w, itemH * n, (int)opts.size() * itemH, scroll);
            });
        }
    }
};
// key names: table 0x43472de0 (packed big-endian chars), else GetKeyNameTextW (first 3 chars, upper-cased) - FUN_4341859f
static const char* KEYNAMES[256] = {nullptr, "M1", "M2", "CAN", "M3", "M4", "M5", nullptr, "BCK", "TAB", nullptr, nullptr, "CLR", "ENT", nullptr, nullptr,
    "SHF", "CTR", "ALT", "PAU", "CAP", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "ESC", nullptr, nullptr, nullptr, nullptr,
    "SPC", "PGU", "PGD", "END", "HOM", "LEF", "UP", "RIG", "DOW", "SEL", "PRI", "EXE", "PS", "INS", "DEL", "HLP"};
static std::wstring keyName(unsigned vk, unsigned lparam) {
    if (vk == 0 || vk == VK_ESCAPE) return L"-";
    if (vk < 0x30 && KEYNAMES[vk]) { std::string s = KEYNAMES[vk]; return std::wstring(s.begin(), s.end()); }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"KP" + std::to_wstring(vk - VK_NUMPAD0);
    static const wchar_t* kp[] = {L"KP*", L"KP+", L"KPS", L"KP-", L"KP.", L"KP/"};
    if (vk >= VK_MULTIPLY && vk <= VK_DIVIDE) return kp[vk - VK_MULTIPLY];
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    static const std::pair<unsigned, const wchar_t*> more[] = {{0x5b, L"WIN"}, {0x5c, L"WIN"}, {0x90, L"NUM"}, {0x91, L"SCR"}, {0xa0, L"SHF"}, {0xa1, L"SHF"},
        {0xa2, L"CTR"}, {0xa3, L"CTR"}, {0xa4, L"ALT"}, {0xa5, L"ALT"}, {0xba, L";:"}, {0xbb, L"+"}, {0xbc, L","}, {0xbd, L"-"}, {0xbe, L"."}, {0xbf, L"/?"},
        {0xc0, L"`~"}, {0xdb, L"[{"}, {0xdc, L"\\|"}, {0xdd, L"]}"}, {0xde, L"'\""}, {0xfa, L"PLA"}, {0xfb, L"ZOO"}};
    for (auto& [k, n] : more) if (k == vk) return n;
    unsigned sc = lparam;
    if (!sc) { sc = (MapVirtualKeyExW(vk, 0, nullptr) & 0xff) << 16; if (vk > 0x20 && (vk < 0x29 || (vk > 0x2c && (vk < 0x2f || vk == 0x6f || vk == 0x90)))) sc |= 0x1000000; }
    wchar_t buf[256] = {};
    if (!(sc & 0xff0000) || !GetKeyNameTextW((sc & 0xfbffffff) | 0x2000000, buf, 255)) swprintf_s(buf, L"%u", vk);
    std::wstring r;
    for (int i = 0; i < 3 && buf[i]; i++) r += iswalpha(buf[i]) ? (wchar_t)towupper(buf[i]) : buf[i];
    return r;
}
static const wchar_t* HOTKEY_MODES[4] = {L"Always on", L"On hotkey", L"Toggle", L"Off hotkey"};  // PTR 0x4346d49c
struct Hotkey : Element {  // vtable 0x4346f928; same-line, right-aligned by the layout. Popup = vtable 0x4346fab0
    unsigned vk = 0; int mode = 1; std::wstring name;
    bool waiting = false, latched = false;                       // +0x60, +0x61
    bool popOpen = false, popLatched = false; int popHover = -1;  // popup +0x64, +0x65, +0x68
    int popW = 100, rowH = 20;
    void HandleDPI() override { Element::HandleDPI(); popW = S(100); rowH = S(20); }
    Hotkey(unsigned key = 0, int m = 1) : vk(key), mode(m) { same = true; kind = 9; w = 0x11; h = 0xb; name = keyName(vk, 0); }
    Pt popPos() const { return {x + w - popW, y + 12}; }  // shortcut: popup anchor (ctor adds LeftPadding; HandleDPI x = pad - w) approximated
    bool popHovered(Pt p) const { Pt q = popPos(); return popOpen && p.x >= q.x && p.x <= q.x + popW && p.y >= q.y && p.y <= q.y + rowH * 4; }
    bool IsHovered(Pt p) override {  // 0x433ad751: popup, else [x - w, x + 25] x [y, y + h]
        return popHovered(p) || (p.x >= x - w && p.x <= x + 0x19 && p.y >= y && p.y <= y + h);
    }
    bool rectHovered(Pt p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }  // Button_IsHovered
    int popupPriority() const { return popOpen ? 2 : 0; }  // slot 12
    void setKey(unsigned k, unsigned lp) { vk = (k == VK_ESCAPE) ? 0 : k; name = keyName(k, lp); }  // FUN_4341859f
    // value bytes (FUN_43486649): dword0 = key << 2 | state bit, dword1 = mode
    bool cfgRead(uint8_t* o) const override { uint32_t d[2] = {vk << 2, (uint32_t)mode}; memcpy(o, d, 8); return true; }
    void cfgWrite(const uint8_t* i) override { uint32_t d[2]; memcpy(d, i, 8); setKey(d[0] >> 2, 0); mode = (int)(d[1] & 3); }
    bool popMouse(const Msg& m, Pt p) {  // 0x433afc02
        if (!popOpen) return false;
        Pt q = popPos();
        if (m.msg == WM_MOUSEMOVE) { if (popHovered(p)) { popHover = (p.y - q.y) / rowH; return true; } popHover = -1; }
        else if (m.msg == WM_LBUTTONDOWN) { if (popHovered(p)) { popLatched = true; return true; } }
        else if (m.msg == WM_LBUTTONUP && popLatched) {
            if (popHovered(p)) { int i = (p.y - q.y) / rowH; if (i >= 0 && i < 4) mode = i; popOpen = false; popLatched = false; return true; }  // HK_modeCB 0x4331ffe8
            popLatched = false;
        }
        return false;
    }
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x4332604c
        if (popMouse(m, p)) return true;
        if (popOpen) return false;
        switch (m.msg) {
        case WM_MOUSEMOVE: hovered = IsHovered(p); return false;
        case WM_LBUTTONDOWN: if (!IsHovered(p)) return false; latched = true; return true;
        case WM_LBUTTONUP: { if (!latched) return false; bool hv = IsHovered(p); latched = false; if (!hv) return false; waiting = true; focused = this; return true; }
        case WM_RBUTTONDOWN: if (!rectHovered(p)) return false; latched = true; return true;
        case WM_RBUTTONUP: if (!latched) return false; latched = false; if (!rectHovered(p)) return false; popOpen = true; popHover = -1; return true;
        }
        return false;
    }
    bool HandleInput(const Msg& m) override {  // 0x43328f7c: while waiting, the next key or mouse button binds
        if (!waiting) return false;
        unsigned k;
        if (m.msg == WM_KEYDOWN) k = (unsigned)m.w;
        else if (m.msg == WM_LBUTTONDOWN) k = VK_LBUTTON;
        else if (m.msg == WM_RBUTTONDOWN) k = VK_RBUTTON;
        else if (m.msg == WM_MBUTTONDOWN) k = VK_MBUTTON;
        else if (m.msg == WM_XBUTTONDOWN) k = GET_XBUTTON_WPARAM(m.w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        else return false;
        setKey(k, m.msg == WM_KEYDOWN ? (unsigned)m.l : 0);
        waiting = false;
        return true;
    }
    bool WantsFocus() const override { return waiting; }
    void ResetInputState() override { hovered = false; popOpen = false; popLatched = false; popHover = -1; }
    void Render(Pt o) override {  // 0x43330cc3: "[%s]" right-aligned at x + w, flags 0x84 (right + Small Fonts slot 4)
        D3DCOLOR c = waiting ? 0xffff0000 : hovered ? 0x82ffffff : 0x64ffffff;
        std::wstring s = L"[" + name + L"]";
        // shortcut: glyph-cell offsets fitted per hotkey font - bitmap Small Fonts (100%) +1/+2 vs the ref, scaled ClearType
        // Verdana -1/0 vs a GS 125% capture (backend right-align 0x84 not ported)
        bool bitmapFont = dpi == 1.f;
        fSmall.draw(o.x + x + w - fSmall.width(s) + (bitmapFont ? 1 : -1), o.y + y + (bitmapFont ? 2 : 0), s, c);
        if (popOpen) {
            Pt q = popPos(); int px = o.x + q.x, py = o.y + q.y; int cur = mode, hv = popHover;
            overlays.push_back([=]() {  // 0x43330648
                rect(px + 1, py + 1, popW - 2, rowH * 4 - 2, 0xff232323); outline(px, py, popW, rowH * 4, 0xff0c0c0c);
                for (int i = 0; i < 4; i++) {
                    if (i == hv) rect(px + 1, py + rowH * i + 1, popW - 2, rowH - 2, 0xff191919);
                    Font& f = i == cur ? fBold : *textFont;
                    f.draw(px + S(10), py + rowH * i + (rowH - f.height) / 2, HOTKEY_MODES[i], i == cur ? menuColor() : 0xc8ffffff);
                }
            });
        }
    }
};
struct ColorPicker : Element {  // vtable 0x43470274
    D3DCOLOR* value; bool menuColour;                  // value as ARGB
    bool latched = false, open = false, ctxOpen = false;  // +0xa0, +0xa1, popup +0x108
    int region = 0, ctxHover = -1; bool ctxLatched = false;  // +0x128 (FUN_43347c72: 1 swatch, 2 SV, 3 hue, 4 alpha)
    float hue = 0, sat = 0, val = 0;                   // +0x12c, +0x130, +0x134
    // geometry relative to the element (ctor constants 0x4346f070 / 0x4347278c / 0x4346f824 / 0x4346f39c)
    int PX = 0, PY = 11, PW = 180, PH = 175, SVX = 5, SVY = 16, SVS = 150, HX = 160, HY = 16, HW = 15, HH = 150, AX = 5, AY = 171, AW = 150, AH = 10;
    void HandleDPI() override {
        Element::HandleDPI();
        SVS = S(150); HW = S(15); HH = S(150); AW = S(150); AH = S(10);
        SVX = 5; SVY = h + 5; HX = SVS + 10; HY = h + 5; AX = 5; AY = h + SVS + 10;
        PY = h; PW = HX + HW + 5; PH = AY + AH + 5 - PY;
    }
    ColorPicker(D3DCOLOR* v, bool mc = false) : value(v), menuColour(mc) { same = true; kind = 12; w = 0x11; h = 0xb; fromColour(); }
    void fromColour() {  // ARGB -> HSV
        float r = ((*value >> 16) & 255) / 255.f, g = ((*value >> 8) & 255) / 255.f, b = (*value & 255) / 255.f;
        float mx = (std::max)({r, g, b}), mn = (std::min)({r, g, b}), d = mx - mn;
        val = mx; sat = mx > 0 ? d / mx : 0;
        if (d <= 0) return;
        float hh = mx == r ? (g - b) / d : mx == g ? 2 + (b - r) / d : 4 + (r - g) / d;
        hue = hh * 60.f; if (hue < 0) hue += 360.f;
    }
    static D3DCOLOR hsv(float h, float s, float v, unsigned a) {  // FUN_43386fb2
        float c = v * s, hp = fmodf(h, 360.f) / 60.f, x = c * (1 - fabsf(fmodf(hp, 2.f) - 1)), m = v - c, r = 0, g = 0, b = 0;
        if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; } else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
        auto q = [&](float f) { return (unsigned)((f + m) * 255.f + 0.5f); };
        return a << 24 | q(r) << 16 | q(g) << 8 | q(b);
    }
    // stored ABGR (byte order R,G,B,A) in the config, ARGB here
    bool cfgRead(uint8_t* o) const override { D3DCOLOR c = *value; o[0] = (c >> 16) & 255; o[1] = (c >> 8) & 255; o[2] = c & 255; o[3] = c >> 24; return true; }
    void cfgWrite(const uint8_t* i) override { *value = (D3DCOLOR)i[3] << 24 | i[0] << 16 | i[1] << 8 | i[2]; fromColour(); apply(); }
    void apply() { if (menuColour) { accent.r = (*value >> 16) & 255; accent.g = (*value >> 8) & 255; accent.b = *value & 255; } }
    bool swatchHit(Pt p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    bool panelHit(Pt p) const { return open && p.x >= x + PX && p.x <= x + PX + PW && p.y >= y + PY && p.y <= y + PY + PH; }
    bool ctxHit(Pt p) const { return ctxOpen && p.x >= x + w - S(100) && p.x <= x + w && p.y >= y + 12 && p.y <= y + 12 + S(40); }
    bool IsHovered(Pt p) override { return swatchHit(p) || panelHit(p) || ctxHit(p); }  // 0x433e08b0
    int popupPriority() const { return (open || ctxOpen) ? 2 : 0; }
    int regionAt(Pt p) const {  // FUN_43347c72
        if (swatchHit(p)) return 1;
        int lx = p.x - x, ly = p.y - y;
        if (lx >= SVX && lx <= SVX + SVS && ly >= SVY && ly <= SVY + SVS) return 2;
        if (lx >= HX && lx <= HX + HW && ly >= HY && ly <= HY + HH) return 3;
        if (lx >= AX && lx <= AX + AW && ly >= AY && ly <= AY + AH) return 4;
        return 0;
    }
    void drag(Pt p, int r) {  // FUN_433647a9
        unsigned al = *value >> 24;
        auto cl = [](float f) { return f < 0 ? 0.f : f > 1 ? 1.f : f; };
        if (r == 2) { sat = cl((float)(p.x - SVX - x) / SVS); val = 1.f - cl((float)(p.y - SVY - y) / SVS); }
        else if (r == 3) hue = 360.f - cl((float)(p.y - HY - y) / HH) * 360.f;
        else if (r == 4) { int v = (int)((float)(p.x - AX - x) / AW * 255.f + 0.5f); al = v < 0 ? 0 : v >= 0xfb ? 0xff : v; }
        *value = hsv(hue, sat, val, al);
        apply();
    }
    bool ctxMouse(const Msg& m, Pt p) {  // Copy / Paste popup (FUN_4339d578 with callback 0x43417f64)
        if (!ctxOpen) return false;
        int top = y + 12;
        if (m.msg == WM_MOUSEMOVE) { if (ctxHit(p)) { ctxHover = (p.y - top) / S(20); return true; } ctxHover = -1; }
        else if (m.msg == WM_LBUTTONDOWN && ctxHit(p)) { ctxLatched = true; return true; }
        else if (m.msg == WM_LBUTTONUP && ctxLatched) {
            ctxLatched = false;
            if (!ctxHit(p)) return false;
            int i = (p.y - top) / S(20); ctxOpen = false;
            // shortcut: clipboard text format of FUN_433f0c77/FUN_43364ae2 not read; uses hex RRGGBBAA
            if (i == 0 && OpenClipboard(nullptr)) {
                char t[16]; sprintf_s(t, "%02X%02X%02X%02X", (*value >> 16) & 255, (*value >> 8) & 255, *value & 255, *value >> 24);
                EmptyClipboard(); HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, 16); memcpy(GlobalLock(hg), t, 9); GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg); CloseClipboard();
            } else if (i == 1 && OpenClipboard(nullptr)) {
                if (HANDLE hd = GetClipboardData(CF_TEXT)) { unsigned r, g, b2, al; if (sscanf_s((const char*)GlobalLock(hd), "%2x%2x%2x%2x", &r, &g, &b2, &al) == 4) { *value = al << 24 | r << 16 | g << 8 | b2; fromColour(); apply(); } GlobalUnlock(hd); }
                CloseClipboard();
            }
            return true;
        }
        return false;
    }
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x433c88cf
        if (ctxMouse(m, p)) return true;
        if (ctxOpen) return false;
        switch (m.msg) {
        case WM_MOUSEMOVE:
            if ((IsHovered(p) || latched) && open) { if (latched && region > 1) drag(p, region); return true; }
            return false;
        case WM_LBUTTONDOWN:
            if (!IsHovered(p)) { open = false; return false; }
            latched = true; region = regionAt(p);
            if (open && region >= 2) drag(p, region);
            return true;
        case WM_LBUTTONUP:
            if (!latched) return false;
            latched = false;
            if (!IsHovered(p)) return false;
            if (region == 1) { open = !open; if (open) fromColour(); else region = 0; }
            return true;
        case WM_RBUTTONDOWN: if (swatchHit(p)) { latched = true; return true; } return false;
        case WM_RBUTTONUP: { if (!latched) return false; bool hv = swatchHit(p); latched = false; if (hv) { ctxOpen = true; open = false; ctxHover = -1; return true; } return false; }
        }
        return false;
    }
    void ResetInputState() override { open = false; ctxOpen = false; latched = false; ctxLatched = false; }
    void Render(Pt o) override {  // 0x4333c000
        D3DCOLOR c = *value;
        int sx = o.x + x + 1, sy = o.y + y + 1;  // shortcut: +1 like the hotkey text (right-aligned same-line items); fitted to ref
        outline(sx, sy, w, h - 2, gray(0x0c));
        gradV(sx + 1, sy + 1, w - 2, h - 4, colAdd(c, 0x141414) | 0xff000000, colSub(c, 0x141414) | 0xff000000);
        int bx = o.x + x, by = o.y + y;
        if (open) {
            float hh = hue, ss = sat, vv = val; unsigned al = c >> 24;
            overlays.push_back([=]() {
                int px = bx + PX, py = by + PY;
                // shortcut: panel frame colours hidden by the decompiler; drawn in the menu's frame style
                outline(px, py, PW, PH, 0xff0c0c0c); outline(px + 1, py + 1, PW - 2, PH - 2, 0xff282828); rect(px + 2, py + 2, PW - 4, PH - 4, 0xff171717);  // panel colours sampled from a real capture
                int qx = bx + SVX, qy = by + SVY;  // saturation/value square: white -> hue, then black overlay
                outline(qx - 1, qy - 1, SVS + 2, SVS + 2, 0xff0c0c0c);
                gradH(qx, qy, SVS, SVS, 0xffffffff, hsv(hh, 1, 1, 255));
                gradV(qx, qy, SVS, SVS, 0x00000000, 0xff000000);
                int cx = qx + (int)(ss * SVS), cy = qy + (int)((1 - vv) * SVS);
                rect(cx - 1, cy - 1, 2, 2, 0x78ffffff); outline(cx - 2, cy - 2, 4, 4, 0xff0c0c0c);
                int hx = bx + HX, hy = by + HY;  // hue bar: 6 segments, muted stops (0xdc / 0x1e)
                static const D3DCOLOR stops[7] = {0xffdc1e1e, 0xffdc1edc, 0xff1e1edc, 0xff1edcdc, 0xff1edc1e, 0xffdcbe1e, 0xffdc1e1e};  // shortcut: first stop inferred (red)
                outline(hx - 1, hy - 1, HW + 2, HH + 2, 0xff0c0c0c);
                for (int i = 0; i < 6; i++) gradV(hx, hy + HH * i / 6, HW, HH * (i + 1) / 6 - HH * i / 6, stops[i], stops[i + 1]);
                int hc = hy + (int)((360.f - hh) / 360.f * HH);
                rect(hx - 1, hc - 1, HW + 2, 2, 0x78ffffff); outline(hx - 2, hc - 2, HW + 4, 4, 0x96000000);
                int ax = bx + AX, ay = by + AY;  // alpha bar
                outline(ax - 1, ay - 1, AW + 2, AH + 2, 0xff0c0c0c);
                rect(ax, ay, AW, AH, 0xff282828); rect(ax, ay, AW, AH, hsv(hh, ss, vv, al));  // final colour incl. its alpha (not a gradient)
                int ac = ax + (int)(al / 255.f * AW);
                rect(ac - 1, ay - 1, 2, AH + 2, 0x78ffffff); outline(ac - 2, ay - 2, 4, AH + 4, 0x96000000);
            });
        }
        if (ctxOpen) {
            int px = bx + w - S(100), py = by + 12, hv = ctxHover, pw = S(100), rh = S(20);
            overlays.push_back([=]() {
                rect(px + 1, py + 1, pw - 2, rh * 2 - 2, 0xff232323); outline(px, py, pw, rh * 2, 0xff0c0c0c);
                static const wchar_t* items[2] = {L"Copy", L"Paste"};  // PTR 0x4346d494
                for (int i = 0; i < 2; i++) {
                    if (i == hv) rect(px + 1, py + rh * i + 1, pw - 2, rh - 2, 0xff191919);
                    textFont->draw(px + S(10), py + rh * i + (rh - textFont->height) / 2, items[i], 0xc8ffffff);
                }
            });
        }
    }
};

// input field look shared by Textbox_Render 0x43364b5c and the Listbox search row: 0x32 border, 0x10 inner border, 0x19 fill
static void inputFrame(int x, int y, int w, int h, D3DCOLOR a) {
    outline(x, y, w, h, a | 0x323232); outline(x + 1, y + 1, w - 2, h - 2, a | 0x101010); rect(x + 2, y + 2, w - 4, h - 4, a | 0x191919);
}
// ---- Listbox (vtable 0x434708f0): rows of 0x12, optional search row (+0x7c), selection +0x88, value = item id
struct Listbox : Element {
    std::vector<std::wstring> items; std::vector<int> ids; int* value; bool search; int sel = -1, scroll = 0;
    int rowH = 0x12;
    std::wstring query;
    void HandleDPI() override { Element::HandleDPI(); rowH = S(0x12); }
    Listbox(int W, int H, int* v, bool srch) : value(v), search(srch) { w = W; h = H; kind = 6; }  // ctor 0x433eda39
    void HandleResize(int cw) override { w = cw - S(100); if (w < S(40)) w = S(40); if (w > S(200)) w = S(200); }  // 0x4333c9a7 (100/40/200 @0x43468f64..)
    int rows() const { return (h - (search ? rowH + 1 : 0)) / rowH; }
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x4336c227
        if (items.empty()) return false;
        int ly = p.y - y - (search ? rowH + 1 : 0);
        if (m.msg == WM_LBUTTONDOWN && IsHovered(p)) {
            if (ly < 0) { focused = this; return true; }  // search row takes the keyboard (+0x7d)
            int i = (ly + scroll) / rowH;
            if (i >= (int)items.size()) return false;
            sel = i; if (value) *value = i < (int)ids.size() ? ids[i] : i;  // FUN_433b0844 + value = item id
            fire(); return true;
        }
        if (m.msg == WM_MOUSEWHEEL && IsHovered(p)) {  // 4 rows per notch (FUN_433e1d26)
            int mx = ((int)items.size() - rows()) * rowH; scroll += GET_WHEEL_DELTA_WPARAM(m.w) > 0 ? -rowH * 4 : rowH * 4;
            if (scroll > mx) scroll = mx; if (scroll < 0) scroll = 0; return true;
        }
        return false;
    }
    bool HandleInput(const Msg& m) override {
        if (!search || focused != this) return false;
        if (m.msg == WM_CHAR && m.w >= 0x20 && m.w != 0x60 && query.size() < 63) { query += (wchar_t)m.w; return true; }
        if (m.msg == WM_KEYDOWN && m.w == VK_BACK && !query.empty()) { query.pop_back(); return true; }
        return false;
    }
    bool WantsFocus() const override { return search && focused == this; }
    void Render(Pt o) override {  // 0x434179de
        int bx = o.x + x, by = o.y + y;
        rect(bx + 1, by + 1, w - 2, h - 2, editable ? 0xff232323 : 0x50232323); outline(bx, by, w, h, 0xff0c0c0c);
        if (search) {  // search row: RectFilledOutlined 0x191919 / 0x323232 + RectOut 0x101010 inside, "%ls_" at x+6
            D3DCOLOR a = editable ? 0xff000000 : 0x50000000;
            inputFrame(bx + 1, by + 1, w - 2, rowH, a);
            D3DCOLOR tc = focused == this ? menuColor() : 0xc8ffffff; if (!editable) tc = (tc & 0xffffff) | 0x50000000;
            textFont->draw(bx + 6, by + 1 + (rowH - textFont->height) / 2, query + L"_", tc, bx + w - 5);
            by += rowH + 1;
        }
        int first = scroll / rowH, n = rows();
        for (int r = 0; r < n && first + r < (int)items.size(); r++) {
            int i = first + r, ry = by + rowH * r;
            if (i == sel) rect(bx + 1, ry + 1, w - 2, rowH - 2, 0xff1a1a1a);
            bool cur = value && i < (int)ids.size() && ids[i] == *value && sel >= 0;
            D3DCOLOR c = cur ? menuColor() : 0xc8ffffff; if (!editable) c = (c & 0xffffff) | 0x50000000;
            textFont->draw(bx + 10, ry + (rowH - textFont->height) / 2, items[i], c, bx + w - 10);
        }
    }
};

// ---- Textbox (vtable 0x4347232c): 0x9e x 0x14, up to 63 chars, "_" caret, menu colour while focused
struct Textbox : Element {
    std::wstring text;
    Textbox() { w = 0x9e; h = 0x14; kind = 10; }  // ctor 0x4336a72d
    void HandleResize(int cw) override { w = cw - S(100); if (w > S(200)) w = S(200); if (w < S(40)) w = S(40); }  // 0x4331e326
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x4332ad0f
        if (!visible || m.msg != WM_LBUTTONDOWN || !IsHovered(p)) return false;
        focused = this; return true;
    }
    bool HandleInput(const Msg& m) override {  // 0x43344b4d
        if (m.msg == WM_KEYDOWN) {
            if (m.w == VK_BACK && !text.empty()) { if (GetKeyState(VK_CONTROL) & 0x8000) text.clear(); else text.pop_back(); }
            else if (m.w == VK_ESCAPE) focused = nullptr;
            return true;
        }
        if (m.msg != WM_CHAR) return false;
        if (m.w != 8 && m.w != 0x60 && text.size() < 0x3f) text += (wchar_t)m.w;
        return true;
    }
    bool WantsFocus() const override { return focused == this; }
    void Render(Pt o) override {  // 0x43364b5c
        int bx = o.x + x, by = o.y + y;
        rect(bx + 1, by + 1, w - 2, h - 2, editable ? 0xff232323 : 0x50232323); outline(bx, by, w, h, 0xff0c0c0c);
        if (!editable) return;
        inputFrame(bx + 1, by + 1, w - 2, h - 2, 0xff000000);  // second RectFilledOutlined 0x191919 / 0x323232, then RectOut 0x101010 (matches GS pixels)
        D3DCOLOR c = focused == this ? menuColor() : 0xc8ffffff;
        textFont->draw(bx + 6, by + (h - textFont->height) / 2, text + L"_", c, bx + w - 10);
    }
};

// ---- Rage "Weapon type" selector: Combobox subclass, vtable 0x4347085c (ctor 0x4348e18f). No box frame (slot 15 is empty);
// each row (slot 14, 0x4332c152) = per-weapon enable checkbox (FUN_433d24ec) + name + the game's weapon icon on the right.
// It fills its groupbox (slot 11 0x43330c8a: x = LeftPadding, y = 0; slot 9 0x43381fc8: size = groupbox size).
static Image weaponIcons[5][12];  // [dpi step][weapon type]: CS:GO panorama SVGs (defindex table 0x4346ef30), rasterised per DPI like the original
static int dpiStep = 0;
struct WeaponSelect : Element {
    std::vector<std::wstring> opts; std::vector<int> vals; int* value; bool* enabled;  // enabled[type]: vars PTR 0x4346efe0[type]+0x10
    bool open = false, pressed = false; int hoverItem = -1, scroll = 0;
    int rowH = 0x1e;  // +0x60 = S(30)
    // right-click popup (class 0x4346fab0 built by 0x4339d578: items "Copy" / "Paste", callback 0x433e7f67 -> 0x433e3b88):
    // +0xb8 = the weapon it acts on (the row under the cursor, or the current one on the box), +0xdc/+0xe0 = mouse position
    bool ctxOpen = false, ctxLatched = false, rLatched = false; int ctxHover = -1, ctxTarget = -1; Pt ctxPos{};
    std::function<void(int weapon, int item)> onContext;  // item 0 copy (0x4335c6c2), 1 paste (0x43394985)
    int ctxW() const { return S(100); }
    int ctxRow() const { return S(20); }
    bool ctxHit(Pt p) const { return ctxOpen && p.x >= ctxPos.x && p.x <= ctxPos.x + ctxW() && p.y >= ctxPos.y && p.y <= ctxPos.y + ctxRow() * 2; }
    WeaponSelect(std::vector<std::wstring> o, std::vector<int> v, int* val, bool* en) : opts(std::move(o)), vals(std::move(v)), value(val), enabled(en) { kind = 4; lp = 0; }
    void HandleDPI() override { rowH = S(0x1e); }  // x/y/size come from the groupbox (slots 9/11)
    bool fills() const override { return true; }
    void HandleResize(int cw) override { w = cw; }  // h set by the layout
    int sel() const { for (int i = 0; i < (int)opts.size(); i++) if (value && vals[i] == *value) return i; return 0; }
    int shown() const { return (int)opts.size() < 8 ? (int)opts.size() : 8; }
    int popupPriority() const { return (open || ctxOpen) ? 2 : 0; }
    bool boxHit(Pt p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    bool listHit(Pt p) const { return open && p.x >= x && p.x <= x + w && p.y > y + h && p.y <= y + h + 1 + shown() * rowH; }
    bool IsHovered(Pt p) override { return boxHit(p) || listHit(p) || ctxHit(p); }  // 0x433fbc33 (popup first)
    bool HandleMouse(const Msg& m, Pt p) override {  // 0x433ea5e2 -> Combobox_HandleMouse, rows 0x4337febf
        int row = (p.y - (y + h + 1)) / rowH, i = scroll / rowH + row;
        if (ctxOpen) {  // the popup takes the mouse first (0x433ea5e2: +0x120 set -> popup vtable +8 / +4)
            if (m.msg == WM_MOUSEMOVE) { ctxHover = ctxHit(p) ? (p.y - ctxPos.y) / ctxRow() : -1; return ctxHit(p); }
            if (m.msg == WM_LBUTTONDOWN) { if (ctxHit(p)) { ctxLatched = true; return true; } ctxOpen = false; return false; }
            if (m.msg == WM_LBUTTONUP && ctxLatched) {
                ctxLatched = false;
                if (!ctxHit(p)) return false;
                int item = (p.y - ctxPos.y) / ctxRow(); ctxOpen = false;
                if (onContext && ctxTarget >= 0 && ctxTarget < 12 && item >= 0 && item < 2) onContext(ctxTarget, item);
                return true;
            }
            if (m.msg == WM_RBUTTONDOWN || m.msg == WM_RBUTTONUP) { if (!ctxHit(p)) ctxOpen = false; else return true; }
        }
        if (m.msg == WM_RBUTTONDOWN) { if (boxHit(p) || listHit(p)) { rLatched = true; return true; } return false; }
        if (m.msg == WM_RBUTTONUP) {  // 0x205: pick the target weapon, open the popup at the cursor
            if (!rLatched) return false; rLatched = false;
            if (listHit(p) && i < (int)opts.size()) ctxTarget = vals[i];
            else if (boxHit(p)) ctxTarget = value ? *value : 0;
            else return false;
            ctxPos = p; ctxOpen = true; ctxHover = -1;
            return true;
        }
        if (m.msg == WM_MOUSEMOVE) { hovered = boxHit(p); hoverItem = listHit(p) ? row : -1; return open && listHit(p); }
        if (m.msg == WM_LBUTTONDOWN) { if (!IsHovered(p)) { open = false; return false; } pressed = true; return true; }
        if (m.msg == WM_LBUTTONUP) {
            if (!pressed) return false; pressed = false;
            if (listHit(p) && i < (int)opts.size()) {
                if (p.x - x < S(7) + S(13) + S(8) + 4) { enabled[vals[i]] = !enabled[vals[i]]; return true; }  // left of the text: toggle that weapon (list stays open)
                if (value && *value != vals[i]) { *value = vals[i]; fire(); }  // FUN_433f49b3
                open = false; return true;
            }
            if (boxHit(p)) { open = !open; return true; }
            return false;
        }
        if (m.msg == WM_MOUSEWHEEL && open && listHit(p)) {
            int mx = ((int)opts.size() - 8) * rowH; scroll += GET_WHEEL_DELTA_WPARAM(m.w) > 0 ? -rowH * 4 : rowH * 4;
            if (scroll > mx) scroll = mx; if (scroll < 0) scroll = 0; return true;
        }
        return false;
    }
    void ResetInputState() override { open = false; pressed = false; hoverItem = -1; ctxOpen = false; ctxLatched = false; rLatched = false; }
    // 0x4332c152, called by Combobox_Render with the row origin at x + S(7). Icon: white@0xc8 for the selected item or when
    // the caller passes flag 1 (the hovered popup row), else white@0x50.
    void drawRow(int ox, int oy, int rh, int i, bool bold, bool lit) const {
        int t = vals[i];
        if (t >= 0 && t < 12 && weaponIcons[dpiStep][t].tex) {
            const Image& im = weaponIcons[dpiStep][t];
            int ix = ox + w - im.w - S(40), iy = oy + (im.h < rh ? (rh - im.h) / 2 : 0);
            quad((float)ix, (float)iy, (float)im.w, (float)im.h, (lit || i == sel()) ? 0xc8ffffff : 0x50ffffff, im.tex);
        }
        Checkbox::drawBox(ox + S(13), oy + (rh - S(8)) / 2, enabled[t], false);
        Font& f = bold ? fBold : *textFont;  // Combo_s14 flag 1 (bold) for the selected item
        f.draw(ox + S(13) + S(20), oy - 1 + (rh - f.height) / 2, opts[i], 0xc8ffffff);
    }
    void Render(Pt o) override {
        int bx = o.x + x, by = o.y + y;
        drawRow(bx + S(7), by, h, sel(), true, true);
        D3DCOLOR vc = 0x88ffffff;  // Combobox arrow 0x433c9f33
        int ax = bx + w - S(12), ay = by + (h - 2 - 1) / 2;
        rect(ax, ay - 1, 5, 1, 0xff000000); rect(ax, ay, 5, 1, vc); rect(ax + 1, ay + 1, 3, 1, vc); rect(ax + 2, ay + 2, 1, 1, vc);
        if (!open) { renderContext(o); return; }
        int ly = by + h + 1, n = shown(), first = scroll / rowH, cur = sel(), hv = hoverItem;
        overlays.push_back([=]() {
            rect(bx + 1, ly + 1, w - 2, rowH * n - 2, 0xff232323); outline(bx, ly, w, rowH * n, 0xff0c0c0c);
            for (int r = 0; r < n && first + r < (int)opts.size(); r++) {
                if (r == hv) rect(bx + 1, ly + rowH * r + 1, w - 2, rowH - 2, 0xff191919);
                drawRow(bx + S(7), ly + rowH * r, rowH, first + r, first + r == cur, r == hv);
            }
            popupScrollbar(bx, ly, w, rowH * n, (int)opts.size() * rowH, scroll);
        });
        renderContext(o);  // after the list, so it is drawn on top
    }
    void renderContext(Pt o) {  // same popup look as the colour picker's Copy / Paste (0x4346fab0 render)
        if (!ctxOpen) return;
        int px = o.x + ctxPos.x, py = o.y + ctxPos.y, pw = ctxW(), rh = ctxRow(), hv = ctxHover;
        overlays.push_back([=]() {
            rect(px + 1, py + 1, pw - 2, rh * 2 - 2, 0xff232323); outline(px, py, pw, rh * 2, 0xff0c0c0c);
            static const wchar_t* items[2] = {L"Copy", L"Paste"};
            for (int k = 0; k < 2; k++) {
                if (k == hv) rect(px + 1, py + rh * k + 1, pw - 2, rh - 2, 0xff191919);
                textFont->draw(px + S(10), py + rh * k + (rh - textFont->height) / 2, items[k], 0xc8ffffff);
            }
        });
    }
};

// ---- Child / groupbox (vtable 0x4347072c). Placement on the 24x24 block grid (0x43384477), item layout (0x433ad23f),
// input (0x4333f951: scroll, drag with swap 0x4334c2c3), content render with fades/arrows/scrollbar (0x433d0330).
struct Child : Element {
    std::wstring title; std::vector<Element*> items;
    int bx, bw, by, bh;           // +0x70 PosSize in blocks
    int padX = 20, padY = 10;     // +0x68 Padding: x = item column/top inset, y = inset from the block rect
    bool movable = true;          // +0xb0
    bool scrollable = false;      // +0x94
    int scroll = 0, content = 0;  // +0xa8, +0xa0
    int status = 0; Pt anchor{};  // +0x7c (1 drag, 2 resize), +0x74
    bool sbDrag = false; int sbGrab = 0;  // scrollbar state (FUN_4340b88b)
    Menu* menu = nullptr;
    int autoH = 0, autoH0 = 0;  // PosSize.h == 0xff: height = +0x64 = S(+0x60, set by the builder), pushes later boxes in its columns down (0x43384477)
    void HandleDPI() override {
        padX = S(20); padY = S(10);
        if (autoH0 == 0) autoH0 = autoH;
        autoH = S(autoH0);
        for (Element* e : items) e->HandleDPI();
    }
    int d0[4];  // default PosSize, restored by "Reset menu layout"
    Child(const wchar_t* t, int BX, int BW, int BY, int BH) : title(t), bx(BX), bw(BW), by(BY), bh(BH), d0{BX, BW, BY, BH} { kind = 7; }
    void resetLayout() { bx = d0[0]; bw = d0[1]; by = d0[2]; bh = d0[3]; scroll = 0; status = 0; }
    template <class T> T* add(T* e) { e->parent = this; items.push_back(e); return e; }
    void layout() {  // 0x433ad23f
        int cur = padX, row = padX;
        for (Element* e : items) {
            if (!e->visible) continue;
            if (e->fills()) { e->x = e->lp; e->y = 0; e->HandleResize(w); e->h = h; continue; }
            if (!e->same) { e->y = cur; e->x = e->lp + padX; if (e->h > 0) { row = cur; cur += 5 + e->h; } }
            else if (e->kind == 12 || e->kind == 9) { e->x = e->lp - e->w + w - padX * 2; e->y = row + 1; row += 5 + e->h; }
            else { e->x = e->lp + padX; e->y = row; }
            e->HandleResize(w);
        }
        scrollable = items.size() >= 2 && cur > h - 5;
        content = scrollable ? padX + cur : 0;
        if (!scrollable) scroll = 0;
        scrollBy(0);
    }
    int maxScroll() const { return content > h ? content - h : 0; }
    void scrollBy(int d) { scroll += d; if (scroll < 0) scroll = 0; if (scroll > maxScroll()) scroll = maxScroll(); }  // 0x433e1d26
    bool IsHovered(Pt p) override { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    bool HandleMouse(const Msg& m, Pt p) override;  // defined after Menu
    void ResetInputState() override { for (Element* e : items) e->ResetInputState(); }
    // shortcut: scrollbar thumb size measured from ref (thumb maths in FUN_433c8760 not read yet)
    int thumbTop() const { int tl = thumbLen(); return maxScroll() ? (h - 2 - tl) * scroll / maxScroll() : 0; }
    bool scrollbarMouse(const Msg& m, int lx, int ly) {  // FUN_4340b88b (child-local, unscrolled)
        int tl = thumbLen(), tt = 2 + thumbTop(), view = h - 2;
        if (m.msg == WM_MOUSEMOVE && sbDrag) {
            int d = ly - sbGrab;
            if (d < 1) scroll = 0;
            else if (view < tl + 1 + d) scroll = maxScroll();
            else if (tl < view) scroll = maxScroll() * (d - 1) / (view - tl);
            return true;
        }
        if (m.msg == WM_LBUTTONDOWN) {
            if (lx < w - 7 || lx > w - 1 || ly < 1 || ly > view) return false;  // shortcut: bar zone = drawn track (dpi constant not read)
            if (ly >= tt && ly <= tt + tl) { sbDrag = true; sbGrab = ly - tt; }
            else scrollBy(ly < tt ? 1 - view : view - 1);  // click on the track pages by the view height - 1
            return true;
        }
        return false;
    }
    int thumbLen() const { int track = h - 2; return content ? (int)((float)track * h / content + 0.5f) + 1 : track; }
    void Render(Pt o) override {
        int X = o.x + x, Y = o.y + y, W = w + 1, H = h + 1;  // frame covers one pixel past the layout size
        // 0x4334af01: inner border = menu colour while resizing (status 2), else +0xb8 (0x282828)
        D3DCOLOR edge = status == 2 ? menuColor() : gray(0x28);
        outline(X, Y, W, H, gray(0x0c)); outline(X + 1, Y + 1, W - 2, H - 2, edge); rect(X + 2, Y + 2, W - 4, H - 4, gray(0x17));
        static int sa = 120, ca = 253;  // shortcut: title shadow/text alpha fitted to ref (title path in 0x4334af01 not fully ported)
        int tw = fBold.width(title);
        texBg(X + 11, Y, tw + 6, 2);
        fBold.draw(X + 15, Y - 5, title, gray(0, sa));
        fBold.draw(X + 14, Y - 6, title, status == 1 ? ((menuColor() & 0xffffff) | (D3DCOLOR)ca << 24) : gray(0xcd, ca));  // title = menu colour while dragging (Child_Render 0x433ae90f)
        // content (0x433d0330): clip to the box (scrollable: from y+7), items shifted by the scroll
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        RECT sc = {X + 2, scrollable ? Y + 7 : Y + 2, X + W - 2, Y + H - 2};
        dev->SetScissorRect(&sc);
        Pt io{X, Y - scroll};
        for (Element* e : items) if (e->visible) e->Render(io);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        if (!scrollable && movable && !layoutLocked) {  // resize grip: Triangle in the edge colour (bottom-right, 5 px), AA hypotenuse
            int rx = X + W - 2, by2 = Y + H - 2;
            for (int k = 0; k < 5; k++) { rect(rx - k, by2 - 5 + k, k + 1, 1, edge); rect(rx - k - 1, by2 - 5 + k, 1, 1, (edge & 0xffffff) | 0xb3000000); }
        }
        if (scrollable) {
            if (scroll > 0) {  // top fade + up arrow
                gradV(X + 2, Y + 7, w - 9, 20, 0xff171717, 0x00171717);
                int ax = X + w - S(17), ay = Y + 1 + S(9);  // Tab_vt1_Render: S(17) / S(9)
                rect(ax + 2, ay, 1, 1, 0xc8ffffff); rect(ax + 1, ay + 1, 3, 1, 0xc8ffffff); rect(ax, ay + 2, 5, 1, 0xc8ffffff); rect(ax, ay + 3, 5, 1, 0xff000000);
            }
            if (scroll < maxScroll()) {  // bottom fade + down arrow
                gradV(X + 2, Y + h - 21, w - 9, 20, 0x00171717, 0xff171717);
                int ax = X + w - S(17), ay = Y + h - 1 - S(9);
                rect(ax, ay, 5, 1, 0xff000000); rect(ax, ay + 1, 5, 1, 0xc8ffffff); rect(ax + 1, ay + 2, 3, 1, 0xc8ffffff); rect(ax + 2, ay + 3, 1, 1, 0xc8ffffff);
            }
            int track = h - 2, tl = thumbLen(), ty = thumbTop();
            rect(X + w - 7, Y + 2, 6, track, gray(0x28));
            rect(X + w - 6, Y + 2 + ty, 5, tl, gray(0x41));
        }
    }
};

// ---- Tab (vtable 0x4346f9c8). Slot 75x64, stacked every 64 px (ctor 0x4348e41d).
static Image tabIcons;  // original 384x48 RGBA strip from the crack's menuBin: rage,legit,visuals,misc,skins,players,aa,config
static const int ICON_OF_TAB[8] = {0, 6, 1, 2, 3, 4, 5, 7};
struct Tab : Element {
    int index; Menu* menu; std::vector<Child*> children;
    Tab(int i, Menu* m, int X, int Y) : index(i), menu(m) { x = X; y = Y; w = 0x4b; h = 0x40; lp = 0; }
    bool HandleMouse(const Msg& m, Pt p) override;  // 0x433ecea1
    void placeChildren();                            // slot 14 0x43384477 + 0x433cf58b
    void Render(Pt o) override;                      // 0x433ac635
    void ResetInputState() override { for (Child* c : children) c->ResetInputState(); }
    void HandleDPI() override { Element::HandleDPI(); for (Child* c : children) c->HandleDPI(); }
};

// ---- CMenu (vtable 0x4346f40c; handlers registered at 0x434857xx)
struct Menu {
    int px = 0, py = 0, sw = WW, sh = WH;  // +0xc pos, +0x14 size
    int minW = WW, minH = WH;              // +0x1c default size = minimum, S()-scaled (0x433b23fc)
    unsigned status = 1;                   // +0x34: 1 open, 4 dragging, 8 resizing
    unsigned buttons = 0;                  // +0x38: mouse button bits
    Pt mouse{}, anchor{};                  // +0x3c, +0x44
    std::vector<Tab*> tabs; unsigned current = 4;
    int fade = 0; DWORD fadeTick = 0; float alpha = 1.f;  // +0x8c, +0x88, +0xb0
    HWND hwnd = nullptr;
    Pt tabStart{7, 10}, tabEnd{7, 6};  // +0x24 / +0x2c (solved from the ref: reproduces all three Misc groupboxes)
    void relayout() { for (Tab* t : tabs) t->placeChildren(); }
    int gripSize() const { return S(15); }  // resize grip: S(15) (0x43327d1b)

    bool forward(const Msg& m) {  // FUN_43398693 + FUN_4334eae8
        mouse = {GET_X_LPARAM(m.l), GET_Y_LPARAM(m.l)};
        Pt rel{mouse.x - px, mouse.y - py};
        bool r = false;
        for (int i = (int)tabs.size() - 1; i >= 0; i--) if (tabs[i]->visible && tabs[i]->HandleMouse(m, rel)) r = true;
        return r;
    }
    bool inside() const { return mouse.x >= px && mouse.x <= px + sw && mouse.y >= py && mouse.y <= py + sh; }
    bool onLButtonDown(const Msg& m) {  // 0x43327c83
        buttons |= 1;
        if (forward(m)) return true;
        if (!inside()) { focused = nullptr; return false; }
        for (Tab* t : tabs) t->ResetInputState();
        focused = nullptr;
        if (sw - gripSize() < mouse.x - px && sh - gripSize() < mouse.y - py) status |= 8; else status |= 4;
        anchor = mouse;
        return true;
    }
    bool onLButtonUp(const Msg& m) {  // 0x433ee1f2
        status &= ~0xcu; buttons &= ~1u;
        bool r = forward(m);
        status &= ~0xcu; buttons &= ~1u;
        if (auto* s = dynamic_cast<Slider*>(focused)) s->dragging = false;
        return r;
    }
    bool onMouseMove(const Msg& m) {  // 0x4339e90b
        bool r = forward(m);
        if (status & 4) {
            int nx = px - anchor.x + mouse.x, ny = py - anchor.y + mouse.y;
            RECT cr; if (GetClientRect(hwnd, &cr)) {  // keep at least 2/3 of the menu on screen
                nx = (std::max)(-sw / 3, (std::min)(nx, (int)cr.right - sw / 3));
                ny = (std::max)(-sh / 3, (std::min)(ny, (int)cr.bottom - sh / 3));
            }
            if (nx != px || ny != py) { anchor = mouse; px = nx; py = ny; }
        } else if ((status & 8) && (mouse.x != anchor.x || mouse.y != anchor.y)) {
            int nw = sw + mouse.x - anchor.x, nh = sh + mouse.y - anchor.y;
            int ow = sw, oh = sh;
            if (minW <= nw) { sw = nw; anchor.x = mouse.x; }
            if (minH <= nh) { sh = nh; anchor.y = mouse.y; }
            if (ow != sw || oh != sh) relayout();  // tabs HandleResize
        }
        unsigned st = (unsigned)m.w & 0xf, changed = buttons ^ st;  // synthesize button edges from wParam
        if (changed) {
            buttons = st;
            if (changed & 1) (st & 1) ? onLButtonDown(m) : onLButtonUp(m);
            if (changed & 2) { if (!(st & 2)) { buttons &= ~2u; forward(m); } else { buttons |= 2; forward(m); } }
        }
        return r || inside();
    }
    bool handle(const Msg& m0) {  // FUN_433ba305 (+ double-click folding from FUN_43405c69)
        Msg m = m0;
        if (m.msg == WM_LBUTTONDBLCLK) m.msg = WM_LBUTTONDOWN;
        if (m.msg == WM_RBUTTONDBLCLK) m.msg = WM_RBUTTONDOWN;
        if (!(status & 1) || fade) return false;
        if (focused && focused->WantsFocus() && focused->HandleInput(m)) return true;
        switch (m.msg) {
        case WM_MOUSEMOVE: return onMouseMove(m);
        case WM_LBUTTONDOWN: return onLButtonDown(m);
        case WM_LBUTTONUP: return onLButtonUp(m);
        case WM_RBUTTONDOWN: { buttons |= 2; bool r = forward(m); return r || inside(); }  // 0x4334c26e
        case WM_RBUTTONUP: buttons &= ~2u; return forward(m);
        case WM_MOUSEWHEEL: { POINT pt{GET_X_LPARAM(m.l), GET_Y_LPARAM(m.l)}; ScreenToClient(hwnd, &pt); Msg mm = m; mm.l = MAKELPARAM(pt.x, pt.y); return forward(mm); }
        }
        return false;
    }
    void switchTab(unsigned i) {  // 0x433b75d3
        if (i == current || i >= tabs.size()) return;
        tabs[current]->ResetInputState(); current = i;
    }
    void toggle() {  // fade in/out (0x43384045), 150 ms
        fadeTick = GetTickCount();
        if (status & 1) fade = 1; else { status |= 1; fade = 2; }
    }
    void updateFade() {
        float t = (GetTickCount() - fadeTick) / 150.f;
        if (fade == 2) { alpha = t; if (t > 1.f) { fade = 0; alpha = 1.f; } }
        else if (fade == 1) { if (t > 1.f) { status &= ~0xdu; fade = 0; alpha = 1.f; for (Tab* tb : tabs) tb->ResetInputState(); } else alpha = 1.f - t; }
        else alpha = 1.f;
    }
    void renderFrame() {  // 0x433ad3f2 + 0x433d2ab3 + 0x4338490e
        int x = px, y = py, w = sw, h = sh;
        outline(x, y, w, h, gray(0x0c));
        outline(x + 1, y + 1, w - 2, h - 2, gray(0x3c));
        rect(x + 2, y + 2, w - 4, h - 4, gray(0x28));
        outline(x + 5, y + 5, w - 10, h - 10, gray(0x3c));
        rect(x + 6, y + 6, w - 12, h - 12, gray(0x0c));
        texBg(x + 6, y + 10, w - 12, h - 16);
        // sidebar (0x433d2ab3): fill + black/0x28 edge, notched around the active tab
        int sx = x + 6, top = y + 10, bot = y + h - 6;
        Tab* at = tabs[current];
        int ay = y + at->y;
        const int tw = at->w, th = at->h;  // tab slot (75 x 64 at 100%, S()-scaled): the sidebar and its notch follow it
        rect(sx, top, tw - 2, ay - 2 - top, gray(0x0c)); rect(sx + tw - 2, top, 1, ay - 1 - top, gray(0)); rect(sx + tw - 1, top, 1, ay - top, gray(0x28));
        rect(sx, ay - 2, tw - 1, 1, gray(0)); rect(sx, ay - 1, tw, 1, gray(0x28));
        rect(sx, ay + th, tw, 1, gray(0x28)); rect(sx, ay + th + 1, tw - 1, 1, gray(0));
        rect(sx, ay + th + 2, tw - 2, bot - ay - th - 2, gray(0x0c)); rect(sx + tw - 2, ay + th + 1, 1, bot - ay - th - 1, gray(0)); rect(sx + tw - 1, ay + th, 1, bot - ay - th, gray(0x28));
        // rainbow bar (0x4338490e)
        int bw = w - 14, half = bw / 2;
        gradH(x + 7, y + 7, half, 2, rgb(55, 177, 218), rgb(204, 70, 205));
        gradH(x + 7 + half, y + 7, bw - half, 2, rgb(204, 70, 205), rgb(204, 227, 53));
        rect(x + 7, y + 8, bw, 2, 0x77000000);  // 2 rows: darkens the bar bottom and the row under it
    }
    void render() {  // 0x433ac5e2
        updateFade();
        if (!(status & 1) && !fade) return;
        globalAlpha = alpha;
        bgOX = px; bgOY = py;
        renderFrame();
        for (Tab* t : tabs) if (t->visible) t->Render({px, py});
        for (auto& f : overlays) f();
        overlays.clear();
        globalAlpha = 1.f;
    }
};

void Element::setVisible(bool v) {  // FUN_433e000b
    if (visible == v) return;
    visible = v;
    if (parent) parent->layout();
}
void Tab::placeChildren() {  // 0x43384477
    const float B = 24.f;
    int pad = S(10);  // tab +0x94 = S(10)
    int x0 = 0 + w + pad + menu->tabStart.x, x1 = menu->sw - (menu->tabEnd.x + pad);
    int y0 = 0 + menu->tabStart.y + pad, y1 = menu->sh - (menu->tabEnd.y + pad);
    int col[24] = {};  // per block column: height taken by auto-height boxes above
    for (Child* c : children) {
        c->menu = menu;
        if (!c->visible) continue;
        int top = col[c->bx < 24 ? c->bx : 23] + y0, p = c->padY;
        c->w = (int)((float)c->bw / B * (float)(x1 - x0));
        int nx = (int)((float)c->bx * ((float)(x1 - x0) / B) + (float)x0) + 0;  // + LeftPadding (0 for groupboxes)
        int ny;
        if (c->autoH) {
            int hh = c->autoH + p * 2;
            for (int i = c->bx; i < c->bw && i < 24; i++) col[i] = hh;  // sic: columns bx .. bw-1
            c->h = hh;
            ny = (int)(((float)(y1 - top) / B) * (float)c->by + (float)top);
        } else {
            c->h = (int)((float)c->bh / B * (float)(y1 - top));
            ny = (int)((float)c->by * ((float)(y1 - top) / B) + (float)top);
        }
        c->x = nx + p; c->y = ny + p; c->w -= 2 * p; c->h -= 2 * p;
        if (c->bx + c->bw > 23 && c->x + c->w != x1 - p) c->w = (x1 - p) - c->x;
        if (!c->autoH && c->by + c->bh > 23 && c->y + c->h != y1 - p) c->h = (y1 - p) - c->y;
        c->layout();
    }
}
bool Child::HandleMouse(const Msg& m, Pt p) {  // 0x4333f951
    int lx = p.x - x, ly0 = p.y - y, ly = ly0;
    bool outside = false;
    if (scrollable) { outside = lx < 5 || lx > w - 10 || ly0 < 20 || ly0 > h - 10; ly += scroll; }
    if (m.msg == WM_LBUTTONUP) { status = 0; if (sbDrag) { sbDrag = false; return true; } }
    if (scrollable && scrollbarMouse(m, lx, ly0)) { for (Element* e : items) e->ResetInputState(); return true; }
    for (Element* e : items)
        if (e->editable && e->visible && (!outside || (dynamic_cast<Combo*>(e) && ((Combo*)e)->popupPriority()) || (dynamic_cast<Hotkey*>(e) && ((Hotkey*)e)->popupPriority()) || (dynamic_cast<ColorPicker*>(e) && ((ColorPicker*)e)->popupPriority()) || (dynamic_cast<WeaponSelect*>(e) && ((WeaponSelect*)e)->popupPriority())) && e->HandleMouse(m, {lx, ly})) return true;
    if (m.msg == WM_LBUTTONDOWN && IsHovered(p)) {
        anchor = p;
        if (!layoutLocked && movable) {
            const int g = S(15);  // DAT_4346ff70 * dpi
            if (w - g < lx && h - g < ly0) { status = 2; return true; }
            if (ly0 < g) { status = 1; return true; }
        }
    } else if (m.msg == WM_MOUSEMOVE && !layoutLocked && movable && status) {
        // 0x4334c2c3 drag: one block per threshold crossed; landing on a sibling swaps the two PosSize records
        int tx = (menu->sw - 0x69) / 24, ty = (menu->sh - 0x23) / 24, dx = p.x - anchor.x, dy = p.y - anchor.y;
        int nbx = bx, nby = by, nbw = bw, nbh = bh;
        if (status == 1) {
            if (abs(dx) >= tx) nbx = (std::max)(0, (std::min)(24 - bw, bx + (dx < 1 ? -1 : 1)));
            if (abs(dy) >= ty) nby = (std::max)(0, (std::min)(24 - bh, by + (dy < 1 ? -1 : 1)));
        } else {  // shortcut: resize (FUN_4341d248) not read yet; modelled on the drag handler, one block per threshold
            if (abs(dx) >= tx) nbw = (std::max)(1, (std::min)(24 - bx, bw + (dx < 1 ? -1 : 1)));
            if (abs(dy) >= ty) nbh = (std::max)(1, (std::min)(24 - by, bh + (dy < 1 ? -1 : 1)));
        }
        if (nbx != bx || nby != by || nbw != bw || nbh != bh) {
            Tab* t = menu->tabs[menu->current];
            Child* hit = nullptr;
            if (status == 1)
                for (Child* o : t->children) if (o != this && nbx < o->bx + o->bw && o->bx < nbx + bw && nby < o->by + o->bh && o->by < nby + bh) { hit = o; break; }
            if (hit) { std::swap(bx, hit->bx); std::swap(bw, hit->bw); std::swap(by, hit->by); std::swap(bh, hit->bh); }
            else { bx = nbx; by = nby; bw = nbw; bh = nbh; }
            anchor = p;
            t->placeChildren();
        }
        return true;
    }
    if (!scrollable) return false;
    if (m.msg == WM_MOUSEWHEEL && IsHovered(p)) {
        const int step = S(20);  // Child_HandleMouse 0x4333fbb8: S(20)
        scrollBy(GET_WHEEL_DELTA_WPARAM(m.w) > 0 ? -step : step);
        for (Element* e : items) e->ResetInputState();
        return true;
    }
    return false;
}
bool Tab::HandleMouse(const Msg& m, Pt p) {
    if (m.msg == WM_LBUTTONDOWN || m.msg == WM_MOUSEMOVE) {
        if (IsHovered(p)) {
            if (m.msg == WM_LBUTTONDOWN) { menu->switchTab(index); return true; }
            hovered = true; return false;
        }
        hovered = false;
    }
    if (menu->current == (unsigned)index)
        for (Child* c : children) if (c->visible && c->HandleMouse(m, p)) return true;
    return false;
}
void Tab::Render(Pt o) {
    bool active = menu->current == (unsigned)index;
    D3DCOLOR tint = active ? 0xffffffff : hovered ? 0xc8ffffff : 0x64ffffff;
    if (tabIcons.tex) {
        int ix = o.x + x + (w - 48) / 2, iy = o.y + y + (h - 48) / 2, k = ICON_OF_TAB[index];
        quad((float)ix, (float)iy, 48.f, 48.f, tint, tabIcons.tex, k * 48 / 384.f, 0.f, (k + 1) * 48 / 384.f, 1.f);
    }
    if (active) for (Child* c : children) if (c->visible) c->Render(o);
}

// config-var identity for an element (emitted by re/gen.js from the builder's var lookups and the var registrations)
static void cfgVar(Element* e, uint32_t hash, int type, int size) { e->cfgHash = hash; e->cfgType = (uint8_t)type; e->cfgSize = (uint16_t)size; }
