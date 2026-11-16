/* Native Win32 cheats window: the Windows counterpart of d2_cheats_ui.m.
 *
 * Runs on its own thread with its own message loop and only talks to the cheat
 * engine through d2_cheats.h (queue actions, read published snapshots), so it
 * never touches guest memory. F1 in the game window (or this window) toggles it,
 * matching the macOS build. Text from the game is UTF-8 (Japanese names on JP). */
#ifdef _WIN32
#include "d2_cheats.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

enum : int {
    ID_STATUS = 100, ID_HP, ID_SP, ID_ONEHIT, ID_SHOP, ID_EXP, ID_DISABLE,
    ID_GENERAL_LIST, ID_GENERAL_EDIT, ID_GENERAL_SET,
    ID_PARTY, ID_CHAR_LIST, ID_CHAR_EDIT, ID_CHAR_SET,
    ID_SKILLS, ID_SKILL_KEY, ID_SKILL_EDIT, ID_SKILL_SET,
    ID_EQUIP, ID_EQUIP_LIST, ID_EQUIP_EDIT, ID_EQUIP_SET,
    ID_ITEMS, ID_ITEM_LIST, ID_ITEM_EDIT, ID_ITEM_SET, ID_ITEM_ADD_ID, ID_ITEM_ADD, ID_ITEM_REMOVE,
    ID_PRESET, ID_PRESET_SAVE, ID_PRESET_LOAD,
};
constexpr UINT WM_D2_TOGGLE = WM_APP + 1;
constexpr UINT_PTR TIMER_REFRESH = 1;

HWND g_window;
HFONT g_font;
std::atomic<bool> g_started{false};
std::unique_ptr<D2CheatsSnapshot> g_snap = std::make_unique<D2CheatsSnapshot>();
std::unique_ptr<D2CheatsItems> g_items = std::make_unique<D2CheatsItems>();
uint64_t g_shown_serial = ~0ull, g_items_serial = ~0ull;
int g_unit = -1;

std::wstring wide(const char* s)
{
    if (!s || !*s) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

HWND item(int id) { return GetDlgItem(g_window, id); }

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") || cls == std::wstring(L"LISTBOX") ? WS_EX_CLIENTEDGE : 0,
                             cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_window,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

void label(const wchar_t* text, int x, int y, int w) { make(L"STATIC", text, 0, x, y, w, 18, -1); }

int selected(int id) { return (int)SendMessageW(item(id), LB_GETCURSEL, 0, 0); }

/* Replace a list box's lines, keeping the selection and scroll position. */
void fill(int id, const std::vector<std::wstring>& lines)
{
    HWND list = item(id);
    int sel = selected(id), top = (int)SendMessageW(list, LB_GETTOPINDEX, 0, 0);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& l : lines) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)l.c_str());
    if (sel >= 0 && sel < (int)lines.size()) SendMessageW(list, LB_SETCURSEL, sel, 0);
    SendMessageW(list, LB_SETTOPINDEX, top, 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

std::wstring field_lines(int scope, const uint64_t* values, std::vector<std::wstring>& out)
{
    const D2CheatsField* f = nullptr;
    unsigned n = d2_cheats_fields(scope, &f);
    out.clear();
    for (unsigned i = 0; i < n; ++i) {
        wchar_t line[160];
        swprintf(line, 160, L"%ls = %llu%ls", wide(f[i].label).c_str(), (unsigned long long)values[i],
                 f[i].readonly ? L"  (read only)" : L"");
        out.push_back(line);
    }
    return L"";
}

bool read_number(int id, uint64_t& value)
{
    wchar_t text[64] = {};
    GetWindowTextW(item(id), text, 63);
    wchar_t* end = nullptr;
    value = wcstoull(text, &end, 0);
    return end && end != text && *end == 0;
}

void status(const char* text) { SetWindowTextW(item(ID_STATUS), wide(text).c_str()); }

const D2CheatsField* field_at(int scope, int index)
{
    const D2CheatsField* f = nullptr;
    unsigned n = d2_cheats_fields(scope, &f);
    return index >= 0 && index < (int)n ? &f[index] : nullptr;
}

void refresh()
{
    if (!d2_cheats_snapshot(g_snap.get())) { status("Waiting for the game..."); return; }
    const D2CheatsSnapshot& s = *g_snap;
    bool editable = s.verified && s.ready;
    char head[320];
    if (!s.supported) std::snprintf(head, sizeof head, "Unsupported EBOOT: %s", s.status);
    else if (!s.verified) std::snprintf(head, sizeof head, "Game %s: version not validated, read only. %s", s.version, s.status);
    else if (!s.ready) std::snprintf(head, sizeof head, "Game %s: %s", s.version, s.status[0] ? s.status : "load a save to edit");
    else std::snprintf(head, sizeof head, "Game %s: save loaded, %u units. %s", s.version, s.party_count, s.status);
    status(head);
    if (s.serial == g_shown_serial) return;
    g_shown_serial = s.serial;

    CheckDlgButton(g_window, ID_HP, s.hp_lock ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_window, ID_SP, s.sp_lock ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_window, ID_ONEHIT, s.one_hit ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_window, ID_SHOP, s.free_shop ? BST_CHECKED : BST_UNCHECKED);
    for (int i = 0; i <= 10; ++i)
        if ((1u << i) == s.exp_multiplier) SendMessageW(item(ID_EXP), CB_SETCURSEL, i, 0);

    std::vector<std::wstring> lines;
    field_lines(D2_CHEATS_GENERAL, s.general, lines);
    fill(ID_GENERAL_LIST, lines);

    lines.clear();
    for (unsigned i = 0; i < s.party_count && i < D2_CHEATS_MAX_PARTY; ++i) {
        wchar_t line[160];
        swprintf(line, 160, L"%2u  %ls  Lv %u  class %u", i, wide(s.party[i].name).c_str(), s.party[i].level, s.party[i].class_id);
        lines.push_back(line);
    }
    fill(ID_PARTY, lines);

    lines.clear();
    if (s.unit >= 0 && s.unit == g_unit) field_lines(D2_CHEATS_CHARACTER, s.character, lines);
    fill(ID_CHAR_LIST, lines);

    lines.clear();
    if (s.unit >= 0 && s.unit == g_unit)
        for (unsigned i = 0; i < s.skill_count && i < D2_CHEATS_MAX_SKILLS; ++i) {
            const D2CheatsSkill& k = s.skills[i];
            wchar_t line[200];
            swprintf(line, 200, L"%ls (ID %u)  Lv %u  EXP %llu  boost %u",
                     k.name[0] ? wide(k.name).c_str() : L"Unknown skill", k.id, k.level, (unsigned long long)k.exp, k.boost);
            lines.push_back(line);
        }
    fill(ID_SKILLS, lines);

    lines.clear();
    if (s.unit >= 0 && s.unit == g_unit)
        for (int i = 0; i < D2_CHEATS_EQUIP_SLOTS; ++i) {
            wchar_t line[120];
            swprintf(line, 120, L"Slot %d: %ls", i + 1, s.equipment[i].id ? wide(s.equipment[i].name).c_str() : L"(empty)");
            lines.push_back(line);
        }
    fill(ID_EQUIP, lines);
    int eq = selected(ID_EQUIP);
    lines.clear();
    if (eq >= 0 && s.unit == g_unit && s.equipment[eq].id) field_lines(D2_CHEATS_ITEM, s.equipment[eq].values, lines);
    fill(ID_EQUIP_LIST, lines);

    for (int id : {ID_GENERAL_SET, ID_CHAR_SET, ID_SKILL_SET, ID_EQUIP_SET, ID_ITEM_SET, ID_ITEM_ADD, ID_ITEM_REMOVE})
        EnableWindow(item(id), editable);
    for (int id : {ID_HP, ID_SP, ID_ONEHIT, ID_SHOP, ID_EXP, ID_PRESET_LOAD}) EnableWindow(item(id), s.verified);
}

void refresh_items()
{
    if (!d2_cheats_items_snapshot(g_items.get()) || g_items->serial == g_items_serial) return;
    g_items_serial = g_items->serial;
    std::vector<std::wstring> lines;
    for (unsigned i = 0; i < g_items->count; ++i) {
        const D2CheatsItem& it = g_items->items[i];
        wchar_t line[120];
        swprintf(line, 120, L"%3u  %ls  (ID %u)", it.slot, wide(it.name).c_str(), it.id);
        lines.push_back(line);
    }
    fill(ID_ITEMS, lines);
    int sel = selected(ID_ITEMS);
    lines.clear();
    if (sel >= 0 && sel < (int)g_items->count) field_lines(D2_CHEATS_ITEM, g_items->items[sel].values, lines);
    fill(ID_ITEM_LIST, lines);
}

void report(int rc, const char* what)
{
    if (rc != 0) {
        char t[160];
        std::snprintf(t, sizeof t, "%s could not be queued.", what);
        status(t);
    }
}

void command(int id, int code)
{
    const D2CheatsSnapshot& s = *g_snap;
    uint64_t v = 0;
    switch (id) {
    case ID_HP: report(d2_cheats_set_toggle("hp_lock", IsDlgButtonChecked(g_window, id) == BST_CHECKED), "Toggle"); break;
    case ID_SP: report(d2_cheats_set_toggle("sp_lock", IsDlgButtonChecked(g_window, id) == BST_CHECKED), "Toggle"); break;
    case ID_ONEHIT: report(d2_cheats_set_toggle("one_hit", IsDlgButtonChecked(g_window, id) == BST_CHECKED), "Toggle"); break;
    case ID_SHOP: report(d2_cheats_set_toggle("free_shop", IsDlgButtonChecked(g_window, id) == BST_CHECKED), "Toggle"); break;
    case ID_EXP:
        if (code == CBN_SELCHANGE)
            report(d2_cheats_set_exp_multiplier(1u << SendMessageW(item(ID_EXP), CB_GETCURSEL, 0, 0)), "EXP multiplier");
        break;
    case ID_DISABLE: report(d2_cheats_disable_all(), "Disable all"); break;
    case ID_GENERAL_SET: {
        const D2CheatsField* f = field_at(D2_CHEATS_GENERAL, selected(ID_GENERAL_LIST));
        if (f && read_number(ID_GENERAL_EDIT, v)) report(d2_cheats_set_general(f->key, v, D2_CHEATS_ANY_GENERATION), "Edit");
        break; }
    case ID_PARTY:
        if (code == LBN_SELCHANGE) { g_unit = selected(ID_PARTY); d2_cheats_select_unit(g_unit); g_shown_serial = ~0ull; }
        break;
    case ID_CHAR_SET: {
        const D2CheatsField* f = field_at(D2_CHEATS_CHARACTER, selected(ID_CHAR_LIST));
        if (f && g_unit >= 0 && read_number(ID_CHAR_EDIT, v)) report(d2_cheats_set_character(g_unit, f->key, v, s.generation), "Edit");
        break; }
    case ID_SKILL_SET: {
        static const char* keys[] = {"skill_level", "skill_exp", "skill_boost", "skill_id"};
        int k = (int)SendMessageW(item(ID_SKILL_KEY), CB_GETCURSEL, 0, 0), sk = selected(ID_SKILLS);
        if (k >= 0 && sk >= 0 && g_unit >= 0 && read_number(ID_SKILL_EDIT, v))
            report(d2_cheats_set_skill(g_unit, sk, keys[k], v, s.generation), "Skill edit");
        break; }
    case ID_EQUIP:
        if (code == LBN_SELCHANGE) g_shown_serial = ~0ull;
        break;
    case ID_EQUIP_SET: {
        int slot = selected(ID_EQUIP);
        const D2CheatsField* f = field_at(D2_CHEATS_ITEM, selected(ID_EQUIP_LIST));
        if (f && slot >= 0 && g_unit >= 0 && read_number(ID_EQUIP_EDIT, v))
            report(d2_cheats_set_equipment(g_unit, slot, f->key, v, s.generation), "Equipment edit");
        break; }
    case ID_ITEMS:
        if (code == LBN_SELCHANGE) g_items_serial = ~0ull;
        break;
    case ID_ITEM_SET: {
        int i = selected(ID_ITEMS);
        const D2CheatsField* f = field_at(D2_CHEATS_ITEM, selected(ID_ITEM_LIST));
        if (f && i >= 0 && i < (int)g_items->count && read_number(ID_ITEM_EDIT, v))
            report(d2_cheats_set_item(g_items->items[i].slot, f->key, v, g_items->generation), "Item edit");
        break; }
    case ID_ITEM_ADD:
        if (read_number(ID_ITEM_ADD_ID, v)) report(d2_cheats_add_item((unsigned)v), "Add item");
        break;
    case ID_ITEM_REMOVE: {
        int i = selected(ID_ITEMS);
        if (i >= 0 && i < (int)g_items->count &&
            MessageBoxW(g_window, L"Remove the selected item?", L"Disgaea D2 Cheats", MB_YESNO | MB_ICONQUESTION) == IDYES)
            report(d2_cheats_remove_item(g_items->items[i].slot, g_items->generation), "Remove item");
        break; }
    case ID_PRESET_SAVE:
    case ID_PRESET_LOAD: {
        unsigned slot = (unsigned)SendMessageW(item(ID_PRESET), CB_GETCURSEL, 0, 0);
        report(d2_cheats_preset(id == ID_PRESET_SAVE, slot), "Preset action");
        break; }
    }
}

void build()
{
    int x = 12, y = 10;
    make(L"STATIC", L"Waiting for the game...", SS_LEFTNOWORDWRAP, x, y, 1000, 18, ID_STATUS);
    y += 26;
    make(L"BUTTON", L"Infinite HP (allies)", BS_AUTOCHECKBOX, x, y, 160, 22, ID_HP);
    make(L"BUTTON", L"Infinite SP (allies)", BS_AUTOCHECKBOX, x + 170, y, 160, 22, ID_SP);
    make(L"BUTTON", L"One-hit kills (enemies)", BS_AUTOCHECKBOX, x + 340, y, 180, 22, ID_ONEHIT);
    make(L"BUTTON", L"Free item-shop purchases", BS_AUTOCHECKBOX, x + 530, y, 190, 22, ID_SHOP);
    label(L"EXP gain multiplier:", x + 730, y + 3, 120);
    HWND exp = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, x + 850, y, 80, 300, ID_EXP);
    for (int i = 0; i <= 10; ++i) { wchar_t t[16]; swprintf(t, 16, L"%u×", 1u << i); SendMessageW(exp, CB_ADDSTRING, 0, (LPARAM)t); }
    SendMessageW(exp, CB_SETCURSEL, 0, 0);
    make(L"BUTTON", L"Disable All", BS_PUSHBUTTON, x + 940, y, 100, 24, ID_DISABLE);
    y += 34;

    const DWORD list = LBS_NOTIFY | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT;
    label(L"General (HL, Mana, Cheat Shop)", x, y, 330);
    label(L"Characters", x + 345, y, 330);
    label(L"Character values", x + 690, y, 340);
    y += 20;
    make(L"LISTBOX", L"", list, x, y, 330, 230, ID_GENERAL_LIST);
    make(L"LISTBOX", L"", list, x + 345, y, 330, 230, ID_PARTY);
    make(L"LISTBOX", L"", list, x + 690, y, 350, 230, ID_CHAR_LIST);
    y += 234;
    make(L"EDIT", L"", ES_AUTOHSCROLL, x, y, 240, 24, ID_GENERAL_EDIT);
    make(L"BUTTON", L"Set", BS_PUSHBUTTON, x + 250, y, 80, 24, ID_GENERAL_SET);
    make(L"EDIT", L"", ES_AUTOHSCROLL, x + 690, y, 260, 24, ID_CHAR_EDIT);
    make(L"BUTTON", L"Set", BS_PUSHBUTTON, x + 960, y, 80, 24, ID_CHAR_SET);
    y += 34;

    label(L"Skills (selected character)", x, y, 330);
    label(L"Equipment", x + 345, y, 330);
    label(L"Equipped item values", x + 690, y, 340);
    y += 20;
    make(L"LISTBOX", L"", list, x, y, 330, 170, ID_SKILLS);
    make(L"LISTBOX", L"", list, x + 345, y, 330, 170, ID_EQUIP);
    make(L"LISTBOX", L"", list, x + 690, y, 350, 170, ID_EQUIP_LIST);
    y += 174;
    HWND key = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST, x, y, 100, 200, ID_SKILL_KEY);
    for (const wchar_t* k : {L"Level", L"EXP", L"Boost", L"Skill ID"}) SendMessageW(key, CB_ADDSTRING, 0, (LPARAM)k);
    SendMessageW(key, CB_SETCURSEL, 0, 0);
    make(L"EDIT", L"", ES_AUTOHSCROLL, x + 110, y, 130, 24, ID_SKILL_EDIT);
    make(L"BUTTON", L"Set", BS_PUSHBUTTON, x + 250, y, 80, 24, ID_SKILL_SET);
    make(L"EDIT", L"", ES_AUTOHSCROLL, x + 690, y, 260, 24, ID_EQUIP_EDIT);
    make(L"BUTTON", L"Set", BS_PUSHBUTTON, x + 960, y, 80, 24, ID_EQUIP_SET);
    y += 34;

    label(L"Inventory (bag + warehouse)", x, y, 330);
    label(L"Item values", x + 345, y, 330);
    label(L"Presets", x + 690, y, 340);
    y += 20;
    make(L"LISTBOX", L"", list, x, y, 330, 170, ID_ITEMS);
    make(L"LISTBOX", L"", list, x + 345, y, 330, 170, ID_ITEM_LIST);
    HWND preset = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST, x + 690, y, 160, 200, ID_PRESET);
    for (const wchar_t* p : {L"Default", L"Preset 1", L"Preset 2", L"Preset 3"}) SendMessageW(preset, CB_ADDSTRING, 0, (LPARAM)p);
    SendMessageW(preset, CB_SETCURSEL, 0, 0);
    make(L"BUTTON", L"Save", BS_PUSHBUTTON, x + 860, y, 85, 24, ID_PRESET_SAVE);
    make(L"BUTTON", L"Load", BS_PUSHBUTTON, x + 955, y, 85, 24, ID_PRESET_LOAD);
    label(L"F1 in the game window shows or hides this window.", x + 690, y + 40, 350);
    label(L"Edits apply next frame. Save in-game to keep them.", x + 690, y + 62, 350);
    y += 174;
    label(L"Add item ID:", x, y + 4, 80);
    make(L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER, x + 85, y, 80, 24, ID_ITEM_ADD_ID);
    make(L"BUTTON", L"Add", BS_PUSHBUTTON, x + 170, y, 70, 24, ID_ITEM_ADD);
    make(L"BUTTON", L"Remove", BS_PUSHBUTTON, x + 250, y, 80, 24, ID_ITEM_REMOVE);
    make(L"EDIT", L"", ES_AUTOHSCROLL, x + 345, y, 240, 24, ID_ITEM_EDIT);
    make(L"BUTTON", L"Set", BS_PUSHBUTTON, x + 595, y, 80, 24, ID_ITEM_SET);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: g_window = h; build(); SetTimer(h, TIMER_REFRESH, 250, nullptr); return 0;
    case WM_TIMER:
        if (IsWindowVisible(h)) { refresh(); refresh_items(); }
        return 0;
    case WM_COMMAND: command(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_D2_TOGGLE:
        if (IsWindowVisible(h)) ShowWindow(h, SW_HIDE);
        else { g_shown_serial = g_items_serial = ~0ull; ShowWindow(h, SW_SHOWNORMAL); SetForegroundWindow(h); }
        return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool ours(HWND h)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return h && pid == GetCurrentProcessId();
}

void ui_thread()
{
    g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"D2CheatsWindow";
    RegisterClassW(&wc);
    RECT r = {0, 0, 1064, 836};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    CreateWindowExW(0, L"D2CheatsWindow", L"Disgaea D2 Cheats",
                    WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
                    r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
}

/* F1 edge in either of our windows toggles the cheats window. */
void hotkey_thread()
{
    bool was = false;
    for (;;) {
        Sleep(30);
        bool down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        if (down && !was && ours(GetForegroundWindow()) && g_window) PostMessageW(g_window, WM_D2_TOGGLE, 0, 0);
        was = down;
    }
}

} // namespace

extern "C" void d2_cheats_window_toggle(void)
{
    if (g_window) PostMessageW(g_window, WM_D2_TOGGLE, 0, 0);
}

extern "C" void d2_cheats_host_start(void)
{
    if (g_started.exchange(true) || std::getenv("D2_CHEATS_NO_WINDOW")) return;
    std::thread(ui_thread).detach();
    std::thread(hotkey_thread).detach();
}
#endif
