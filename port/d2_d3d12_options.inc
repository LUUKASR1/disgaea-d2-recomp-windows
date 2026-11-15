/* D2_D3D12_OPTIONS_V1: F1 options panel for the DirectX 12 window.
 *
 * The DirectX 12 backend (SDK) has no 2D drawing of its own, so this panel is a separate layered pop-up window owned
 * by the game window and drawn with GDI. It works the same way as the Vulkan panel (tabs, mouse, saved settings)
 * but only offers what the DirectX 12 path can do: screen mode, window size, V-Sync (next start), FPS counter,
 * image filters (FXAA, sharpness, scanlines, CRT mask, vignette, vivid colours: the post pass of the SDK backend),
 * keyboard remapping, language and the graphics API switch. Internal resolution scaling and FSR exist only in Vulkan.
 *
 * Settings live in the same settings.ini as the Vulkan panel (fullscreen, vsync, width, height, lang, key0..key13);
 * other lines are kept as they are. Included by main.cpp, Windows only, after the shutdown helpers. */

extern "C" void ps3_pad_set_keyboard_map(const int* vk, int count);
extern "C" void d2_restart_with_api(int api);
extern "C" void d2_graphics_set_post(const float* values, int enabled); /* port/graphics/graphics.cpp */
static std::wstring d2_settings_ini_path(void);

namespace d2opt {

constexpr int kKeys = 14;
constexpr int kRows = 10;
constexpr int kMaxHits = 48;
constexpr int A_KEY0 = 200;
enum Act { A_NONE = -1, A_MODE, A_WINSIZE, A_VSYNC, A_LANG, A_API, A_RESET, A_CLOSE, A_KEYS_RESET, A_FPS, A_SHARPEN, A_SCAN, A_MASK, A_VIGNETTE, A_VIVID, A_AA, A_OUTRES, A_SCALE, A_UPSCALER, A_RESTART, A_TAB0 = 100 };
constexpr int kTabs = 4; // 0 video, 1 filters, 2 controls, 3 system

constexpr int kDefaultKeys[kKeys] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', '1', '2', VK_RETURN, VK_TAB};
constexpr int kResW[] = {960, 1280, 1600, 1920, 2560};
constexpr int kResH[] = {540, 720, 900, 1080, 1440};
constexpr int kResCount = 5;
// Output (swap chain) sizes and internal resolution percentages: they apply when the game starts (the swap chain and the
// scene textures are created once), so changing them marks a restart as pending.
constexpr int kOutW[] = {1280, 1600, 1920, 2560, 3840};
constexpr int kOutH[] = {720, 900, 1080, 1440, 2160};
constexpr int kOutCount = 5;
constexpr int kScalePct[] = {50, 75, 100, 150, 200};
constexpr int kScaleCount = 5;

struct Config {
    bool fullscreen = false, vsync = true;
    unsigned width = 1280, height = 720, lang = 0;
    bool show_fps = false;
    int out_idx = -1, scale_pct = -1, fsr = -1; // -1: not chosen in the game (the launcher's values stay)
    unsigned sharpen = 0, scanlines = 0, crt_mask = 0, vignette = 0, vivid = 0, aa = 0; // 0 off .. 3 strong (aa: 0/1)
    int keys[kKeys] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', '1', '2', VK_RETURN, VK_TAB};
};

static Config cfg;
static HWND game = nullptr, panel = nullptr, backdrop = nullptr;
static WNDPROC game_proc = nullptr;
static bool is_open = false;
static int page = 0, hover = A_NONE, listening = -1;
static ULONGLONG api_armed_until = 0;
static bool attached = false;

struct Hit { int act; RECT r; };
static Hit hits[kMaxHits];
static int nhits = 0;
static double ui_scale = 1.0;

static const char* L(const char* pt, const char* en) { return cfg.lang ? en : pt; }

static std::wstring W(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring out((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], n);
    return out;
}

/* ---- settings.ini ------------------------------------------------------------------------------------------ */

static bool is_known_key(const std::string& key)
{
    if (key == "fullscreen" || key == "vsync" || key == "width" || key == "height" || key == "lang" || key == "showfps" || key == "sharpen" ||
        key == "scanlines" || key == "crtmask" || key == "vignette" || key == "vivid" || key == "aa" || key == "d3_out" || key == "d3_scale" ||
        key == "d3_fsr")
        return true;
    if (key.rfind("key", 0) == 0 && key.size() > 3) {
        for (size_t i = 3; i < key.size(); i++)
            if (key[i] < '0' || key[i] > '9') return false;
        return true;
    }
    return false;
}

static void load_config()
{
    cfg.lang = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE) ? 0u : 1u;
    const std::wstring path = d2_settings_ini_path();
    if (!path.empty()) {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const long value = atol(line.c_str() + eq + 1);
            if (key == "fullscreen") cfg.fullscreen = value != 0;
            else if (key == "vsync") cfg.vsync = value != 0;
            else if (key == "lang" && value >= 0 && value <= 1) cfg.lang = (unsigned)value;
            else if (key == "showfps") cfg.show_fps = value != 0;
            else if (key == "d3_out" && value >= 0 && value < kOutCount) cfg.out_idx = (int)value;
            else if (key == "d3_scale" && value >= 25 && value <= 400) cfg.scale_pct = (int)value;
            else if (key == "d3_fsr" && value >= 0 && value <= 1) cfg.fsr = (int)value;
            else if (key == "sharpen" && value >= 0 && value <= 3) cfg.sharpen = (unsigned)value;
            else if (key == "scanlines" && value >= 0 && value <= 3) cfg.scanlines = (unsigned)value;
            else if (key == "crtmask" && value >= 0 && value <= 3) cfg.crt_mask = (unsigned)value;
            else if (key == "vignette" && value >= 0 && value <= 3) cfg.vignette = (unsigned)value;
            else if (key == "vivid" && value >= 0 && value <= 3) cfg.vivid = (unsigned)value;
            else if (key == "aa" && value >= 0 && value <= 1) cfg.aa = (unsigned)value;
            else if (key == "width" && value >= 640 && value <= 16384) cfg.width = (unsigned)value;
            else if (key == "height" && value >= 360 && value <= 16384) cfg.height = (unsigned)value;
            else if (key.rfind("key", 0) == 0 && key.size() > 3 && value >= 1 && value <= 254) {
                const int index = atoi(key.c_str() + 3);
                if (index >= 0 && index < kKeys) cfg.keys[index] = (int)value;
            }
        }
    }
    if (const char* vs = getenv("D2_VSYNC"); vs && *vs) cfg.vsync = vs[0] != '0';
}

static void save_config()
{
    const std::wstring path = d2_settings_ini_path();
    if (path.empty()) return;
    std::string text, line;
    {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            const size_t eq = line.find('=');
            if (line.empty() || eq == std::string::npos) continue;
            if (!is_known_key(line.substr(0, eq))) text += line + "\n";
        }
    }
    text += "fullscreen=" + std::to_string(cfg.fullscreen ? 1 : 0) + "\n";
    text += "vsync=" + std::to_string(cfg.vsync ? 1 : 0) + "\n";
    text += "width=" + std::to_string(cfg.width) + "\n";
    text += "height=" + std::to_string(cfg.height) + "\n";
    text += "lang=" + std::to_string(cfg.lang) + "\n";
    text += "showfps=" + std::to_string(cfg.show_fps ? 1 : 0) + "\n";
    if (cfg.out_idx >= 0) text += "d3_out=" + std::to_string(cfg.out_idx) + "\n";
    if (cfg.scale_pct >= 0) text += "d3_scale=" + std::to_string(cfg.scale_pct) + "\n";
    if (cfg.fsr >= 0) text += "d3_fsr=" + std::to_string(cfg.fsr) + "\n";
    text += "sharpen=" + std::to_string(cfg.sharpen) + "\nscanlines=" + std::to_string(cfg.scanlines) + "\ncrtmask=" + std::to_string(cfg.crt_mask) +
            "\nvignette=" + std::to_string(cfg.vignette) + "\nvivid=" + std::to_string(cfg.vivid) + "\naa=" + std::to_string(cfg.aa) + "\n";
    for (int i = 0; i < kKeys; i++) text += "key" + std::to_string(i) + "=" + std::to_string(cfg.keys[i]) + "\n";
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    out << text;
}

/* ---- resolution / upscaler (applied at start) --------------------------------------------------------------------- */

static unsigned env_number(const char* name, unsigned fallback)
{
    const char* e = getenv(name);
    return (e && *e) ? (unsigned)atoi(e) : fallback;
}
static unsigned effective_out_w() { return cfg.out_idx >= 0 ? (unsigned)kOutW[cfg.out_idx] : env_number("D2_OUTPUT_WIDTH", 1280); }
static unsigned effective_out_h() { return cfg.out_idx >= 0 ? (unsigned)kOutH[cfg.out_idx] : env_number("D2_OUTPUT_HEIGHT", 720); }
static bool effective_fsr()
{
    if (cfg.fsr >= 0) return cfg.fsr != 0;
    const char* e = getenv("D2_UPSCALER");
    return e && strcmp(e, "fsr1") == 0;
}
static int effective_pct()
{
    if (cfg.scale_pct >= 0) return cfg.scale_pct;
    const unsigned rh = env_number("D2_RENDER_HEIGHT", 0);
    return rh ? (int)(rh * 100u / effective_out_h()) : 100;
}
static unsigned render_dim(unsigned out, int pct)
{
    unsigned v = (unsigned)((double)out * pct / 100.0 + 0.5);
    v &= ~1u;
    return v < 320 ? 320 : v > 4096 ? 4096 : v;
}
static bool restart_pending = false;
static ULONGLONG restart_armed_until = 0;

/* Called before the DirectX 12 backend starts: turns the choices made in the panel into the environment variables the
 * renderer reads (output size, internal resolution, upscaler), and switches the image filters on. */
static void prepare()
{
    load_config();
    if (cfg.out_idx >= 0 || cfg.scale_pct >= 0 || cfg.fsr >= 0) {
        const unsigned ow = effective_out_w(), oh = effective_out_h();
        const bool fsr = effective_fsr();
        int pct = effective_pct();
        if (fsr && pct > 100) pct = 100; // FSR 1 needs the render size to be at most the output size
        _putenv_s("D2_OUTPUT_WIDTH", std::to_string(ow).c_str());
        _putenv_s("D2_OUTPUT_HEIGHT", std::to_string(oh).c_str());
        _putenv_s("D2_RENDER_WIDTH", std::to_string(render_dim(ow, pct)).c_str());
        _putenv_s("D2_RENDER_HEIGHT", std::to_string(render_dim(oh, pct)).c_str());
        _putenv_s("D2_UPSCALER", fsr ? "fsr1" : "native");
        fprintf(stderr, "[d3d12][ui] start options: output %ux%u, internal %d%%, upscaler %s\n", ow, oh, pct, fsr ? "FSR1" : "native");
    }
    if (!getenv("D2_POST_FILTERS")) _putenv_s("D2_POST_FILTERS", "1");
}

/* ---- window handling --------------------------------------------------------------------------------------- */

static void apply_keymap() { ps3_pad_set_keyboard_map(cfg.keys, kKeys); }

// Image filter strengths for the post pass of the SDK backend (same table as the Vulkan panel).
static void apply_post()
{
    static const float sharpen[4] = {0.f, 0.3f, 0.6f, 1.0f}, scan[4] = {0.f, 0.2f, 0.4f, 0.6f}, mask[4] = {0.f, 0.25f, 0.5f, 0.75f},
                       vig[4] = {0.f, 0.3f, 0.55f, 0.8f}, sat[4] = {1.f, 1.15f, 1.3f, 1.5f}, con[4] = {1.f, 1.05f, 1.1f, 1.15f};
    const float params[8] = {sharpen[cfg.sharpen], scan[cfg.scanlines], mask[cfg.crt_mask], vig[cfg.vignette], sat[cfg.vivid], con[cfg.vivid],
                             cfg.aa ? 1.f : 0.f, 0.f};
    const bool active = cfg.sharpen || cfg.scanlines || cfg.crt_mask || cfg.vignette || cfg.vivid || cfg.aa;
    d2_graphics_set_post(params, active ? 1 : 0);
}

// ---- FPS counter: a small click-through window in the top-left corner of the game window -----------------------
static HWND fps_wnd = nullptr;
static unsigned fps_value = 0, fps_last_flips = 0;
static ULONGLONG fps_last_tick = 0;

static LRESULT CALLBACK fps_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_TIMER: {
        const ULONGLONG now = GetTickCount64();
        const unsigned flips = cellGcm_flip_request_count();
        if (fps_last_tick && now > fps_last_tick) fps_value = (unsigned)((flips - fps_last_flips) * 1000ull / (now - fps_last_tick) + 0 );
        fps_last_tick = now;
        fps_last_flips = flips;
        if (game && cfg.show_fps && IsWindowVisible(game) && !IsIconic(game)) {
            POINT o = {0, 0};
            ClientToScreen(game, &o);
            SetWindowPos(h, HWND_TOP, o.x + 10, o.y + 8, 150, 38, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            InvalidateRect(h, nullptr, FALSE);
        } else {
            ShowWindow(h, SW_HIDE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(dc, &r, key);
        DeleteObject(key);
        HFONT font = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        wchar_t text[32];
        swprintf(text, sizeof text / sizeof text[0], L"%u FPS", fps_value);
        SetTextColor(dc, RGB(8, 3, 24));
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx || dy) TextOutW(dc, 4 + dx, 2 + dy, text, (int)wcslen(text));
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 4, 2, text, (int)wcslen(text));
        SelectObject(dc, old);
        DeleteObject(font);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void ensure_fps_window()
{
    if (fps_wnd || !game) return;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = fps_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"d2_fps_counter";
    RegisterClassExW(&wc);
    fps_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"d2_fps_counter", L"", WS_POPUP, 0, 0, 150, 38,
                              game, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!fps_wnd) return;
    SetLayeredWindowAttributes(fps_wnd, RGB(255, 0, 255), 0, LWA_COLORKEY);
    SetTimer(fps_wnd, 1, 500, nullptr);
}

static void apply_window_size(unsigned w, unsigned h)
{
    cfg.width = w;
    cfg.height = h;
    if (!game || cfg.fullscreen) return;
    RECT r = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&r, (DWORD)GetWindowLongPtrA(game, GWL_STYLE), FALSE);
    SetWindowPos(game, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK backdrop_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    if (m == WM_SETCURSOR) { SetCursor(nullptr); return TRUE; }
    return DefWindowProcW(h, m, w, l);
}

/* Borderless fullscreen. The game image keeps its 16:9 shape: when the monitor is wider, the game window is centred
 * and a black window behind it (its owner) fills the rest of the monitor. */
static void set_fullscreen(bool on)
{
    static WINDOWPLACEMENT previous = {sizeof(WINDOWPLACEMENT)};
    static LONG_PTR previous_style = 0;
    if (!game || on == cfg.fullscreen) return;
    if (on) {
        MONITORINFO mi = {sizeof(mi)};
        if (!GetWindowPlacement(game, &previous) || !GetMonitorInfoA(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi)) return;
        previous_style = GetWindowLongPtrA(game, GWL_STYLE);
        const int mx = mi.rcMonitor.left, my = mi.rcMonitor.top;
        const int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        int gw = mw, gh = mh;
        if (mw * 9 > mh * 16) gw = mh * 16 / 9; /* wider than 16:9 */
        else if (mw * 9 < mh * 16) gh = mw * 9 / 16; /* taller than 16:9 */
        const bool bars = gw != mw || gh != mh;
        if (bars) {
            if (!backdrop) {
                WNDCLASSEXW wc = {sizeof(wc)};
                wc.lpfnWndProc = backdrop_proc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.lpszClassName = L"d2_letterbox";
                RegisterClassExW(&wc);
                backdrop = CreateWindowExW(WS_EX_TOOLWINDOW, L"d2_letterbox", L"", WS_POPUP, mx, my, mw, mh, nullptr, nullptr,
                                           GetModuleHandleW(nullptr), nullptr);
            }
            if (backdrop) {
                SetWindowPos(backdrop, nullptr, mx, my, mw, mh, SWP_NOZORDER | SWP_NOACTIVATE);
                ShowWindow(backdrop, SW_SHOWNOACTIVATE);
                SetWindowLongPtrA(game, GWLP_HWNDPARENT, (LONG_PTR)backdrop); /* owned: always above the black window */
            }
        }
        SetWindowLongPtrA(game, GWL_STYLE, (previous_style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
        SetWindowPos(game, HWND_TOP, mx + (mw - gw) / 2, my + (mh - gh) / 2, gw, gh, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrA(game, GWL_STYLE, previous_style ? previous_style : (LONG_PTR)(WS_OVERLAPPEDWINDOW | WS_VISIBLE));
        SetWindowLongPtrA(game, GWLP_HWNDPARENT, 0);
        if (backdrop) ShowWindow(backdrop, SW_HIDE);
        SetWindowPlacement(game, &previous);
        SetWindowPos(game, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    cfg.fullscreen = on;
    SetForegroundWindow(game);
}

/* ---- key names --------------------------------------------------------------------------------------------- */

static std::string key_name(int vk)
{
    switch (vk) {
    case VK_UP: return L("Seta cima", "Up");
    case VK_DOWN: return L("Seta baixo", "Down");
    case VK_LEFT: return L("Seta esq.", "Left");
    case VK_RIGHT: return L("Seta dir.", "Right");
    case VK_RETURN: return "Enter";
    case VK_TAB: return "Tab";
    case VK_SPACE: return L("Espaço", "Space");
    case VK_BACK: return "Backspace";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_CAPITAL: return "Caps";
    case VK_DELETE: return "Del";
    case VK_INSERT: return "Ins";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PgUp";
    case VK_NEXT: return "PgDn";
    default: break;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, (char)vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num" + std::string(1, (char)('0' + (vk - VK_NUMPAD0)));
    if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
    char name[64] = {};
    if (GetKeyNameTextA((LONG)(MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC) << 16), name, (int)sizeof name) > 0) return name;
    return "VK" + std::to_string(vk);
}

static bool key_bindable(int vk)
{
    return vk > 0 && vk < 255 && vk != VK_F1 && vk != VK_F11 && vk != VK_ESCAPE && vk != VK_LWIN && vk != VK_RWIN && vk != VK_APPS;
}

static void assign_key(int index, int vk)
{
    if (index < 0 || index >= kKeys) return;
    for (int i = 0; i < kKeys; i++)
        if (i != index && cfg.keys[i] == vk) cfg.keys[i] = cfg.keys[index];
    cfg.keys[index] = vk;
    fprintf(stderr, "[d3d12][keys] action %d -> VK %d\n", index, vk);
    apply_keymap();
}

/* ---- actions ----------------------------------------------------------------------------------------------- */

static void panel_redraw() { if (panel) InvalidateRect(panel, nullptr, FALSE); }
static bool api_armed() { return GetTickCount64() < api_armed_until; }

static void activate(int act, int dir)
{
    if (act >= A_KEY0 && act < A_KEY0 + kKeys) {
        if (dir == 1) listening = act - A_KEY0;
        else listening = -1;
        panel_redraw();
        return;
    }
    listening = -1;
    if (act != A_API) api_armed_until = 0;
    if (act != A_RESTART) restart_armed_until = 0;
    if (act >= A_TAB0 && act < A_TAB0 + kTabs) { page = act - A_TAB0; panel_redraw(); return; }
    switch (act) {
    case A_MODE: set_fullscreen(!cfg.fullscreen); break;
    case A_WINSIZE: {
        if (cfg.fullscreen) return;
        int cur = 0;
        for (int i = 0; i < kResCount; i++)
            if ((unsigned)kResH[i] <= cfg.height) cur = i;
        cur = (cur + dir + kResCount) % kResCount;
        apply_window_size((unsigned)kResW[cur], (unsigned)kResH[cur]);
        break;
    }
    case A_VSYNC: cfg.vsync = !cfg.vsync; restart_pending = true; break;
    case A_OUTRES: {
        int cur = 1;
        for (int i = 0; i < kOutCount; i++)
            if ((unsigned)kOutH[i] <= effective_out_h()) cur = i;
        cfg.out_idx = (cur + dir + kOutCount) % kOutCount;
        restart_pending = true;
        break;
    }
    case A_SCALE: {
        int cur = 2;
        for (int i = 0; i < kScaleCount; i++)
            if (kScalePct[i] <= effective_pct()) cur = i;
        int next = (cur + dir + kScaleCount) % kScaleCount;
        if (effective_fsr() && kScalePct[next] > 100) next = dir > 0 ? 0 : 2; // FSR 1 cannot render above the output size
        cfg.scale_pct = kScalePct[next];
        restart_pending = true;
        break;
    }
    case A_UPSCALER:
        cfg.fsr = effective_fsr() ? 0 : 1;
        if (cfg.fsr && effective_pct() > 100) cfg.scale_pct = 100;
        restart_pending = true;
        break;
    case A_RESTART:
        if (dir != 1) { restart_armed_until = 0; break; }
        if (GetTickCount64() >= restart_armed_until) { restart_armed_until = GetTickCount64() + 6000; panel_redraw(); return; }
        restart_armed_until = 0;
        save_config();
        fprintf(stderr, "[d3d12] restart requested to apply the new start options\n");
        d2_restart_with_api(-1);
        break;
    case A_FPS:
        cfg.show_fps = !cfg.show_fps;
        ensure_fps_window();
        if (!cfg.show_fps && fps_wnd) ShowWindow(fps_wnd, SW_HIDE);
        break;
    case A_SHARPEN: cfg.sharpen = (cfg.sharpen + 4 + (unsigned)dir) % 4; apply_post(); break;
    case A_SCAN: cfg.scanlines = (cfg.scanlines + 4 + (unsigned)dir) % 4; apply_post(); break;
    case A_MASK: cfg.crt_mask = (cfg.crt_mask + 4 + (unsigned)dir) % 4; apply_post(); break;
    case A_VIGNETTE: cfg.vignette = (cfg.vignette + 4 + (unsigned)dir) % 4; apply_post(); break;
    case A_VIVID: cfg.vivid = (cfg.vivid + 4 + (unsigned)dir) % 4; apply_post(); break;
    case A_AA: cfg.aa = cfg.aa ? 0u : 1u; apply_post(); break;
    case A_LANG: cfg.lang = cfg.lang ? 0u : 1u; break;
    case A_API:
#ifdef D2_VULKAN_GAME_BACKEND
        if (dir != 1) { api_armed_until = 0; break; }
        if (!api_armed()) { api_armed_until = GetTickCount64() + 6000; panel_redraw(); return; }
        api_armed_until = 0;
        save_config();
        fprintf(stderr, "[d3d12] graphics API switch requested: restarting with Vulkan\n");
        d2_restart_with_api(1);
#endif
        break;
    case A_RESET:
        if (page == 1) {
            cfg.sharpen = cfg.scanlines = cfg.crt_mask = cfg.vignette = cfg.vivid = cfg.aa = 0;
            apply_post();
        } else {
            cfg.vsync = true;
            cfg.show_fps = false;
            cfg.out_idx = cfg.scale_pct = cfg.fsr = -1;
            restart_pending = true;
            if (fps_wnd) ShowWindow(fps_wnd, SW_HIDE);
            if (cfg.fullscreen) set_fullscreen(false);
            apply_window_size(1280, 720);
        }
        break;
    case A_KEYS_RESET:
        for (int i = 0; i < kKeys; i++) cfg.keys[i] = kDefaultKeys[i];
        apply_keymap();
        break;
    case A_CLOSE: break; /* handled by the caller */
    default: return;
    }
    save_config();
    panel_redraw();
}

/* ---- drawing ----------------------------------------------------------------------------------------------- */

static COLORREF rgb(int r, int g, int b) { return RGB(r, g, b); }
static COLORREF mix(COLORREF a, COLORREF b, double t)
{
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static void gradient(HDC dc, int x, int y, int w, int h, COLORREF top, COLORREF bottom)
{
    if (w <= 0 || h <= 0) return;
    const int steps = h < 48 ? h : 48;
    for (int i = 0; i < steps; i++) {
        const int y0 = y + h * i / steps, y1 = y + h * (i + 1) / steps;
        HBRUSH brush = CreateSolidBrush(mix(top, bottom, steps > 1 ? (double)i / (steps - 1) : 0.0));
        RECT r = {x, y0, x + w, y1};
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    }
}

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    HBRUSH brush = CreateSolidBrush(c);
    RECT r = {x, y, x + w, y + h};
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void text_outline(HDC dc, int x, int y, const std::string& utf8, COLORREF color, COLORREF ink, int o)
{
    const std::wstring t = W(utf8);
    if (t.empty()) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    for (int dy = -o; dy <= o; dy++)
        for (int dx = -o; dx <= o; dx++)
            if (dx || dy) TextOutW(dc, x + dx, y + dy, t.c_str(), (int)t.size());
    SetTextColor(dc, color);
    TextOutW(dc, x, y, t.c_str(), (int)t.size());
}

static int text_width(HDC dc, const std::string& utf8)
{
    const std::wstring t = W(utf8);
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &size);
    return size.cx;
}

static HFONT make_font(int px) { return CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"); }

struct Row { int act; std::string label, value; bool enabled, button; };

static void layout_rows(Row* rows, std::string& hint)
{
    const std::string on_off[2] = {std::string("« ") + L("Desligado", "Off") + " »", std::string("« ") + L("Ligado", "On") + " »"};
    char res[48];
    snprintf(res, sizeof res, "« %u × %u »", cfg.width, cfg.height);
    for (int i = 0; i < kRows; i++) rows[i] = Row{A_NONE, "", "", false, false};
    if (page == 0) {
        rows[0] = Row{A_MODE, L("Modo de tela", "Display mode"), cfg.fullscreen ? std::string("« ") + L("Tela cheia", "Fullscreen") + " »" : std::string("« ") + L("Janela", "Windowed") + " »", true, false};
        rows[1] = Row{A_WINSIZE, L("Tamanho da janela", "Window size"), cfg.fullscreen ? L("só em modo janela", "windowed mode only") : res, !cfg.fullscreen, false};
        char out_text[48], scale_text[64];
        snprintf(out_text, sizeof out_text, "« %u × %u »", effective_out_w(), effective_out_h());
        snprintf(scale_text, sizeof scale_text, "« %d%%  (%u × %u) »", effective_pct(), render_dim(effective_out_w(), effective_pct()),
                 render_dim(effective_out_h(), effective_pct()));
        rows[2] = Row{A_OUTRES, L("Resolução de saída", "Output resolution"), out_text, true, false};
        rows[3] = Row{A_SCALE, L("Resolução interna", "Internal resolution"), scale_text, true, false};
        rows[4] = Row{A_UPSCALER, "Upscaler", effective_fsr() ? "« AMD FSR 1.0 »" : "« Bilinear »", true, false};
        rows[5] = Row{A_VSYNC, "V-Sync", on_off[cfg.vsync ? 1 : 0], true, false};
        rows[6] = Row{A_FPS, L("Mostrar FPS", "Show FPS"), on_off[cfg.show_fps ? 1 : 0], true, false};
        if (restart_pending)
            rows[7] = Row{A_RESTART, GetTickCount64() < restart_armed_until ? L("Clique de novo: reiniciar o jogo", "Click again: restart the game") : L("Reiniciar agora para aplicar", "Restart now to apply"), "", true, true};
        rows[8] = Row{A_RESET, L("Restaurar padrões", "Restore defaults"), "", true, true};
        rows[9] = Row{A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true};
        hint = L("Resolução de saída, resolução interna, upscaler e V-Sync valem depois de reiniciar o jogo (botão acima). O FSR 1.0 funciona com resolução interna até 100%.",
                 "Output resolution, internal resolution, upscaler and V-Sync apply after restarting the game (button above). FSR 1.0 works with an internal resolution up to 100%.");
    } else if (page == 1) {
        const std::string levels[4] = {std::string("« ") + L("Desligado", "Off") + " »", std::string("« ") + L("Leve", "Low") + " »",
                                       std::string("« ") + L("Médio", "Medium") + " »", std::string("« ") + L("Forte", "Strong") + " »"};
        rows[0] = Row{A_SHARPEN, L("Nitidez", "Sharpness"), levels[cfg.sharpen], true, false};
        rows[1] = Row{A_SCAN, "Scanlines", levels[cfg.scanlines], true, false};
        rows[2] = Row{A_MASK, L("Máscara CRT", "CRT mask"), levels[cfg.crt_mask], true, false};
        rows[3] = Row{A_VIGNETTE, L("Vinheta", "Vignette"), levels[cfg.vignette], true, false};
        rows[4] = Row{A_VIVID, L("Cores vivas", "Vivid colors"), levels[cfg.vivid], true, false};
        rows[5] = Row{A_AA, L("Suavização (FXAA)", "Anti-aliasing (FXAA)"), on_off[cfg.aa ? 1 : 0], true, false};
        rows[8] = Row{A_RESET, L("Restaurar padrões", "Restore defaults"), "", true, true};
        rows[9] = Row{A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true};
        hint = L("Os filtros valem para a imagem inteira, na resolução do jogo. \"Desligado\" volta ao original.",
                 "Filters apply to the whole image at the game's resolution. \"Off\" restores the original.");
    } else if (page == 2) {
        rows[8] = Row{A_KEYS_RESET, L("Restaurar teclas", "Restore keys"), "", true, true};
        rows[9] = Row{A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true};
        hint = L("Clique em uma ação e aperte a nova tecla (Esc cancela). Vale quando não há controle conectado ou junto com ele.",
                 "Click an action and press the new key (Esc cancels). Applies with no controller connected or together with one.");
    } else {
        rows[0] = Row{A_LANG, L("Idioma", "Language"), cfg.lang ? "« English »" : "« Português »", true, false};
#ifdef D2_VULKAN_GAME_BACKEND
        rows[1] = Row{A_API, L("API gráfica", "Graphics API"), api_armed() ? std::string("« ") + L("Clique: reiniciar em Vulkan", "Click: restart in Vulkan") + " »" : "« DirectX 12 »", true, false};
#else
        rows[1] = Row{A_API, L("API gráfica", "Graphics API"), std::string("« DirectX 12 (") + L("sem Vulkan nesta versão", "no Vulkan in this build") + ") »", false, false};
#endif
        rows[9] = Row{A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true};
        hint = L("Trocar a API reinicia o jogo (o progresso não salvo é perdido). O Vulkan (experimental) é outro renderizador, com os mesmos filtros.",
                 "Switching the API restarts the game (unsaved progress is lost). Vulkan (experimental) is a different renderer with the same filters.");
    }
}

static void paint(HDC dc, int W_, int H_)
{
    const double s = ui_scale;
    const COLORREF pink = rgb(255, 89, 166), gold = rgb(255, 214, 77), white = rgb(255, 255, 255), ink = rgb(8, 3, 24), soft = rgb(232, 219, 255);
    const int head_h = (int)(70 * s), tab_h = (int)(42 * s), rowh = (int)(44 * s), foot_h = H_ - head_h - tab_h - rowh * kRows;
    HFONT fnt = make_font((int)(25 * s)), fsmall = make_font((int)(19 * s)), fbig = make_font((int)(40 * s)), ftab = make_font((int)(23 * s));
    nhits = 0;
    auto add_hit = [&](int act, int x, int y, int w, int h) {
        if (nhits < kMaxHits) hits[nhits++] = Hit{act, RECT{x, y, x + w, y + h}};
    };

    gradient(dc, 0, 0, W_, H_, rgb(56, 23, 92), rgb(15, 10, 41));
    fill(dc, 0, 0, W_, (int)(3 * s) + 1, gold);
    // frame: magenta outer, gold inner
    fill(dc, 0, 0, W_, (int)(4 * s), pink);
    fill(dc, 0, H_ - (int)(4 * s), W_, (int)(4 * s), pink);
    fill(dc, 0, 0, (int)(4 * s), H_, pink);
    fill(dc, W_ - (int)(4 * s), 0, (int)(4 * s), H_, pink);

    // title banner
    gradient(dc, (int)(4 * s), (int)(4 * s), W_ - (int)(8 * s), head_h - (int)(4 * s), rgb(199, 41, 115), rgb(92, 20, 102));
    fill(dc, (int)(4 * s), head_h, W_ - (int)(8 * s), (int)(3 * s), gold);
    SelectObject(dc, fbig);
    const std::string title = L("OPÇÕES", "OPTIONS");
    text_outline(dc, (W_ - text_width(dc, title)) / 2, (int)((head_h - 40 * s * 1.3) / 2), title, white, ink, (int)(2 * s) + 1);

    // tabs
    const char* tab_names[kTabs] = {L("VÍDEO", "VIDEO"), L("FILTROS", "FILTERS"), L("CONTROLES", "CONTROLS"), L("SISTEMA", "SYSTEM")};
    const int gap = (int)(10 * s), tab_w = (W_ - (int)(48 * s) - (kTabs - 1) * gap) / kTabs;
    SelectObject(dc, ftab);
    for (int t = 0; t < kTabs; t++) {
        const int tx = (int)(24 * s) + t * (tab_w + gap), ty = head_h + (int)(8 * s), th = tab_h - (int)(8 * s);
        const bool active = page == t, hot = hover == A_TAB0 + t;
        add_hit(A_TAB0 + t, tx, ty, tab_w, th);
        if (active) gradient(dc, tx, ty, tab_w, th, rgb(255, 89, 166), rgb(158, 31, 128));
        else gradient(dc, tx, ty, tab_w, th, hot ? rgb(89, 46, 128) : rgb(41, 20, 66), rgb(26, 13, 46));
        if (active) fill(dc, tx, ty + th - (int)(3 * s), tab_w, (int)(3 * s), gold);
        const std::string name = tab_names[t];
        text_outline(dc, tx + (tab_w - text_width(dc, name)) / 2, ty + (int)(6 * s), name, active ? white : soft, ink, (int)(1 * s) + 1);
    }

    Row rows[kRows];
    std::string hint;
    layout_rows(rows, hint);
    const int rows_y = head_h + tab_h;
    SelectObject(dc, fnt);
    if (page == 2) {
        const char* names[kKeys] = {L("Cima", "Up"), L("Baixo", "Down"), L("Esquerda", "Left"), L("Direita", "Right"), L("Cruz", "Cross"), L("Círculo", "Circle"),
                                    L("Quadrado", "Square"), L("Triângulo", "Triangle"), "L1", "R1", "L2", "R2", "Start", "Select"};
        const int col_w = (W_ - (int)(60 * s)) / 2;
        for (int i = 0; i < kKeys; i++) {
            const int col = i / 7, row = i % 7;
            const int cx = (int)(22 * s) + col * (col_w + (int)(16 * s)), cy = rows_y + rowh * row;
            add_hit(A_KEY0 + i, cx, cy, col_w, rowh);
            const bool wait = listening == i, hot = hover == A_KEY0 + i;
            gradient(dc, cx, cy + (int)(3 * s), col_w, rowh - (int)(6 * s), wait ? rgb(255, 140, 51) : hot ? rgb(255, 89, 166) : rgb(46, 23, 77),
                     wait ? rgb(191, 64, 26) : hot ? rgb(158, 31, 128) : rgb(26, 13, 51));
            if (wait || hot) fill(dc, cx, cy + (int)(3 * s), (int)(5 * s), rowh - (int)(6 * s), gold);
            const int ty = cy + (int)(6 * s);
            text_outline(dc, cx + (int)(16 * s), ty, names[i], white, ink, (int)(1 * s) + 1);
            const std::string key = wait ? std::string(L("Pressione…", "Press a key…")) : key_name(cfg.keys[i]);
            text_outline(dc, cx + col_w - (int)(16 * s) - text_width(dc, key), ty, key, gold, ink, (int)(1 * s) + 1);
        }
    }
    for (int i = 0; i < kRows; i++) {
        if (page == 2 && i < 8) continue;
        const Row& row = rows[i];
        const int ry = rows_y + rowh * i;
        if (row.act != A_NONE) add_hit(row.act, 0, ry, W_, rowh);
        if (row.act == A_NONE) continue;
        const bool hot = row.act == hover && row.enabled;
        if (i > 0 && !row.button) fill(dc, (int)(30 * s), ry, W_ - (int)(60 * s), 1, rgb(80, 66, 40));
        if (hot) {
            gradient(dc, (int)(14 * s), ry + (int)(3 * s), W_ - (int)(28 * s), rowh - (int)(6 * s), rgb(255, 89, 166), rgb(158, 31, 128));
            fill(dc, (int)(14 * s), ry + (int)(3 * s), (int)(5 * s), rowh - (int)(6 * s), gold);
        }
        const COLORREF label_color = row.enabled ? white : rgb(179, 168, 209);
        const int ty = ry + (int)(6 * s);
        if (row.button) {
            const int bw = text_width(dc, row.label);
            text_outline(dc, (W_ - bw) / 2, ty, row.label, row.enabled ? (hot ? white : gold) : label_color, ink, (int)(1 * s) + 1);
        } else {
            text_outline(dc, (int)(36 * s), ty, row.label, label_color, ink, (int)(1 * s) + 1);
            if (!row.value.empty())
                text_outline(dc, W_ - (int)(36 * s) - text_width(dc, row.value), ty, row.value, row.enabled ? (hot ? white : gold) : rgb(179, 168, 209), ink, (int)(1 * s) + 1);
        }
    }

    // footer: hint (word-wrapped) and the closing line
    const int fy = rows_y + rowh * kRows + (int)(6 * s);
    fill(dc, (int)(30 * s), fy, W_ - (int)(60 * s), (int)(2 * s) + 1, rgb(255, 71, 148));
    SelectObject(dc, fsmall);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, soft);
    std::wstring text = W(hint + "\n" + std::string(L("F1 ou Esc fecha · as opções são salvas sozinhas.", "F1 or Esc closes · options are saved automatically.")));
    RECT tr = {(int)(35 * s), fy + (int)(10 * s), W_ - (int)(35 * s), H_ - (int)(4 * s)};
    DrawTextW(dc, text.c_str(), (int)text.size(), &tr, DT_WORDBREAK | DT_NOPREFIX);
    (void)foot_h;
    DeleteObject(fnt);
    DeleteObject(fsmall);
    DeleteObject(fbig);
    DeleteObject(ftab);
}

/* ---- panel window ------------------------------------------------------------------------------------------ */

static void close_panel();

static int hit_test(int x, int y)
{
    for (int i = 0; i < nhits; i++)
        if (x >= hits[i].r.left && x < hits[i].r.right && y >= hits[i].r.top && y < hits[i].r.bottom) return hits[i].act;
    return A_NONE;
}

static LRESULT CALLBACK panel_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        paint(mem, cr.right, cr.bottom);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int hit = hit_test((short)LOWORD(l), (short)HIWORD(l));
        if (hit != hover) { hover = hit; panel_redraw(); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int hit = hit_test((short)LOWORD(l), (short)HIWORD(l));
        if (hit == A_CLOSE) close_panel();
        else if (hit != A_NONE) activate(hit, 1);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        const int hit = hit_test((short)LOWORD(l), (short)HIWORD(l));
        if (hit != A_NONE && hit != A_CLOSE) activate(hit, -1);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p = {(short)LOWORD(l), (short)HIWORD(l)};
        ScreenToClient(h, &p);
        const int hit = hit_test(p.x, p.y);
        if (hit != A_NONE && hit != A_CLOSE && hit != A_API && hit != A_RESTART && hit < A_KEY0 && !(hit >= A_TAB0 && hit < A_TAB0 + kTabs))
            activate(hit, (short)HIWORD(w) > 0 ? -1 : 1);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (listening >= 0) {
            if (w == VK_ESCAPE) listening = -1;
            else if (key_bindable((int)w)) { assign_key(listening, (int)w); listening = -1; save_config(); }
            panel_redraw();
            return 0;
        }
        if (w == VK_F1 || w == VK_ESCAPE) { close_panel(); return 0; }
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    }
    return DefWindowProcW(h, m, w, l);
}

static void open_panel()
{
    if (!game) return;
    if (!panel) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = panel_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"d2_options_panel";
        RegisterClassExW(&wc);
        panel = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"d2_options_panel", L"Disgaea D2", WS_POPUP, 0, 0, 100, 100, game, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
        if (!panel) return;
        SetLayeredWindowAttributes(panel, 0, 245, LWA_ALPHA);
    }
    RECT client;
    GetClientRect(game, &client);
    ui_scale = client.bottom / 720.0;
    if (ui_scale < 0.8) ui_scale = 0.8;
    if (ui_scale > 2.0) ui_scale = 2.0;
    int pw = (int)(720 * ui_scale), ph = (int)((70 + 42 + 44 * kRows + 120) * ui_scale);
    if (pw > client.right - 20) pw = client.right - 20;
    if (ph > client.bottom - 10) ph = client.bottom - 10;
    POINT origin = {0, 0};
    ClientToScreen(game, &origin);
    SetWindowPos(panel, HWND_TOP, origin.x + (client.right - pw) / 2, origin.y + (client.bottom - ph) / 2, pw, ph, SWP_NOACTIVATE);
    page = 0;
    hover = A_NONE;
    listening = -1;
    is_open = true;
    ShowWindow(panel, SW_SHOW);
    SetForegroundWindow(panel);
    SetFocus(panel);
    panel_redraw();
}

static void close_panel()
{
    if (!is_open) return;
    is_open = false;
    listening = -1;
    api_armed_until = 0;
    if (panel) ShowWindow(panel, SW_HIDE);
    if (game) SetForegroundWindow(game);
}

/* ---- game window hook -------------------------------------------------------------------------------------- */

static LRESULT CALLBACK game_hook(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_F1) { if (is_open) close_panel(); else open_panel(); return 0; }
        if (w == VK_F11) { set_fullscreen(!cfg.fullscreen); save_config(); return 0; }
        if (w == VK_ESCAPE) return 0; /* the SDK window would close the game: Alt+F4 / the window button do that */
        break;
    case WM_SYSKEYDOWN:
        if (w == VK_RETURN && (l & (1 << 29))) { set_fullscreen(!cfg.fullscreen); save_config(); return 0; }
        break;
    case WM_SETCURSOR:
        if (cfg.fullscreen && LOWORD(l) == HTCLIENT && !is_open) { SetCursor(nullptr); return TRUE; }
        break;
    }
    return CallWindowProcA(game_proc, h, m, w, l);
}

static BOOL CALLBACK find_game_window(HWND h, LPARAM out)
{
    char name[64] = {};
    GetClassNameA(h, name, (int)sizeof name);
    if (strcmp(name, "ps3recomp_d3d12") != 0) return TRUE;
    *(HWND*)out = h;
    return FALSE;
}

/* Called on the render thread, right after the DirectX 12 window exists. */
static void attach()
{
    if (attached) return;
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), find_game_window, (LPARAM)&found);
    if (!found) { fprintf(stderr, "[d3d12][ui] options panel unavailable: window not found\n"); return; }
    game = found;
    load_config();
    apply_keymap();
    apply_post();
    if (cfg.show_fps) ensure_fps_window();
    if (!getenv("D2_PRESENT_INTERVAL")) _putenv_s("D2_PRESENT_INTERVAL", cfg.vsync ? "1" : "0");
    game_proc = (WNDPROC)SetWindowLongPtrA(game, GWLP_WNDPROC, (LONG_PTR)game_hook);
    if (!game_proc) { fprintf(stderr, "[d3d12][ui] options panel unavailable: could not hook the window\n"); return; }
    attached = true;
    const bool fullscreen = cfg.fullscreen;
    cfg.fullscreen = false;
    if (const char* ow = getenv("D2_OUTPUT_WIDTH"); !(ow && *ow)) apply_window_size(cfg.width, cfg.height);
    if (fullscreen) set_fullscreen(true);
    fprintf(stderr, "[d3d12][ui] options panel ready (F1), keyboard map applied, vsync=%d\n", cfg.vsync ? 1 : 0);
}

} // namespace d2opt
