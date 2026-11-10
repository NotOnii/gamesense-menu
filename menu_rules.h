// menu_rules.h - visibility logic of the builder's callbacks (registered via FUN_433c081c), translated from their
// disassembly. Element ids are the builder's creation order (E[] in menu_data.h). Pure game-logic callbacks are left out.
static bool cbv(int i) { return ((Checkbox*)E[i])->get(); }
static int cov(int i) { auto* c = (Combo*)E[i]; return c->value ? *c->value : 0; }
static bool material2(int i) { int v = cov(i); return v == 2 || v == 3 || v == 4 || v == 5; }  // Shaded/Metallic/Glow/Bubble
static void vis(int i, bool v) { E[i]->setVisible(v); }
static void on(int i, std::function<void()> f) { E[i]->calls.push_back([f](Element*) { f(); }); }
static int yawOffset[6], bodyYawOffset[4];  // tab +0xcc / +0xd0: per-mode slider values (vars fe49974d / 8dfd54bd)

// ---- per-weapon configs (rage weapon selector). The selector's value change (0x433aba07) serialises the config vars of
// tab "rage" -> groupbox "aimbot" (found by FNV-1a name hash, 0x43381b62; element types 0/1/2/7/8/11/14 skipped by
// 0x433e8d40) into the old weapon's blob (PTR 0x43472b98[type]) and loads the new weapon's blob (0x433b0879).
// Blob = config format: u32 0xdec00d60, u32 2, u32 key 0x92ca5df1, then per var {u16 size, u8 type, u8 0, u32 hash,
// value bytes} (writer 0x43417939); the reader (0x43367569) matches hash == key ^ hash ^ 0x92ca5df1, type and size,
// and stops at a type-8 entry. Copy / Paste put the blob on the clipboard as base64 (0x4333ceb7 / 0x4341a2c6).
static std::vector<uint8_t> weaponBlob[12];
static int curWeapon = 0;
static std::vector<Element*> weaponVars() {
    std::vector<Element*> r;
    for (Element* e : ((Child*)E[5])->items) if (e->cfgHash) r.push_back(e);
    return r;
}
static void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static std::vector<uint8_t> saveWeaponGroup() {
    std::vector<uint8_t> b;
    put32(b, 0xdec00d60); put32(b, 2); put32(b, 0x92ca5df1);
    for (Element* e : weaponVars()) {
        std::vector<uint8_t> v(e->cfgSize);
        if (!e->cfgRead(v.data())) continue;
        b.push_back((uint8_t)(e->cfgSize & 255)); b.push_back((uint8_t)(e->cfgSize >> 8));
        b.push_back(e->cfgType); b.push_back(0);
        put32(b, e->cfgHash);
        b.insert(b.end(), v.begin(), v.end());
    }
    return b;
}
static bool loadWeaponGroup(const std::vector<uint8_t>& b) {
    if (b.size() <= 12 || get32(&b[0]) != 0xdec00d60) return false;
    uint32_t key = get32(&b[8]);
    for (Element* e : weaponVars())
        for (size_t p = 12; p + 8 <= b.size();) {
            uint16_t sz = (uint16_t)(b[p] | b[p + 1] << 8); uint8_t ty = b[p + 2];
            if (ty == 8 || p + 8 + sz > b.size()) break;
            if (get32(&b[p + 4]) == (key ^ e->cfgHash ^ 0x92ca5df1) && ty == e->cfgType && (ty == 12 || sz == e->cfgSize)) { e->cfgWrite(&b[p + 8]); break; }
            p += 8 + sz;
        }
    for (Element* e : ((Child*)E[5])->items) {  // OnConfigLoad: dependents and callbacks follow the new values
        if (auto* c = dynamic_cast<Checkbox*>(e)) c->applyDeps();
        e->fire();
    }
    return true;
}
static std::string base64(const std::vector<uint8_t>& in) {  // 0x4333ceb7, table 0x43473200, '=' padded
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t v = in[i] << 16 | (i + 1 < in.size() ? in[i + 1] << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        o += T[v >> 18]; o += T[(v >> 12) & 63];
        o += i + 1 < in.size() ? T[(v >> 6) & 63] : '='; o += i + 2 < in.size() ? T[v & 63] : '=';
    }
    return o;
}
static std::vector<uint8_t> unbase64(const std::string& s) {  // 0x4341a2c6: stops at '=', skips anything not [A-Za-z0-9+/]
    std::vector<uint8_t> o; uint32_t acc = 0; int n = 0;
    for (char c : s) {
        if (c == '=') break;
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (v < 0) continue;
        acc = acc << 6 | v;
        if (++n == 4) { o.push_back((uint8_t)(acc >> 16)); o.push_back((uint8_t)(acc >> 8)); o.push_back((uint8_t)acc); acc = 0; n = 0; }
    }
    if (n == 2) o.push_back((uint8_t)(acc >> 4));
    else if (n == 3) { o.push_back((uint8_t)(acc >> 10)); o.push_back((uint8_t)(acc >> 2)); }
    return o;
}
static void weaponContext(int weapon, int item) {  // callback 0x433e3b88 (weapon < 12)
    if (item == 0) {  // Copy (0x4335c6c2): base64 of the weapon's blob as CF_TEXT
        // shortcut: the current weapon is serialised live (the original's blob argument is hidden by the decompiler)
        std::vector<uint8_t> b = weapon == curWeapon ? saveWeaponGroup() : weaponBlob[weapon];
        if (b.empty() || !OpenClipboard(nullptr)) return;
        std::string t = base64(b);
        EmptyClipboard();
        if (HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, t.size() + 1)) {
            memcpy(GlobalLock(hg), t.c_str(), t.size() + 1); GlobalUnlock(hg);
            if (!SetClipboardData(CF_TEXT, hg)) GlobalFree(hg);
        }
        CloseClipboard();
    } else {  // Paste (0x43394985): CF_TEXT -> base64 decode -> that weapon's blob (and the live vars if it is shown)
        if (!IsClipboardFormatAvailable(CF_TEXT) || !OpenClipboard(nullptr)) return;
        std::string t;
        if (HANDLE hd = GetClipboardData(CF_TEXT)) { if (const char* p = (const char*)GlobalLock(hd)) { t = p; GlobalUnlock(hd); } }
        CloseClipboard();
        std::vector<uint8_t> b = unbase64(t);
        if (b.size() <= 12 || get32(&b[0]) != 0xdec00d60) return;  // not a config blob: ignored
        weaponBlob[weapon] = b;
        if (weapon == curWeapon) loadWeaponGroup(b);
    }
}

static void applyRules() {
    // ---- RAGE weapon selector: per-weapon configs + Copy / Paste
    {
        auto* ws = (WeaponSelect*)E[4];
        curWeapon = ws->value ? *ws->value : 0;
        ws->calls.push_back([ws](Element*) {
            int nw = ws->value ? *ws->value : 0;
            if (nw == curWeapon || nw < 0 || nw >= 12) return;
            weaponBlob[curWeapon] = saveWeaponGroup();
            loadWeaponGroup(weaponBlob[nw]);  // never-visited weapon: empty blob, the shown values carry over (as in GS)
            curWeapon = nw;
        });
        ws->onContext = weaponContext;
    }
    std::vector<std::function<void()>> init;
    auto rule = [&](std::vector<int> src, std::function<void()> f) { for (int s : src) on(s, f); init.push_back(f); };

    // ---- RAGE
    rule({9}, [] {  // 0x433efa51: "Prefer body aim" needs Head plus another hitbox; otherwise it is cleared and hidden
        int v = cov(9); bool b = (v & 2) && (v & 0xe) != 2;
        if (!b) ((Checkbox*)E[22])->set(false);
        vis(22, b);
    });
    rule({10}, [] { vis(12, cov(10) != 0); });  // 0x43369a9b: multi-point scale

    // ---- ANTI-AIM
    rule({60}, [] { int v = cov(60); if (v <= 6) vis(61, v == 6); });  // 0x433b5760: custom pitch
    rule({63}, [] {  // 0x433b2007: yaw offset slider bound to the mode's own value
        int v = cov(63); if (v >= 6) return;
        if (v == 0) vis(64, false); else { ((Slider*)E[64])->value = &yawOffset[v]; vis(64, true); }
        vis(65, v != 0);
        vis(66, v != 0 && cov(65) != 0);
    });
    rule({65}, [] { int v = cov(65); if (v < 5) vis(66, v != 0 && cov(63) != 0); });  // 0x4332ae6e
    rule({67}, [] {  // 0x433ce908: body yaw
        int v = cov(67); if (v >= 4) return;
        vis(69, v != 0);
        if (v != 0 && v != 1) { ((Slider*)E[68])->value = &bodyYawOffset[v]; vis(68, true); } else vis(68, false);
    });

    // ---- LEGIT
    rule({125}, [] { vis(126, cov(125) != 0); });  // 0x4332927a: accuracy boost range
    rule({114}, [] {  // 0x4341f19e: triggerbot reaction time disables automatic penetration
        int v = ((Slider*)E[114])->get();
        if (v) ((Checkbox*)E[118])->set(false);
        E[118]->editable = v == 0;
    });

    // ---- VISUALS
    rule({161}, [] { vis(163, cbv(161)); vis(164, cbv(161)); });  // 0x43330832: out of FOV arrow size/distance
    auto model = [&](std::vector<int> cbs, int combo, int col, int extra) {  // 0x43407c86/0x433ed78b and siblings
        rule(cbs, [=] {
            bool any = false; for (int c : cbs) any |= cbv(c);
            vis(combo, any); vis(col, any && material2(combo)); if (extra >= 0) vis(extra, any);
        });
        on(combo, [=] { bool any = false; for (int c : cbs) any |= cbv(c); vis(col, any && material2(combo)); });
    };
    model({166, 168}, 170, 171, -1);  // Player / behind wall
    model({172, 174}, 176, 177, -1);  // Teammate / behind wall
    model({178}, 180, 181, -1);       // Local player
    model({183}, 185, 186, -1);       // Local player fake
    model({187}, 189, 190, 191);      // On shot (+ duration slider)
    model({193}, 195, 196, -1);       // Hands
    model({197}, 199, 200, -1);       // Weapon viewmodel
    model({201}, 203, 204, -1);       // Weapons
    rule({237}, [] { for (int i = 238; i <= 273; i++) vis(i, cbv(237)); });  // 0x4334d372: Danger Zone item list
    rule({282}, [] { vis(283, cov(282) == 2); });  // 0x433b6fb3: night mode colour

    // ---- MISC
    auto zhop = [] { bool b = E[341]->visible && cbv(341); vis(343, b); vis(344, b); };  // 0x43337663
    rule({337}, [=] {  // 0x433d1146: air strafe options
        bool v = cbv(337);
        for (int i : {338, 339, 341, 342, 345, 346}) vis(i, v);
        zhop();
        vis(340, v);
    });
    on(341, zhop);
    rule({366}, [] { layoutLocked = cbv(366); });  // 0x4332a6c5: lock menu layout

    // ---- PLAYERS: player-list callback 0x4338936b. With no players every element of every groupbox in the tab gets +0x14 = 0;
    // a selection re-enables the Players box (+0x9c) and the Adjustments box (+0xa0). No game here, so the list stays empty.
    for (int c : {400, 403}) for (Element* e : ((Child*)E[c])->items) e->editable = ((Listbox*)E[401])->items.size() > 0;

    for (Element* e : E) if (auto* c = dynamic_cast<Checkbox*>(e)) c->applyDeps();
    for (auto& f : init) f();
}
