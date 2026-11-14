// Maotaw Project - Win32 menu and Direct2D character preview.
// Settings are stored in %APPDATA%\\Maotaw\\config.cfg (survives project delete/rebuild).
// Menu toggle and exit keys are configurable in Settings (defaults: F11 / End).

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <sstream>
#include <system_error>
#include <vector>

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <d2d1.h>
#include <dwrite.h>
#include "PreviewRenderer.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

template <typename T>
static T MaxValue(T a, T b)
{
    return (a > b) ? a : b;
}

template <typename T>
static T MinValue(T a, T b)
{
    return (a < b) ? a : b;
}

template <typename T>
static void SafeRelease(T*& value)
{
    if (value)
    {
        value->Release();
        value = nullptr;
    }
}

static float Clamp(float value, float minimum, float maximum)
{
    return MaxValue(minimum, MinValue(value, maximum));
}

static std::filesystem::path ExecutableDirectory()
{
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::filesystem::current_path();
    return std::filesystem::path(modulePath).parent_path();
}

static bool Hit(float x, float y, float left, float top, float right, float bottom)
{
    return x >= left && x <= right && y >= top && y <= bottom;
}

static D2D1_COLOR_F MakeColor(std::uint32_t rgb, float alpha = 1.0f)
{
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha);
}

static D2D1_COLOR_F WithAlpha(const D2D1_COLOR_F& color, float alpha)
{
    return D2D1::ColorF(color.r, color.g, color.b, alpha);
}

static D2D1_COLOR_F BlendColor(
    const D2D1_COLOR_F& base,
    const D2D1_COLOR_F& tint,
    float amount)
{
    amount = Clamp(amount, 0.0f, 1.0f);
    const float inverse = 1.0f - amount;

    return D2D1::ColorF(
        base.r * inverse + tint.r * amount,
        base.g * inverse + tint.g * amount,
        base.b * inverse + tint.b * amount,
        1.0f);
}

static D2D1_COLOR_F HsvToColor(float hue, float saturation, float value)
{
    hue = hue - std::floor(hue);
    saturation = Clamp(saturation, 0.0f, 1.0f);
    value = Clamp(value, 0.0f, 1.0f);

    const float scaled = hue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaled)) % 6;
    const float fraction = scaled - std::floor(scaled);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - fraction * saturation);
    const float t = value * (1.0f - (1.0f - fraction) * saturation);

    switch (sector)
    {
    case 0: return D2D1::ColorF(value, t, p, 1.0f);
    case 1: return D2D1::ColorF(q, value, p, 1.0f);
    case 2: return D2D1::ColorF(p, value, t, 1.0f);
    case 3: return D2D1::ColorF(p, q, value, 1.0f);
    case 4: return D2D1::ColorF(t, p, value, 1.0f);
    default: return D2D1::ColorF(value, p, q, 1.0f);
    }
}

static void ColorToHsv(const D2D1_COLOR_F& color, float& hue, float& saturation, float& value)
{
    const float maximum = MaxValue(color.r, MaxValue(color.g, color.b));
    const float minimum = MinValue(color.r, MinValue(color.g, color.b));
    const float delta = maximum - minimum;

    value = maximum;
    saturation = maximum <= 0.00001f ? 0.0f : delta / maximum;

    if (delta <= 0.00001f)
    {
        hue = 0.0f;
        return;
    }

    if (maximum == color.r)
        hue = std::fmod((color.g - color.b) / delta, 6.0f) / 6.0f;
    else if (maximum == color.g)
        hue = (((color.b - color.r) / delta) + 2.0f) / 6.0f;
    else
        hue = (((color.r - color.g) / delta) + 4.0f) / 6.0f;

    if (hue < 0.0f)
        hue += 1.0f;
}

namespace UI
{
    // Total window = Width + PreviewWidth = 710. The options pane is modestly wider,
    // while the embedded character preview keeps its own right-hand pane.
    constexpr int Width = 500;          // options and left navigation
    constexpr int Height = 400;
    constexpr int PreviewWidth = 210;   // embedded 3D character preview
    constexpr int SideWidth = 210;

    constexpr float SidebarWidth = 125.0f;
    constexpr float TopBarHeight = 38.0f;
    constexpr float ContentPadding = 14.0f;
    constexpr float VisualContentTop = 60.0f;
    constexpr float FeatureListTopOffset = 63.0f;
    constexpr float FeatureListBottomInset = 12.0f;
    constexpr float DragHeight = 38.0f;
}

namespace Theme
{
    const D2D1_COLOR_F Window = MakeColor(0x0A0911);
    const D2D1_COLOR_F TopBar = MakeColor(0x0C0A14);
    const D2D1_COLOR_F Sidebar = MakeColor(0x090811);
    const D2D1_COLOR_F SidebarBorder = MakeColor(0x39264D);

    const D2D1_COLOR_F Card = MakeColor(0x0C0A14, 0.96f);
    const D2D1_COLOR_F CardBorder = MakeColor(0x292033);
    const D2D1_COLOR_F CardInner = MakeColor(0x100D19);
    const D2D1_COLOR_F RowHover = MakeColor(0x241832, 0.78f);
    const D2D1_COLOR_F Divider = MakeColor(0x2B2335, 0.86f);

    const D2D1_COLOR_F Text = MakeColor(0xF3F0F8);
    const D2D1_COLOR_F Muted = MakeColor(0xD0CCDA);
    const D2D1_COLOR_F Muted2 = MakeColor(0xAAA5B8);

    const D2D1_COLOR_F Accent = MakeColor(0xA855F7);
    const D2D1_COLOR_F AccentSoft = MakeColor(0x6E2AA3, 0.42f);
    const D2D1_COLOR_F AccentBright = MakeColor(0xC46BFF);
    const D2D1_COLOR_F SliderKnob = MakeColor(0xF5ECFF);
    const D2D1_COLOR_F Input = MakeColor(0x15111F);
    const D2D1_COLOR_F InputHover = MakeColor(0x21182E);
}

namespace Icon
{
    constexpr const wchar_t* Search = L"\xE721";
    constexpr const wchar_t* Mouse = L"\xE962";
    constexpr const wchar_t* Skull = L"\xE9A1";
    constexpr const wchar_t* Anti = L"\xE72D";
    constexpr const wchar_t* Eye = L"\xE890";
    constexpr const wchar_t* Settings = L"\xE713";
    constexpr const wchar_t* Package = L"\xE7B8";
    constexpr const wchar_t* Heart = L"\xEB51";
    constexpr const wchar_t* Shield = L"\xEA18";
    constexpr const wchar_t* People = L"\xE716";
    constexpr const wchar_t* Link = L"\xE71B";
    constexpr const wchar_t* Info = L"\xE946";
    constexpr const wchar_t* Menu = L"\xE700";
    constexpr const wchar_t* Phone = L"\xE717";
    constexpr const wchar_t* Code = L"\xE943";
    constexpr const wchar_t* Folder = L"\xE8B7";
    constexpr const wchar_t* Save = L"\xE74E";
    constexpr const wchar_t* Load = L"\xE72C";
    constexpr const wchar_t* Delete = L"\xE74D";
    constexpr const wchar_t* Add = L"\xE710";
    constexpr const wchar_t* File = L"\xE8A5";
    constexpr const wchar_t* Check = L"\xE73E";
    constexpr const wchar_t* Chevron = L"\xE70D";
    constexpr const wchar_t* Help = L"\xE897";
    constexpr const wchar_t* Drop = L"\xE790";
}

constexpr int HotkeyKeyMask = 0x00FF;
constexpr int HotkeyCtrlFlag = 0x0100;
constexpr int HotkeyAltFlag = 0x0200;
constexpr int HotkeyShiftFlag = 0x0400;

// ---------------------------------------------------------------------------
// Enemy-only ESP option model. The same menu values drive both the embedded
// synthetic character preview and the separate in-game overlay runtime.
// ---------------------------------------------------------------------------
struct VisualElement
{
    bool enabled = true;
    D2D1_COLOR_F color = MakeColor(0xFFFFFF);
    int thickness = 1;
};

struct TeamVisuals
{
    bool enabled = true;
    bool onlyVisible = false;
    VisualElement box;
    D2D1_COLOR_F boxFill = MakeColor(0xA855F7);
    D2D1_COLOR_F boxOutline = MakeColor(0x080808);
    bool boxOutlineEnabled = true;
    bool boxFillEnabled = true;
    float boxRounding = 4.0f;
    VisualElement health;
    VisualElement armor;
    D2D1_COLOR_F healthSecondary = MakeColor(0xFFFFFF);
    D2D1_COLOR_F armorSecondary = MakeColor(0xFFFFFF);
    VisualElement skeleton;
    VisualElement snapline;
    VisualElement trail;
    VisualElement name;
    VisualElement weapon;
    VisualElement distance;
    int boxStyle = 0;               // 0 Normal, 1 Normal+Filled, 2 Cornered, 3 Cornered+Filled, 4 Rainbow, 5 Rainbow+Filled
    float boxFillOpacity = 0.20f;
    float boxThickness = 2.0f;
    float skeletonThickness = 1.0f;
    int healthStyle = 0;            // 0 Normal, 1 Segmented, 2 Gradient
    int healthAnimationMode = 1;    // 0 Instant, 1 Smooth, 2 Delayed, 3 Spring
    int trailPersonality = 0;       // Classic, Fade, Dots, Ribbon, Pulse, Velocity
    int healthSide = 0;             // Left, Right, Top, Bottom
    int armorSide = 1;
    float maxDistance = 500.0f;
    float healthWidth = 0.0f;       // 0 means 1px in the source settings model
    float armorWidth = 0.0f;
};


static TeamVisuals MakeEnemyVisuals()
{
    TeamVisuals v{};
    v.box.color = MakeColor(0xA855F7);
    v.boxFill = MakeColor(0xA855F7);
    v.boxOutline = MakeColor(0x08070D);
    v.box.enabled = true;
    v.box.thickness = 2;
    v.boxStyle = 2;
    v.boxFillEnabled = true;
    v.boxFillOpacity = 0.18f;
    v.boxRounding = 0.0f;
    v.health.color = MakeColor(0x78F542);
    v.health.enabled = true;
    v.healthSide = 0;
    v.healthStyle = 0;
    v.healthWidth = 3.0f;
    v.healthSecondary = HsvToColor(0.00f, 0.80f, 1.00f);
    v.armor.color = HsvToColor(0.61f, 0.78f, 0.95f);
    v.armorSecondary = HsvToColor(0.54f, 0.72f, 0.72f);
    v.skeleton.color = HsvToColor(0.00f, 0.36f, 0.90f);
    v.snapline.color = HsvToColor(0.08f, 0.72f, 0.94f);
    v.trail.color = HsvToColor(0.92f, 0.50f, 0.92f);
    v.name.color = HsvToColor(0.00f, 0.00f, 0.95f);
    v.weapon.color = HsvToColor(0.08f, 0.72f, 0.96f);
    v.distance.color = HsvToColor(0.00f, 0.00f, 0.72f);
    v.box.enabled = true;
    v.health.enabled = true;
    v.armor.enabled = false;
    v.snapline.enabled = false;
    v.skeleton.enabled = false;
    v.trail.enabled = false;
    v.name.enabled = false;
    v.weapon.enabled = false;
    v.distance.enabled = false;
    return v;
}

#include "RuntimeBindings.hpp"

static void WriteProfileColor(std::wostream& file, const std::wstring& key, const D2D1_COLOR_F& c)
{
    file << key << L".r=" << c.r << L"\n"
         << key << L".g=" << c.g << L"\n"
         << key << L".b=" << c.b << L"\n";
}

static void WriteVisualProfile(std::wostream& file, const std::wstring& prefix, const TeamVisuals& v)
{
    auto b = [&](const wchar_t* k, bool x) { file << prefix << k << L"=" << (x ? 1 : 0) << L"\n"; };
    auto i = [&](const wchar_t* k, int x) { file << prefix << k << L"=" << x << L"\n"; };
    auto f = [&](const wchar_t* k, float x) { file << prefix << k << L"=" << x << L"\n"; };
    b(L"enabled", v.enabled);
    b(L"box.enabled", v.box.enabled); b(L"boxOutlineEnabled", v.boxOutlineEnabled);
    b(L"boxFillEnabled", v.boxFillEnabled); f(L"boxRounding", v.boxRounding);
    b(L"health.enabled", v.health.enabled); b(L"armor.enabled", v.armor.enabled);
    b(L"skeleton.enabled", v.skeleton.enabled); b(L"snapline.enabled", v.snapline.enabled);
    b(L"trail.enabled", v.trail.enabled); b(L"name.enabled", v.name.enabled);
    b(L"weapon.enabled", v.weapon.enabled); b(L"distance.enabled", v.distance.enabled);
    i(L"boxStyle", v.boxStyle); f(L"boxFillOpacity", v.boxFillOpacity);
    f(L"boxThickness", v.boxThickness); f(L"skeletonThickness", v.skeletonThickness);
    i(L"healthStyle", v.healthStyle); i(L"healthAnimationMode", v.healthAnimationMode);
    i(L"trailPersonality", v.trailPersonality); i(L"healthSide", v.healthSide);
    i(L"armorSide", v.armorSide); i(L"armorStyle", v.armor.thickness);
    i(L"skeletonStyle", v.skeleton.thickness); i(L"snaplineWidth", v.snapline.thickness);
    i(L"trailWidth", v.trail.thickness);
    f(L"maxDistance", v.maxDistance); f(L"healthWidth", v.healthWidth); f(L"armorWidth", v.armorWidth);
    WriteProfileColor(file, prefix + L"box.color", v.box.color);
    WriteProfileColor(file, prefix + L"boxFill", v.boxFill);
    WriteProfileColor(file, prefix + L"boxOutline", v.boxOutline);
    WriteProfileColor(file, prefix + L"health.color", v.health.color);
    WriteProfileColor(file, prefix + L"healthSecondary", v.healthSecondary);
    WriteProfileColor(file, prefix + L"armor.color", v.armor.color);
    WriteProfileColor(file, prefix + L"armorSecondary", v.armorSecondary);
    WriteProfileColor(file, prefix + L"skeleton.color", v.skeleton.color);
    WriteProfileColor(file, prefix + L"snapline.color", v.snapline.color);
    WriteProfileColor(file, prefix + L"trail.color", v.trail.color);
    WriteProfileColor(file, prefix + L"name.color", v.name.color);
    WriteProfileColor(file, prefix + L"weapon.color", v.weapon.color);
    WriteProfileColor(file, prefix + L"distance.color", v.distance.color);
}

static bool ApplyVisualProfileValue(TeamVisuals& v, const std::wstring& prefix,
                                    const std::wstring& key, const std::wstring& value)
{
    auto parseBool = [&]() { return std::stoi(value) != 0; };
    auto parseInt = [&]() { return std::stoi(value); };
    auto parseFloat = [&]() { return std::stof(value); };
    auto setB = [&](const wchar_t* k, bool& x) { if (key == prefix + k) { x = parseBool(); return true; } return false; };
    auto setI = [&](const wchar_t* k, int& x) { if (key == prefix + k) { x = parseInt(); return true; } return false; };
    auto setF = [&](const wchar_t* k, float& x) { if (key == prefix + k) { x = parseFloat(); return true; } return false; };
    auto setC = [&](const std::wstring& k, D2D1_COLOR_F& c) {
        if (key == prefix + k + L".r") { c.r = parseFloat(); return true; }
        if (key == prefix + k + L".g") { c.g = parseFloat(); return true; }
        if (key == prefix + k + L".b") { c.b = parseFloat(); return true; }
        return false;
    };
    if (setB(L"enabled", v.enabled) ||
        setB(L"box.enabled", v.box.enabled) || setB(L"boxOutlineEnabled", v.boxOutlineEnabled) ||
        setB(L"boxFillEnabled", v.boxFillEnabled) || setF(L"boxRounding", v.boxRounding) ||
        setB(L"health.enabled", v.health.enabled) || setB(L"armor.enabled", v.armor.enabled) ||
        setB(L"skeleton.enabled", v.skeleton.enabled) || setB(L"snapline.enabled", v.snapline.enabled) ||
        setB(L"trail.enabled", v.trail.enabled) || setB(L"name.enabled", v.name.enabled) ||
        setB(L"weapon.enabled", v.weapon.enabled) || setB(L"distance.enabled", v.distance.enabled) ||
        setI(L"boxStyle", v.boxStyle) || setF(L"boxFillOpacity", v.boxFillOpacity) ||
        setF(L"boxThickness", v.boxThickness) || setF(L"skeletonThickness", v.skeletonThickness) ||
        setI(L"healthStyle", v.healthStyle) || setI(L"healthAnimationMode", v.healthAnimationMode) ||
        setI(L"trailPersonality", v.trailPersonality) || setI(L"healthSide", v.healthSide) ||
        setI(L"armorSide", v.armorSide) || setI(L"armorStyle", v.armor.thickness) ||
        setI(L"skeletonStyle", v.skeleton.thickness) || setI(L"snaplineWidth", v.snapline.thickness) ||
        setI(L"trailWidth", v.trail.thickness) || setF(L"maxDistance", v.maxDistance) ||
        setF(L"healthWidth", v.healthWidth) || setF(L"armorWidth", v.armorWidth)) return true;
    return setC(L"box.color", v.box.color) || setC(L"boxFill", v.boxFill) ||
        setC(L"boxOutline", v.boxOutline) || setC(L"health.color", v.health.color) ||
        setC(L"healthSecondary", v.healthSecondary) || setC(L"armor.color", v.armor.color) ||
        setC(L"armorSecondary", v.armorSecondary) || setC(L"skeleton.color", v.skeleton.color) ||
        setC(L"snapline.color", v.snapline.color) || setC(L"trail.color", v.trail.color) ||
        setC(L"name.color", v.name.color) || setC(L"weapon.color", v.weapon.color) ||
        setC(L"distance.color", v.distance.color);
}


struct NavItem
{
    std::wstring label;
    const wchar_t* icon;
};

static LRESULT CALLBACK EmptySideWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    bool handled = false;
    const LRESULT result = PreviewRenderer::HandleMessage(hwnd, message, wParam, lParam, handled);
    if (handled)
        return result;

    // Keep the preview pane inside the same opaque, interactive window.
    // Returning HTCLIENT prevents clicks from falling through to the game or desktop.
    if (message == WM_NCHITTEST)
        return HTCLIENT;

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

class App
{
public:
    bool Initialize(HINSTANCE instance);
    int Run();
    void Shutdown();

private:

    struct DropdownOverlay
    {
        bool valid = false;
        float buttonLeft = 0.0f;
        float buttonTop = 0.0f;
        float buttonRight = 0.0f;
        float buttonBottom = 0.0f;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float itemHeight = 28.0f;
        std::vector<std::wstring> items;
        int* selected = nullptr;
        int valueOffset = 0;
    };

    HWND hwnd_ = nullptr;
    HWND sideHwnd_ = nullptr;
    ID2D1Factory* d2dFactory_ = nullptr;
    ID2D1HwndRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    IDWriteFactory* writeFactory_ = nullptr;
    IDWriteTextFormat* font_ = nullptr;
    IDWriteTextFormat* smallFont_ = nullptr;
    IDWriteTextFormat* titleFont_ = nullptr;
    IDWriteTextFormat* logoFont_ = nullptr;
    IDWriteTextFormat* iconFont_ = nullptr;

    POINT mouse_{ -1000, -1000 };
    bool mouseDown_ = false;
    bool mousePressed_ = false;

    // Aimbot and recoil settings. These are mirrored into RuntimeBindings.hpp.
    bool aimEnabled_ = true;          // default on so the project does something out of the box
    bool teamCheck_ = true;
    bool visibleOnly_ = false;
    float aimFov_ = 55.0f;
    float aimSmooth_ = 5.5f;
    float aimDistance_ = 250.0f;
    int targetBone_ = 0;
    int aimHoldKey_ = VK_LBUTTON;
    int menuToggleKey_ = VK_F11;   // open/close menu
    int exitKey_ = VK_END;         // close program
    // Recoil control
    bool rcsEnabled_ = true;
    float rcsLegit_ = 50.0f;   // 0 = full aggressive, 100 = max legit
    float rcsSensitivity_ = 2.11f;

    // Internal values derived from rcsLegit_ (kept for config compatibility)
    float rcsSmooth_ = 3.5f;
    float rcsJitter_ = 0.4f;
    float downBias_ = 0.2f;
    float rcsScale_ = 2.0f;
    int activePage_ = 0; // 0 Visuals, 1 Aimbot, 2 Settings

    // Independent visual options. Enabling one never changes another.
    bool espBox_ = true;
    bool espCornerBox_ = false;
    bool espHealthBar_ = true;
    bool espName_ = true;
    bool espDistance_ = false;
    bool espSnapline_ = false;
    bool espSkeleton_ = false;
    bool espWeapon_ = false;
    bool espFilled_ = true;
    bool visualGlow_ = false;
    float visualGlowThickness_ = 2.0f;
    bool infoEnabled_ = true;
    int boxStyle_ = 2;       // 0 off, 1 box, 2 cornered, 3 box+filled, 4 cornered+filled
    int healthStyle_ = 1;    // 0 normal, 1 segmented
    float healthBarWidth_ = 6.0f;
    int activeVisualPopup_ = 1; // selected feature shown in the right-hand properties panel
    int boxStyleView_ = 1;
    bool windowsVisible_ = true;

    TeamVisuals enemyVisuals_ = MakeEnemyVisuals();

    D2D1_COLOR_F boxColor_ = MakeColor(0xFFFFFF);
    D2D1_COLOR_F filledColor_ = MakeColor(0xB31622);
    D2D1_COLOR_F healthColor_ = MakeColor(0x42D66B);
    D2D1_COLOR_F healthBackColor_ = MakeColor(0x080A0D);
    D2D1_COLOR_F visualGlowColor_ = MakeColor(0xB31622);
    D2D1_COLOR_F lineColor_ = MakeColor(0xFFFFFF);
    D2D1_COLOR_F skeletonColor_ = MakeColor(0xD5DBFF);

    bool dropdownOpen_ = false;
    DropdownOverlay dropdownOverlay_{};
    int activeSlider_ = -1;
    float sliderDragStartX_ = 0.0f;
    float sliderDragStartValue_ = 0.0f;
    float sliderDragTrackW_ = 1.0f;

    bool settingsOpen_ = false;
    float previewScale_ = 0.88f;

    std::wstring lastAutoSavedSnapshot_;
    std::wstring pendingAutoSaveSnapshot_;
    ULONGLONG lastAutoSaveChangeTick_ = 0;
    float featureListScroll_ = 0.0f;
    float featureListMaxScroll_ = 0.0f;
    float settingsScroll_ = 0.0f;
    float settingsMaxScroll_ = 0.0f;
    bool featureListDragging_ = false;
    float featureScrollbarGrabOffset_ = 0.0f;

    // Window tint and UI accent are independent color settings.
    D2D1_COLOR_F backgroundColor_ = MakeColor(0x0A0911);
    D2D1_COLOR_F accentColor_ = MakeColor(0xA855F7);

    int* keybindTarget_ = nullptr;
    bool keybindCapture_ = false;
    // The first left-click opens the keybind field; wait for its release so
    // it is not accidentally captured as the new binding.
    bool keybindIgnoreMouseUntilRelease_ = false;

    bool colorPickerOpen_ = false;
    D2D1_COLOR_F* pickerTarget_ = nullptr;
    D2D1_COLOR_F pickerOriginal_{};
    D2D1_COLOR_F pickerWorking_{};
    float pickerHue_ = 0.0f;
    float pickerSaturation_ = 0.0f;
    float pickerValue_ = 1.0f;
    int activeColorArea_ = -1; // 0 = saturation/value, 1 = hue


    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

    HRESULT CreateFactories();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    void Render();
    void DrawShell(float width, float height);
    void DrawSidebar(float height);
    void DrawCards();
    void DrawSettingsPage();

    void DrawAimbotCard(float x, float y, float w, float h);
    void DrawVisualsCard(float x, float y, float w, float h);
    void ToggleCompact(const std::wstring& label, bool& value, float x, float y, float w);
    TeamVisuals& ActiveVisuals() { return enemyVisuals_; }
    void DrawVisualFeatureRow(const std::wstring& label, bool& value, int popupId, float x, float y, float w);
    void DrawVisualFeaturePopup(float cardX, float cardY, float cardW);

    void SetBrush(D2D1_COLOR_F color);
    void FillRect(float left, float top, float right, float bottom, D2D1_COLOR_F color);
    void DrawRect(float left, float top, float right, float bottom, D2D1_COLOR_F color, float stroke = 1.0f);
    void FillRound(float left, float top, float right, float bottom, float radius, D2D1_COLOR_F color);
    void DrawRound(float left, float top, float right, float bottom, float radius, D2D1_COLOR_F color, float stroke = 1.0f);
    void DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float stroke = 1.0f);
    void DrawTextLine(const std::wstring& text, float left, float top, float right, float bottom,
        D2D1_COLOR_F color, IDWriteTextFormat* format = nullptr,
        DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    void DrawIcon(const wchar_t* icon, float left, float top, float right, float bottom, D2D1_COLOR_F color);
    void DrawTriangleLogo(float x, float y);

    bool Hover(float left, float top, float right, float bottom) const;
    D2D1_COLOR_F AccentBright() const;
    D2D1_COLOR_F AccentSoft(float alpha = 0.22f) const;
    void Card(float x, float y, float w, float h, const std::wstring& title);
    void Divider(float x, float y, float w);
    void Toggle(const std::wstring& label, bool& value, float x, float y, float w);
    void Slider(const std::wstring& label, float& value, float minimum, float maximum,
        bool isFloat, float x, float y, float w, int sliderId, float step = 0.01f);
    void Dropdown(const std::wstring& label, const std::vector<std::wstring>& items,
        int& selected, float x, float y, float w, int valueOffset = 0);
    void DrawDropdownOverlay();
    void ColorRow(const std::wstring& label, D2D1_COLOR_F& color, float x, float y, float w, bool second = false);
    void KeybindControl(const std::wstring& label, int& encodedKey, float x, float y, float w);
    bool Button(const std::wstring& label, const wchar_t* icon, float x, float y, float w, float h, bool accent = false, bool danger = false);
    std::filesystem::path ConfigFilePath() const;
    std::wstring SerializeConfig() const;
    bool SaveConfig();
    bool LoadConfig();
    void InitializeAutoConfig();
    void SaveAutoConfigIfChanged(bool force = false);
    std::wstring BaseKeyName(int virtualKey) const;
    std::wstring KeybindName(int encodedKey) const;
    int EncodeHotkey(int virtualKey) const;
    bool IsModifierKey(int virtualKey) const;
    void CompleteKeybindCapture(int virtualKey);
    void OpenColorPicker(D2D1_COLOR_F& color);
    void UpdatePickerColorFromHsv();
    void DrawColorPicker();
    void SyncPreviewSettings();
    void SyncRuntimeSettings();

    ULONGLONG nextUiRefresh_ = 0;
    bool timerResolutionActive_ = false;
};

HRESULT App::CreateFactories()
{
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    if (FAILED(hr))
        return hr;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&writeFactory_));
    if (FAILED(hr))
        return hr;

    auto createFont = [&](const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** out)
        {
            HRESULT result = writeFactory_->CreateTextFormat(
                family,
                nullptr,
                weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"en-US",
                out);

            if (SUCCEEDED(result))
            {
                (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            return result;
        };

    if (FAILED(createFont(L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_NORMAL, &font_)))
        return E_FAIL;
    if (FAILED(createFont(L"Segoe UI", 11.0f, DWRITE_FONT_WEIGHT_NORMAL, &smallFont_)))
        return E_FAIL;
    if (FAILED(createFont(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &titleFont_)))
        return E_FAIL;
    if (FAILED(createFont(L"Segoe UI", 19.0f, DWRITE_FONT_WEIGHT_BOLD, &logoFont_)))
        return E_FAIL;
    if (FAILED(createFont(L"Segoe MDL2 Assets", 15.0f, DWRITE_FONT_WEIGHT_NORMAL, &iconFont_)))
        return E_FAIL;

    return S_OK;
}

HRESULT App::CreateDeviceResources()
{
    if (target_)
        return S_OK;

    RECT client{};
    GetClientRect(hwnd_, &client);

    HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
        D2D1::HwndRenderTargetProperties(
            hwnd_,
            D2D1::SizeU(client.right - client.left, client.bottom - client.top),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &target_);

    if (FAILED(hr))
        return hr;

    return target_->CreateSolidColorBrush(Theme::Text, &brush_);
}

void App::DiscardDeviceResources()
{
    SafeRelease(brush_);
    SafeRelease(target_);
}

bool App::Initialize(HINSTANCE instance)
{
    SetProcessDPIAware();

    if (FAILED(CreateFactories()))
        return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MaotawProjectMainWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc))
        return false;

    WNDCLASSEXW sideClass{};
    sideClass.cbSize = sizeof(sideClass);
    sideClass.style = CS_HREDRAW | CS_VREDRAW;
    sideClass.lpfnWndProc = EmptySideWindowProc;
    sideClass.hInstance = instance;
    sideClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    sideClass.hbrBackground = nullptr;
    sideClass.lpszClassName = L"MaotawProjectPreviewWindow";

    if (!RegisterClassExW(&sideClass))
        return false;

    // Single main window: options (left) + character preview (right), no gap, no separate window.
    const int totalWidth = UI::Width + UI::PreviewWidth;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - totalWidth) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - UI::Height) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Maotaw Project",
        WS_POPUP | WS_CLIPCHILDREN,
        x,
        y,
        totalWidth,
        UI::Height,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_)
        return false;

    // Character preview as borderless child of the main window (right side).
    sideHwnd_ = CreateWindowExW(
        0,
        sideClass.lpszClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        UI::Width,
        static_cast<int>(UI::TopBarHeight),
        UI::PreviewWidth,
        UI::Height - static_cast<int>(UI::TopBarHeight),
        hwnd_,
        nullptr,
        instance,
        nullptr);

    if (!sideHwnd_)
        return false;

    InitializeAutoConfig();
    SyncPreviewSettings();
    if (!PreviewRenderer::Initialize(sideHwnd_))
    {
        MessageBoxW(hwnd_, L"Could not initialise the 3D character preview.",
            L"Preview error", MB_ICONERROR);
        return false;
    }

    // Publish the current menu settings before the first runtime frame. The
    // runtime retries its CS2 attachment automatically when the game starts.
    SyncRuntimeSettings();
    if (!MaotawRuntime::Initialize(instance))
    {
        MessageBoxW(hwnd_, L"Could not initialise the in-game overlay runtime.",
            L"Runtime error", MB_ICONERROR);
        return false;
    }

    // Standard rectangular client area: no layered alpha and no rounded region.

    UnregisterHotKey(hwnd_, 2);
    UnregisterHotKey(hwnd_, 3);
    RegisterHotKey(hwnd_, 2, MOD_NOREPEAT, menuToggleKey_ & 0xFF);
    RegisterHotKey(hwnd_, 3, MOD_NOREPEAT, exitKey_ & 0xFF);

    ShowWindow(hwnd_, SW_SHOW);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(hwnd_);
    UpdateWindow(sideHwnd_);
    timerResolutionActive_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    SetTimer(hwnd_, 1, 1, nullptr);
    return true;
}

int App::Run()
{
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void App::Shutdown()
{
    SaveAutoConfigIfChanged(true);
    if (hwnd_)
    {
        KillTimer(hwnd_, 1);
        UnregisterHotKey(hwnd_, 1);
        UnregisterHotKey(hwnd_, 2);
    }

    if (MaotawRuntime::initialized)
        MaotawRuntime::Shutdown();

    if (timerResolutionActive_)
    {
        timeEndPeriod(1);
        timerResolutionActive_ = false;
    }

    if (sideHwnd_)
    {
        PreviewRenderer::Shutdown();
        DestroyWindow(sideHwnd_);
        sideHwnd_ = nullptr;
    }

    DiscardDeviceResources();
    SafeRelease(iconFont_);
    SafeRelease(logoFont_);
    SafeRelease(titleFont_);
    SafeRelease(smallFont_);
    SafeRelease(font_);
    SafeRelease(writeFactory_);
    SafeRelease(d2dFactory_);
}

void App::SetBrush(D2D1_COLOR_F color)
{
    brush_->SetColor(color);
}

void App::FillRect(float left, float top, float right, float bottom, D2D1_COLOR_F color)
{
    SetBrush(color);
    target_->FillRectangle(D2D1::RectF(left, top, right, bottom), brush_);
}

void App::DrawRect(float left, float top, float right, float bottom, D2D1_COLOR_F color, float stroke)
{
    SetBrush(color);
    target_->DrawRectangle(D2D1::RectF(left, top, right, bottom), brush_, stroke);
}

// The menu uses a strict 0 px corner radius everywhere. Keep these helpers so
// existing layout code stays readable, but render them as regular rectangles.
void App::FillRound(float left, float top, float right, float bottom, float radius, D2D1_COLOR_F color)
{
    (void)radius;
    FillRect(left, top, right, bottom, color);
}

void App::DrawRound(float left, float top, float right, float bottom, float radius, D2D1_COLOR_F color, float stroke)
{
    (void)radius;
    DrawRect(left, top, right, bottom, color, stroke);
}

void App::DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float stroke)
{
    SetBrush(color);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_, stroke);
}

void App::DrawTextLine(const std::wstring& text, float left, float top, float right, float bottom,
    D2D1_COLOR_F color, IDWriteTextFormat* format, DWRITE_TEXT_ALIGNMENT alignment)
{
    if (!format)
        format = font_;

    format->SetTextAlignment(alignment);
    SetBrush(color);
    target_->DrawTextW(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        format,
        D2D1::RectF(left, top, right, bottom),
        brush_,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void App::DrawIcon(const wchar_t* icon, float left, float top, float right, float bottom, D2D1_COLOR_F color)
{
    DrawTextLine(icon, left, top, right, bottom, color, iconFont_, DWRITE_TEXT_ALIGNMENT_CENTER);
}

bool App::Hover(float left, float top, float right, float bottom) const
{
    return Hit(static_cast<float>(mouse_.x), static_cast<float>(mouse_.y), left, top, right, bottom);
}

D2D1_COLOR_F App::AccentBright() const
{
    return D2D1::ColorF(
        Clamp(accentColor_.r * 1.22f + 0.03f, 0.0f, 1.0f),
        Clamp(accentColor_.g * 1.22f + 0.03f, 0.0f, 1.0f),
        Clamp(accentColor_.b * 1.22f + 0.03f, 0.0f, 1.0f),
        1.0f);
}

D2D1_COLOR_F App::AccentSoft(float alpha) const
{
    return WithAlpha(accentColor_, alpha);
}

void App::DrawTriangleLogo(float x, float y)
{
    const D2D1_COLOR_F white = MakeColor(0xFFFFFF);

    DrawLine(x + 2.0f, y + 21.0f, x + 12.5f, y + 3.0f, white, 1.7f);
    DrawLine(x + 12.5f, y + 3.0f, x + 23.0f, y + 21.0f, white, 1.7f);
    DrawLine(x + 23.0f, y + 21.0f, x + 2.0f, y + 21.0f, white, 1.7f);

    DrawLine(x + 7.0f, y + 18.0f, x + 12.5f, y + 9.0f, white, 1.2f);
    DrawLine(x + 12.5f, y + 9.0f, x + 18.0f, y + 18.0f, white, 1.2f);
    DrawLine(x + 18.0f, y + 18.0f, x + 7.0f, y + 18.0f, white, 1.2f);
}

void App::Card(float x, float y, float w, float h, const std::wstring& title)
{
    // Fully transparent section: no card fill and no card border.
    // Only the section title and controls remain on the window background.
    DrawTextLine(title, x + 4.0f, y + 8.0f, x + w - 4.0f, y + 34.0f, Theme::Muted, titleFont_);
}

void App::Divider(float x, float y, float w)
{
    DrawLine(x, y, x + w, y, Theme::Divider, 1.0f);
}


void App::ToggleCompact(const std::wstring& label, bool& value, float x, float y, float w)
{
    const float rowH = 22.0f;
    const bool hovered = Hover(x, y, x + w, y + rowH);
    DrawTextLine(label, x, y, x + w - 48.0f, y + rowH,
        hovered ? Theme::Text : MakeColor(0xE8E4EF), smallFont_);
    const float trackW = 29.0f;
    const float trackH = 13.0f;
    const float trackX = x + w - trackW;
    const float trackY = y + (rowH - trackH) * 0.5f;
    const D2D1_COLOR_F trackColor = value
        ? WithAlpha(accentColor_, hovered ? 0.62f : 0.46f)
        : (hovered ? MakeColor(0x232832) : MakeColor(0x191D24));
    FillRound(trackX, trackY, trackX + trackW, trackY + trackH, 7.0f, trackColor);
    DrawRound(trackX, trackY, trackX + trackW, trackY + trackH, 7.0f,
        value ? WithAlpha(AccentBright(), 0.42f) : MakeColor(0x252A33), 1.0f);
    const float knob = 11.0f;
    const float knobX = value ? trackX + trackW - knob - 1.0f : trackX + 1.0f;
    const float knobY = trackY + 1.0f;
    FillRound(knobX, knobY, knobX + knob, knobY + knob, 6.0f,
        value ? AccentBright() : MakeColor(0x343A45));
    if (!colorPickerOpen_ && !dropdownOpen_ && !keybindCapture_ && hovered && mousePressed_)
        value = !value;
}

void App::Toggle(const std::wstring& label, bool& value, float x, float y, float w)
{
    const float rowH = 28.0f;
    const bool hovered = Hover(x, y, x + w, y + rowH);

    DrawTextLine(label, x, y, x + w - 48.0f, y + rowH,
        hovered ? Theme::Text : MakeColor(0xE8E4EF), font_);

    const float trackW = 31.0f;
    const float trackH = 14.0f;
    const float trackX = x + w - trackW;
    const float trackY = y + (rowH - trackH) * 0.5f;

    const D2D1_COLOR_F trackColor = value
        ? WithAlpha(accentColor_, hovered ? 0.58f : 0.44f)
        : (hovered ? MakeColor(0x232832) : MakeColor(0x191D24));

    FillRound(trackX, trackY, trackX + trackW, trackY + trackH, 7.0f, trackColor);
    DrawRound(trackX, trackY, trackX + trackW, trackY + trackH, 7.0f,
        value ? WithAlpha(AccentBright(), 0.42f) : MakeColor(0x252A33), 1.0f);

    const float knobSize = 12.0f;
    const float knobX = value ? trackX + trackW - knobSize - 1.0f : trackX + 1.0f;
    const float knobY = trackY + 1.0f;

    FillRound(knobX, knobY, knobX + knobSize, knobY + knobSize, 6.0f,
        value ? AccentBright() : MakeColor(0x343A45));

    if (value)
        DrawRound(knobX, knobY, knobX + knobSize, knobY + knobSize, 6.0f,
            WithAlpha(MakeColor(0xFFFFFF), 0.15f), 1.0f);

    if (!colorPickerOpen_ && !dropdownOpen_ && !keybindCapture_ && hovered && mousePressed_)
        value = !value;
}

void App::Slider(const std::wstring& label, float& value, float minimum, float maximum,
    bool isFloat, float x, float y, float w, int sliderId, float step)
{
    const float labelW = 64.0f;
    const float valueW = 40.0f;
    const float trackX = x + labelW + 6.0f;
    const float trackY = y + 14.0f;
    const float trackW = MaxValue(22.0f, w - labelW - valueW - 10.0f);
    const float valueLeft = x + w - valueW;

    DrawTextLine(label, x, y, x + labelW, y + 24.0f, Theme::Text, smallFont_);
    wchar_t buffer[64]{};
    if (sliderId == 43)
        swprintf_s(buffer, L"%.0f%%", value * 100.0f);
    else if (isFloat)
        swprintf_s(buffer, L"%.2f", value);
    else
        swprintf_s(buffer, L"%.1f", value);
    FillRound(valueLeft, y + 2.0f, x + w, y + 24.0f, 6.0f, Theme::Input);
    DrawRound(valueLeft, y + 2.0f, x + w, y + 24.0f, 6.0f, Theme::CardBorder, 0.8f);
    DrawTextLine(buffer, valueLeft + 2.0f, y + 2.0f, x + w - 2.0f, y + 24.0f,
        Theme::Text, smallFont_, DWRITE_TEXT_ALIGNMENT_CENTER);

    const float safeRange = MaxValue(0.0001f, maximum - minimum);
    const float ratio = Clamp((value - minimum) / safeRange, 0.0f, 1.0f);
    const float knobX = trackX + ratio * trackW;
    DrawLine(trackX, trackY, trackX + trackW, trackY, Theme::Divider, 2.5f);
    DrawLine(trackX, trackY, knobX, trackY, accentColor_, 2.5f);
    FillRound(knobX - 4.5f, trackY - 4.5f, knobX + 4.5f, trackY + 4.5f,
        4.5f, activeSlider_ == sliderId ? AccentBright() : MakeColor(0xF3EAFF));

    const bool hovered = Hover(trackX - 5.0f, trackY - 9.0f,
        trackX + trackW + 5.0f, trackY + 9.0f);
    if (!colorPickerOpen_ && !dropdownOpen_ && !keybindCapture_ && hovered && mousePressed_)
    {
        activeSlider_ = sliderId;
        sliderDragStartX_ = static_cast<float>(mouse_.x);
        sliderDragStartValue_ = value;
        sliderDragTrackW_ = trackW;
    }
    if (!colorPickerOpen_ && !dropdownOpen_ && !keybindCapture_ && mouseDown_ && activeSlider_ == sliderId)
    {
        // Relative dragging avoids snapping a value to the click position. The
        // user can make small adjustments by moving the mouse a few pixels.
        const float dragWidth = MaxValue(1.0f, sliderDragTrackW_);
        const float delta = (static_cast<float>(mouse_.x) - sliderDragStartX_) / dragWidth;
        value = Clamp(sliderDragStartValue_ + delta * safeRange, minimum, maximum);
        if (!isFloat)
            value = std::round(value);
        else if (step > 0.0f)
            value = minimum + std::round((value - minimum) / step) * step;
        value = Clamp(value, minimum, maximum);
    }
}

void App::Dropdown(const std::wstring& label, const std::vector<std::wstring>& items,
    int& selected, float x, float y, float w, int valueOffset)
{
    if (items.empty()) return;
    const int maxValue = valueOffset + static_cast<int>(items.size()) - 1;
    selected = std::clamp(selected, valueOffset, maxValue);
    const int selectedIndex = selected - valueOffset;

    const float labelW = std::clamp(w * 0.41f, 70.0f, 120.0f);
    const float boxX = x + labelW + 7.0f;
    const float boxW = MaxValue(48.0f, w - labelW - 7.0f);
    const float boxTop = y;
    const float boxBottom = y + 30.0f;
    const bool isOpen = dropdownOpen_ && dropdownOverlay_.selected == &selected;
    const bool hovered = Hover(boxX, boxTop, boxX + boxW, boxBottom);

    DrawTextLine(label, x, y, x + labelW - 3.0f, y + 25.0f, Theme::Text, smallFont_);

    // Crisp, square field with a restrained violet focus edge.
    const D2D1_COLOR_F fieldFill = isOpen ? MakeColor(0x1B1427)
        : hovered ? MakeColor(0x191420) : MakeColor(0x111019);
    FillRect(boxX, boxTop, boxX + boxW, boxBottom, fieldFill);
    DrawRect(boxX + 0.5f, boxTop + 0.5f, boxX + boxW - 0.5f, boxBottom - 0.5f,
        isOpen ? WithAlpha(AccentBright(), 0.95f)
               : hovered ? MakeColor(0x57416D) : MakeColor(0x332A40),
        isOpen ? 1.25f : 0.9f);
    if (isOpen)
        FillRect(boxX + 1.0f, boxTop + 1.0f, boxX + 3.0f, boxBottom - 1.0f,
            AccentBright());

    DrawTextLine(items[selectedIndex], boxX + 12.0f, boxTop, boxX + boxW - 31.0f, boxBottom,
        Theme::Text, smallFont_);
    DrawIcon(Icon::Chevron, boxX + boxW - 25.0f, boxTop + 2.0f,
        boxX + boxW - 7.0f, boxBottom - 2.0f,
        isOpen ? AccentBright() : Theme::Muted);

    // If another dropdown list is open and this click is on that list,
    // do not steal the click (fixes Health Style covering Animation/Side).
    bool clickOnOpenPopup = false;
    if (dropdownOpen_ && !isOpen && dropdownOverlay_.valid && !dropdownOverlay_.items.empty())
    {
        const float popupBottom = dropdownOverlay_.top +
            dropdownOverlay_.itemHeight * static_cast<float>(dropdownOverlay_.items.size());
        clickOnOpenPopup = Hit(
            static_cast<float>(mouse_.x), static_cast<float>(mouse_.y),
            dropdownOverlay_.left, dropdownOverlay_.top,
            dropdownOverlay_.right, popupBottom);
    }

    if (!colorPickerOpen_ && !keybindCapture_ && hovered && mousePressed_ && !clickOnOpenPopup)
    {
        if (isOpen)
        {
            dropdownOpen_ = false;
            dropdownOverlay_ = DropdownOverlay{};
        }
        else
        {
            dropdownOpen_ = true;
            dropdownOverlay_.selected = &selected;
            dropdownOverlay_.valueOffset = valueOffset;
            dropdownOverlay_.valid = false;
            dropdownOverlay_.items = items;
        }
        keybindCapture_ = false;
        keybindTarget_ = nullptr;
        mousePressed_ = false;
    }

    if (dropdownOpen_ && dropdownOverlay_.selected == &selected)
    {
        dropdownOverlay_.valid = true;
        dropdownOverlay_.buttonLeft = boxX;
        dropdownOverlay_.buttonTop = boxTop;
        dropdownOverlay_.buttonRight = boxX + boxW;
        dropdownOverlay_.buttonBottom = boxBottom;
        dropdownOverlay_.left = boxX;
        dropdownOverlay_.itemHeight = 29.0f;
        dropdownOverlay_.items = items;
        const float popupHeight = dropdownOverlay_.itemHeight * static_cast<float>(items.size());
        // Prefer opening downward; if it would cover too much, open upward.
        dropdownOverlay_.top = boxBottom + 3.0f;
        if (dropdownOverlay_.top + popupHeight > static_cast<float>(UI::Height) - 5.0f)
            dropdownOverlay_.top = MaxValue(4.0f, boxTop - popupHeight - 3.0f);
        dropdownOverlay_.right = boxX + boxW;
    }
}

void App::DrawDropdownOverlay()
{
    if (!dropdownOpen_ || !dropdownOverlay_.valid ||
        dropdownOverlay_.selected == nullptr || dropdownOverlay_.items.empty())
    {
        return;
    }

    const float left = dropdownOverlay_.left;
    const float top = dropdownOverlay_.top;
    const float right = dropdownOverlay_.right;
    const float itemHeight = dropdownOverlay_.itemHeight;
    const float bottom = top + itemHeight * static_cast<float>(dropdownOverlay_.items.size());

    // Flat, opaque popup. Every edge is square (0 px radius), with a fine
    // border and a small offset shadow to separate it from the settings panel.
    FillRect(left + 3.0f, top + 4.0f, right + 3.0f, bottom + 4.0f,
        MakeColor(0x000000, 0.58f));
    FillRect(left, top, right, bottom, MakeColor(0x0D0B13));
    DrawRect(left + 0.5f, top + 0.5f, right - 0.5f, bottom - 0.5f,
        WithAlpha(accentColor_, 0.88f), 1.0f);
    FillRect(left + 1.0f, top + 1.0f, right - 1.0f, top + 2.5f,
        WithAlpha(AccentBright(), 0.9f));

    for (std::size_t index = 0; index < dropdownOverlay_.items.size(); ++index)
    {
        const float itemTop = top + itemHeight * static_cast<float>(index);
        const float itemBottom = itemTop + itemHeight;
        const bool hovered = Hover(left + 1.0f, itemTop, right - 1.0f, itemBottom);
        const bool selected = static_cast<int>(index) ==
            (*dropdownOverlay_.selected - dropdownOverlay_.valueOffset);

        if (selected)
        {
            FillRect(left + 1.0f, itemTop + 1.0f, right - 1.0f, itemBottom - 1.0f,
                MakeColor(0x241632));
            FillRect(left + 1.0f, itemTop + 4.0f, left + 3.0f, itemBottom - 4.0f,
                AccentBright());
        }
        else if (hovered)
        {
            FillRect(left + 1.0f, itemTop + 1.0f, right - 1.0f, itemBottom - 1.0f,
                MakeColor(0x1A1522));
        }

        if (index > 0)
            DrawLine(left + 9.0f, itemTop, right - 9.0f, itemTop,
                WithAlpha(Theme::Divider, 0.42f), 0.6f);

        DrawTextLine(
            dropdownOverlay_.items[index],
            left + 13.0f,
            itemTop,
            right - 33.0f,
            itemBottom,
            selected ? AccentBright() : hovered ? Theme::Text : MakeColor(0xC7C1D1),
            smallFont_);

        if (selected)
            DrawIcon(Icon::Check, right - 27.0f, itemTop + 1.0f,
                right - 8.0f, itemBottom - 1.0f, AccentBright());

        if (!colorPickerOpen_ && hovered && mousePressed_)
        {
            *dropdownOverlay_.selected = static_cast<int>(index) + dropdownOverlay_.valueOffset;
            if (dropdownOverlay_.selected == &boxStyleView_)
                enemyVisuals_.boxStyle = (boxStyleView_ == 1) ? 2 : 0;
            dropdownOpen_ = false;
            dropdownOverlay_ = DropdownOverlay{};
            mousePressed_ = false;
            mouseDown_ = false; // stop anything behind from treating this as a drag/click
            return;
        }
    }
}

bool App::IsModifierKey(int virtualKey) const
{
    return virtualKey == VK_CONTROL ||
        virtualKey == VK_LCONTROL ||
        virtualKey == VK_RCONTROL ||
        virtualKey == VK_MENU ||
        virtualKey == VK_LMENU ||
        virtualKey == VK_RMENU ||
        virtualKey == VK_SHIFT ||
        virtualKey == VK_LSHIFT ||
        virtualKey == VK_RSHIFT;
}

int App::EncodeHotkey(int virtualKey) const
{
    int encoded = virtualKey & HotkeyKeyMask;

    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        encoded |= HotkeyCtrlFlag;

    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0)
        encoded |= HotkeyAltFlag;

    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        encoded |= HotkeyShiftFlag;

    return encoded;
}

void App::CompleteKeybindCapture(int virtualKey)
{
    if (!keybindCapture_ || !keybindTarget_ || virtualKey <= 0)
        return;

    *keybindTarget_ = EncodeHotkey(virtualKey);
    keybindCapture_ = false;
    keybindTarget_ = nullptr;
    keybindIgnoreMouseUntilRelease_ = false;
    mousePressed_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);

    UnregisterHotKey(hwnd_, 2);
    UnregisterHotKey(hwnd_, 3);
    RegisterHotKey(hwnd_, 2, MOD_NOREPEAT, menuToggleKey_ & 0xFF);
    RegisterHotKey(hwnd_, 3, MOD_NOREPEAT, exitKey_ & 0xFF);
}


std::wstring App::BaseKeyName(int virtualKey) const
{
    if (virtualKey == 0)
        return L"None";

    switch (virtualKey)
    {
    case VK_LBUTTON: return L"Mouse 1";
    case VK_RBUTTON: return L"Mouse 2";
    case VK_MBUTTON: return L"Mouse 3";
    case VK_XBUTTON1: return L"Mouse 4";
    case VK_XBUTTON2: return L"Mouse 5";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Escape";
    case VK_BACK: return L"Backspace";
    default:
        break;
    }

    UINT scanCode = MapVirtualKeyW(
        static_cast<UINT>(virtualKey),
        MAPVK_VK_TO_VSC);

    if (virtualKey == VK_LEFT ||
        virtualKey == VK_UP ||
        virtualKey == VK_RIGHT ||
        virtualKey == VK_DOWN ||
        virtualKey == VK_INSERT ||
        virtualKey == VK_DELETE ||
        virtualKey == VK_HOME ||
        virtualKey == VK_END ||
        virtualKey == VK_PRIOR ||
        virtualKey == VK_NEXT)
    {
        scanCode |= 0xE000;
    }

    wchar_t name[64]{};
    if (GetKeyNameTextW(
        static_cast<LONG>(scanCode << 16),
        name,
        static_cast<int>(sizeof(name) / sizeof(name[0]))) > 0)
    {
        return name;
    }

    return L"Key " + std::to_wstring(virtualKey);
}

std::wstring App::KeybindName(int encodedKey) const
{
    const int virtualKey = encodedKey & HotkeyKeyMask;
    if (virtualKey == 0)
        return L"None";

    std::wstring result;

    if ((encodedKey & HotkeyCtrlFlag) != 0)
        result += L"Ctrl+";

    if ((encodedKey & HotkeyAltFlag) != 0)
        result += L"Alt+";

    if ((encodedKey & HotkeyShiftFlag) != 0)
        result += L"Shift+";

    result += BaseKeyName(virtualKey);
    return result;
}

void App::KeybindControl(
    const std::wstring& label,
    int& encodedKey,
    float x,
    float y,
    float w)
{
    const float rowHeight = 31.0f;
    const float fieldWidth = 148.0f;  // wider so Enter / Page Down etc. fit
    const float fieldLeft = x + w - fieldWidth;
    const float fieldRight = x + w;

    const bool rowHover = Hover(x, y, x + w, y + rowHeight);
    const bool fieldHover = Hover(fieldLeft, y + 2.0f, fieldRight, y + rowHeight - 2.0f);
    const bool capturingThis = keybindCapture_ && keybindTarget_ == &encodedKey;

    if (rowHover)
    {
        FillRound(
            x - 5.0f,
            y + 1.0f,
            x + w + 1.0f,
            y + rowHeight - 1.0f,
            5.0f,
            WithAlpha(Theme::RowHover, 0.28f));
    }

    DrawTextLine(
        label,
        x,
        y,
        fieldLeft - 10.0f,
        y + rowHeight,
        rowHover ? Theme::Text : MakeColor(0xD5D5D5),
        font_);

    FillRound(
        fieldLeft,
        y + 4.0f,
        fieldRight,
        y + rowHeight - 4.0f,
        6.0f,
        capturingThis
        ? WithAlpha(accentColor_, 0.22f)
        : fieldHover
        ? Theme::InputHover
        : Theme::Input);

    DrawRound(
        fieldLeft,
        y + 4.0f,
        fieldRight,
        y + rowHeight - 4.0f,
        6.0f,
        capturingThis ? accentColor_ : Theme::CardBorder,
        1.0f);

    const std::wstring valueText = capturingThis
        ? L"Press key..."
        : KeybindName(encodedKey);

    DrawTextLine(
        valueText,
        fieldLeft + 7.0f,
        y + 4.0f,
        fieldRight - 7.0f,
        y + rowHeight - 4.0f,
        capturingThis ? AccentBright() : Theme::Text,
        smallFont_,
        DWRITE_TEXT_ALIGNMENT_CENTER);

    if (!colorPickerOpen_ && !dropdownOpen_ && fieldHover && mousePressed_)
    {
        keybindCapture_ = true;
        keybindTarget_ = &encodedKey;
        // Do not bind the same click that opened the capture field. If the
        // button has already been released before this frame, capture can
        // begin immediately on the next mouse-button press.
        keybindIgnoreMouseUntilRelease_ = mouseDown_;
        dropdownOpen_ = false;
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    }
}


bool App::Button(
    const std::wstring& label,
    const wchar_t* icon,
    float x,
    float y,
    float w,
    float h,
    bool accent,
    bool danger)
{
    const bool hovered = Hover(x, y, x + w, y + h);

    D2D1_COLOR_F fill = hovered ? Theme::InputHover : Theme::Input;
    D2D1_COLOR_F border = Theme::CardBorder;
    D2D1_COLOR_F textColor = hovered ? Theme::Text : MakeColor(0xD5D5D5);

    if (accent)
    {
        fill = hovered ? AccentBright() : accentColor_;
        border = AccentBright();
        textColor = MakeColor(0xFFFFFF);
    }
    else if (danger)
    {
        fill = hovered ? MakeColor(0x54151C) : MakeColor(0x261014);
        border = MakeColor(0x7D1B25);
        textColor = hovered ? MakeColor(0xFFFFFF) : MakeColor(0xDF8A92);
    }

    FillRound(x, y, x + w, y + h, 7.0f, fill);
    DrawRound(x, y, x + w, y + h, 7.0f, border, 1.0f);

    if (icon)
        DrawIcon(icon, x + 8.0f, y, x + 32.0f, y + h, textColor);

    DrawTextLine(
        label,
        x + (icon ? 35.0f : 8.0f),
        y,
        x + w - 8.0f,
        y + h,
        textColor,
        smallFont_,
        icon ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_CENTER);

    return !colorPickerOpen_ && !dropdownOpen_ && hovered && mousePressed_;
}

std::filesystem::path App::ConfigFilePath() const
{
    // Persist outside the project so deleting/rebuilding the folder
    // doesn't wipe settings. Still migrates an old next-to-exe config once.
    wchar_t appData[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::filesystem::path dir;
    if (len > 0 && len < MAX_PATH)
        dir = std::filesystem::path(appData) / L"Maotaw";
    else
        dir = ExecutableDirectory(); // fallback if APPDATA missing

    const auto persistent = dir / L"config.cfg";
    const auto legacy = ExecutableDirectory() / L"config.cfg";

    std::error_code ec;
    if (!std::filesystem::exists(persistent, ec) && std::filesystem::exists(legacy, ec))
    {
        std::filesystem::create_directories(dir, ec);
        std::filesystem::copy_file(legacy, persistent, ec);
    }

    return persistent;
}

std::wstring App::SerializeConfig() const
{
    std::wostringstream file;
    file << std::setprecision(9);

    auto writeBool = [&](const wchar_t* key, bool value)
        {
            file << key << L"=" << (value ? 1 : 0) << L"\n";
        };
    auto writeFloat = [&](const wchar_t* key, float value)
        {
            file << key << L"=" << value << L"\n";
        };
    auto writeInt = [&](const wchar_t* key, int value)
        {
            file << key << L"=" << value << L"\n";
        };
    auto writeColor = [&](const wchar_t* prefix, const D2D1_COLOR_F& color)
        {
            file << prefix << L"R=" << color.r << L"\n";
            file << prefix << L"G=" << color.g << L"\n";
            file << prefix << L"B=" << color.b << L"\n";
        };

    file << L"[MaotawProject]\n";
    writeBool(L"aimEnabled", aimEnabled_);
    writeBool(L"teamCheck", teamCheck_);
    writeBool(L"visibleOnly", visibleOnly_);
    writeFloat(L"aimFov", aimFov_);
    writeFloat(L"aimSmooth", aimSmooth_);
    writeFloat(L"aimDistance", aimDistance_);
    writeInt(L"targetBone", targetBone_);
    writeInt(L"aimHoldKey", aimHoldKey_);
    writeInt(L"menuToggleKey", menuToggleKey_);
    writeInt(L"exitKey", exitKey_);
    writeBool(L"rcsEnabled", rcsEnabled_);
    writeFloat(L"rcsLegit", rcsLegit_);
    writeFloat(L"rcsSensitivity", rcsSensitivity_);
    // Keep legacy keys so older versions can still read this config.
    writeFloat(L"rcsSmooth", rcsSmooth_);
    writeFloat(L"rcsJitter", rcsJitter_);
    writeFloat(L"downBias", downBias_);
    writeFloat(L"rcsScale", rcsScale_);

    writeBool(L"espBox", espBox_);
    writeBool(L"espCornerBox", espCornerBox_);
    writeBool(L"espHealthBar", espHealthBar_);
    writeBool(L"espName", espName_);
    writeBool(L"espDistance", espDistance_);
    writeBool(L"espSnapline", espSnapline_);
    writeBool(L"espSkeleton", espSkeleton_);
    writeBool(L"espWeapon", espWeapon_);
    writeBool(L"espFilled", espFilled_);
    writeBool(L"visualGlow", visualGlow_);
    writeFloat(L"visualGlowThickness", visualGlowThickness_);

    writeFloat(L"previewScale", previewScale_);
    writeInt(L"healthStyle", healthStyle_);
    writeFloat(L"healthBarWidth", healthBarWidth_);

    writeColor(L"background", backgroundColor_);
    writeColor(L"accent", accentColor_);
    writeColor(L"box", boxColor_);
    writeColor(L"filled", filledColor_);
    writeColor(L"health", healthColor_);
    writeColor(L"healthBack", healthBackColor_);
    writeColor(L"visualGlow", visualGlowColor_);
    writeColor(L"line", lineColor_);
    writeColor(L"skeleton", skeletonColor_);

    WriteVisualProfile(file, L"enemy.", enemyVisuals_);
    return file.str();
}

bool App::SaveConfig()
{
    const std::filesystem::path target = ConfigFilePath();
    const std::filesystem::path temporary = target.wstring() + L".tmp";

    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error)
        return false;

    // Write to a temporary file first so an interrupted write cannot destroy
    // the last valid config. MoveFileEx replaces the old file atomically.
    DeleteFileW(temporary.c_str());
    {
        std::wofstream file(temporary, std::ios::out | std::ios::trunc);
        if (!file)
            return false;
        file << SerializeConfig();
        file.flush();
        if (!file.good())
            return false;
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool App::LoadConfig()
{
    std::wifstream file(ConfigFilePath());
    if (!file)
        return false;

    std::wstring line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == L'[')
            continue;

        const std::size_t separator = line.find(L'=');
        if (separator == std::wstring::npos)
            continue;

        const std::wstring key = line.substr(0, separator);
        const std::wstring value = line.substr(separator + 1);

        try
        {
            const bool booleanValue = std::stoi(value) != 0;
            const float floatValue = std::stof(value);

            if (key == L"aimEnabled") aimEnabled_ = booleanValue;
            else if (key == L"teamCheck") teamCheck_ = booleanValue;
            else if (key == L"visibleOnly") visibleOnly_ = booleanValue;
            else if (key == L"aimFov") aimFov_ = floatValue;
            else if (key == L"aimSmooth") aimSmooth_ = floatValue;
            else if (key == L"aimDistance") aimDistance_ = floatValue;
            else if (key == L"targetBone") targetBone_ = std::stoi(value);
            else if (key == L"aimHoldKey") aimHoldKey_ = std::stoi(value);
            else if (key == L"menuToggleKey") menuToggleKey_ = std::stoi(value);
            else if (key == L"exitKey") exitKey_ = std::stoi(value);
            else if (key == L"rcsEnabled") rcsEnabled_ = booleanValue;
            else if (key == L"rcsLegit") rcsLegit_ = floatValue;
            else if (key == L"rcsProfile") { /* legacy */ rcsLegit_ = (std::stoi(value) == 0) ? 70.0f : 15.0f; }
            else if (key == L"rcsSensitivity") rcsSensitivity_ = floatValue;
            else if (key == L"rcsSmooth") rcsSmooth_ = floatValue;
            else if (key == L"rcsJitter") rcsJitter_ = floatValue;
            else if (key == L"downBias") downBias_ = floatValue;
            else if (key == L"rcsScale") rcsScale_ = floatValue;

            else if (key == L"espBox") espBox_ = booleanValue;
            else if (key == L"espCornerBox") espCornerBox_ = booleanValue;
            else if (key == L"espHealthBar") espHealthBar_ = booleanValue;
            else if (key == L"espName") espName_ = booleanValue;
            else if (key == L"espDistance") espDistance_ = booleanValue;
            else if (key == L"espSnapline") espSnapline_ = booleanValue;
            else if (key == L"espSkeleton") espSkeleton_ = booleanValue;
            else if (key == L"espWeapon") espWeapon_ = booleanValue;
            else if (key == L"espFilled") espFilled_ = booleanValue;
            else if (key == L"visualGlow") visualGlow_ = booleanValue;
            else if (key == L"visualGlowThickness") visualGlowThickness_ = floatValue;

            else if (key == L"previewScale") previewScale_ = floatValue;
            else if (key == L"healthStyle") healthStyle_ = std::stoi(value);
            else if (key == L"healthBarWidth") healthBarWidth_ = floatValue;

            else if (key == L"backgroundR") backgroundColor_.r = floatValue;
            else if (key == L"backgroundG") backgroundColor_.g = floatValue;
            else if (key == L"backgroundB") backgroundColor_.b = floatValue;
            else if (key == L"accentR") accentColor_.r = floatValue;
            else if (key == L"accentG") accentColor_.g = floatValue;
            else if (key == L"accentB") accentColor_.b = floatValue;
            else if (key == L"boxR") boxColor_.r = floatValue;
            else if (key == L"boxG") boxColor_.g = floatValue;
            else if (key == L"boxB") boxColor_.b = floatValue;
            else if (key == L"healthR") healthColor_.r = floatValue;
            else if (key == L"healthG") healthColor_.g = floatValue;
            else if (key == L"healthB") healthColor_.b = floatValue;
            else if (key == L"filledR") filledColor_.r = floatValue;
            else if (key == L"filledG") filledColor_.g = floatValue;
            else if (key == L"filledB") filledColor_.b = floatValue;
            else if (key == L"healthBackR") healthBackColor_.r = floatValue;
            else if (key == L"healthBackG") healthBackColor_.g = floatValue;
            else if (key == L"healthBackB") healthBackColor_.b = floatValue;
            else if (key == L"visualGlowR") visualGlowColor_.r = floatValue;
            else if (key == L"visualGlowG") visualGlowColor_.g = floatValue;
            else if (key == L"visualGlowB") visualGlowColor_.b = floatValue;
            else if (key == L"lineR") lineColor_.r = floatValue;
            else if (key == L"lineG") lineColor_.g = floatValue;
            else if (key == L"lineB") lineColor_.b = floatValue;
            else if (key == L"skeletonR") skeletonColor_.r = floatValue;
            else if (key == L"skeletonG") skeletonColor_.g = floatValue;
            else if (key == L"skeletonB") skeletonColor_.b = floatValue;

            else if (ApplyVisualProfileValue(enemyVisuals_, L"enemy.", key, value)) { }
        }
        catch (...)
        {
        }
    }

    aimFov_ = Clamp(aimFov_, 1.0f, 1000.0f);
    aimSmooth_ = Clamp(aimSmooth_, 0.0f, 30.0f);
    aimDistance_ = Clamp(aimDistance_, 10.0f, 1000.0f);
    targetBone_ = std::clamp(targetBone_, 0, 4);
    aimHoldKey_ = std::clamp(aimHoldKey_, 0, 0x07FF);
    menuToggleKey_ = std::clamp(menuToggleKey_, 1, 0xFF);
    exitKey_ = std::clamp(exitKey_, 1, 0xFF);
    rcsLegit_ = Clamp(rcsLegit_, 0.0f, 100.0f);
    rcsSensitivity_ = Clamp(rcsSensitivity_, 0.1f, 5.0f);
    rcsSmooth_ = Clamp(rcsSmooth_, 0.0f, 20.0f);
    rcsJitter_ = Clamp(rcsJitter_, 0.0f, 2.0f);
    downBias_ = Clamp(downBias_, -2.0f, 3.0f);
    rcsScale_ = Clamp(rcsScale_, 0.1f, 4.0f);
    healthStyle_ = std::clamp(healthStyle_, 0, 1);
    healthBarWidth_ = Clamp(healthBarWidth_, 6.0f, 20.0f);
    visualGlowThickness_ = Clamp(visualGlowThickness_, 0.5f, 6.0f);

    auto clampProfile = [&](TeamVisuals& v)
    {
        v.onlyVisible = false;
        v.boxStyle = std::clamp(v.boxStyle, 0, 5);
        v.boxFillOpacity = Clamp(v.boxFillOpacity, 0.0f, 1.0f);
        v.boxThickness = Clamp(v.boxThickness, 1.0f, 4.0f);
        v.skeletonThickness = Clamp(v.skeletonThickness, 1.0f, 4.0f);
        v.boxRounding = 0.0f;
        v.healthStyle = std::clamp(v.healthStyle, 0, 2);
        v.healthAnimationMode = std::clamp(v.healthAnimationMode, 0, 3);
        v.healthSide = std::clamp(v.healthSide, 0, 3);
        v.armorSide = std::clamp(v.armorSide, 0, 3);
        v.armor.thickness = std::clamp(v.armor.thickness, 1, 4);
        v.skeleton.thickness = std::clamp(v.skeleton.thickness, 1, 4);
        v.snapline.thickness = std::clamp(v.snapline.thickness, 1, 3);
        v.trail.thickness = std::clamp(v.trail.thickness, 1, 3);
        v.trailPersonality = std::clamp(v.trailPersonality, 0, 5);
        v.maxDistance = Clamp(v.maxDistance, 1.0f, 500.0f);
        v.healthWidth = Clamp(v.healthWidth, 0.0f, 11.0f);
        v.armorWidth = Clamp(v.armorWidth, 0.0f, 11.0f);
    };
    clampProfile(enemyVisuals_);

    auto clampColor = [](D2D1_COLOR_F& color)
        {
            color.r = Clamp(color.r, 0.0f, 1.0f);
            color.g = Clamp(color.g, 0.0f, 1.0f);
            color.b = Clamp(color.b, 0.0f, 1.0f);
            color.a = 1.0f;
        };

    clampColor(backgroundColor_);
    clampColor(accentColor_);
    clampColor(boxColor_);
    clampColor(healthColor_);
    clampColor(lineColor_);
    clampColor(skeletonColor_);

    auto clampProfileColors = [&](TeamVisuals& v)
    {
        clampColor(v.box.color); clampColor(v.boxFill); clampColor(v.boxOutline);
        clampColor(v.health.color); clampColor(v.healthSecondary);
        clampColor(v.armor.color); clampColor(v.armorSecondary);
        clampColor(v.skeleton.color); clampColor(v.snapline.color); clampColor(v.trail.color);
        clampColor(v.name.color); clampColor(v.weapon.color); clampColor(v.distance.color);
    };
    clampProfileColors(enemyVisuals_);
    boxStyleView_ = (enemyVisuals_.boxStyle == 2 || enemyVisuals_.boxStyle == 3) ? 1 : 0;

    return true;
}

void App::InitializeAutoConfig()
{
    const std::filesystem::path path = ConfigFilePath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    // load from %APPDATA%\Maotaw\config.cfg; create defaults if missing
    if (!LoadConfig())
        SaveConfig();

    // The initial loaded/default state becomes the baseline. The next write is
    // performed only after a real setting change, not every render frame.
    lastAutoSavedSnapshot_ = SerializeConfig();
    pendingAutoSaveSnapshot_ = lastAutoSavedSnapshot_;
    lastAutoSaveChangeTick_ = GetTickCount64();
}

void App::SaveAutoConfigIfChanged(bool force)
{
    const std::wstring snapshot = SerializeConfig();
    if (snapshot == lastAutoSavedSnapshot_)
        return;

    const ULONGLONG now = GetTickCount64();
    if (snapshot != pendingAutoSaveSnapshot_)
    {
        pendingAutoSaveSnapshot_ = snapshot;
        lastAutoSaveChangeTick_ = now;
    }

    // Debounce continuous slider drags and colour picking. A completed change
    // is written shortly after input settles; shutdown/F11 can force a flush.
    if (!force && now - lastAutoSaveChangeTick_ < 120)
        return;

    if (SaveConfig())
        lastAutoSavedSnapshot_ = pendingAutoSaveSnapshot_;
}

void App::ColorRow(const std::wstring& label, D2D1_COLOR_F& color, float x, float y, float w, bool second)
{
    const float rowH = 29.0f;
    const float iconLeft = x + w - 27.0f;
    const float iconRight = x + w - 1.0f;
    const float chipLeft = x + w - 115.0f;
    const float chipRight = chipLeft + 18.0f;
    const float hexLeft = chipRight + 7.0f;
    const bool rowHover = Hover(x, y, x + w, y + rowH);
    const bool iconHover = Hover(iconLeft, y, iconRight, y + rowH);

    if (rowHover)
        FillRound(x - 5.0f, y + 1.0f, x + w + 1.0f, y + rowH - 1.0f, 5.0f,
            WithAlpha(Theme::RowHover, 0.32f));

    DrawTextLine(label, x, y, chipLeft - 8.0f, y + rowH,
        rowHover ? Theme::Text : MakeColor(0xD5D5D5), font_);
    FillRound(chipLeft, y + 5.0f, chipRight, y + rowH - 5.0f, 4.0f, color);
    DrawRound(chipLeft, y + 5.0f, chipRight, y + rowH - 5.0f, 4.0f,
        WithAlpha(MakeColor(0xFFFFFF), 0.14f), 0.7f);

    const int red = static_cast<int>(std::round(Clamp(color.r, 0.0f, 1.0f) * 255.0f));
    const int green = static_cast<int>(std::round(Clamp(color.g, 0.0f, 1.0f) * 255.0f));
    const int blue = static_cast<int>(std::round(Clamp(color.b, 0.0f, 1.0f) * 255.0f));
    wchar_t hex[16]{};
    swprintf_s(hex, L"#%02X%02X%02X", red, green, blue);
    DrawTextLine(hex, hexLeft, y, iconLeft - 3.0f, y + rowH,
        Theme::Text, smallFont_);

    if (iconHover)
        FillRound(iconLeft + 1.0f, y + 3.0f, iconRight - 1.0f, y + rowH - 3.0f,
            5.0f, WithAlpha(accentColor_, 0.16f));
    DrawIcon(Icon::Drop, iconLeft, y + (second ? -1.0f : 0.0f), iconRight, y + rowH,
        Theme::Text);

    if (!colorPickerOpen_ && !dropdownOpen_ && iconHover && mousePressed_)
        OpenColorPicker(color);
}

void App::OpenColorPicker(D2D1_COLOR_F& color)
{
    pickerTarget_ = &color;
    pickerOriginal_ = color;
    pickerWorking_ = color;
    ColorToHsv(color, pickerHue_, pickerSaturation_, pickerValue_);
    colorPickerOpen_ = true;
    activeColorArea_ = -1;
    activeSlider_ = -1;
    dropdownOpen_ = false;
}

void App::UpdatePickerColorFromHsv()
{
    pickerWorking_ = HsvToColor(pickerHue_, pickerSaturation_, pickerValue_);
}

void App::DrawColorPicker()
{
    if (!colorPickerOpen_ || !pickerTarget_)
        return;

    FillRect(0.0f, 0.0f, static_cast<float>(UI::Width),
        static_cast<float>(UI::Height), MakeColor(0x000000, 0.76f));

    const float boxWidth = MinValue(320.0f, static_cast<float>(UI::Width) - 24.0f);
    const float boxHeight = 300.0f;
    const float x = (static_cast<float>(UI::Width) - boxWidth) * 0.5f;
    const float y = (static_cast<float>(UI::Height) - boxHeight) * 0.5f;

    FillRound(x, y, x + boxWidth, y + boxHeight, 12.0f, MakeColor(0x090A0D));
    DrawRound(x, y, x + boxWidth, y + boxHeight, 12.0f, Theme::CardBorder, 1.0f);
    DrawLine(x + 15.0f, y + 42.0f, x + boxWidth - 15.0f, y + 42.0f, accentColor_, 1.2f);

    DrawTextLine(L"Custom Color Picker", x + 16.0f, y + 5.0f,
        x + boxWidth - 16.0f, y + 38.0f, Theme::Text, titleFont_);

    const float paletteX = x + 14.0f;
    const float paletteY = y + 57.0f;
    const float paletteWidth = boxWidth - 56.0f;
    const float paletteHeight = 174.0f;
    const float hueX = paletteX + paletteWidth + 10.0f;
    const float hueWidth = 18.0f;

    constexpr int columns = 64;
    constexpr int rows = 42;
    const float cellW = paletteWidth / static_cast<float>(columns);
    const float cellH = paletteHeight / static_cast<float>(rows);

    for (int row = 0; row < rows; ++row)
    {
        const float value = 1.0f - static_cast<float>(row) / static_cast<float>(rows - 1);
        for (int column = 0; column < columns; ++column)
        {
            const float saturation = static_cast<float>(column) / static_cast<float>(columns - 1);
            const float left = paletteX + cellW * static_cast<float>(column);
            const float top = paletteY + cellH * static_cast<float>(row);
            FillRect(left, top, left + cellW + 0.8f, top + cellH + 0.8f,
                HsvToColor(pickerHue_, saturation, value));
        }
    }

    constexpr int hueSteps = 96;
    const float hueStep = paletteHeight / static_cast<float>(hueSteps);
    for (int i = 0; i < hueSteps; ++i)
    {
        const float hue = static_cast<float>(i) / static_cast<float>(hueSteps - 1);
        const float top = paletteY + hueStep * static_cast<float>(i);
        FillRect(hueX, top, hueX + hueWidth, top + hueStep + 0.8f,
            HsvToColor(hue, 1.0f, 1.0f));
    }

    DrawRect(paletteX, paletteY, paletteX + paletteWidth, paletteY + paletteHeight,
        MakeColor(0x202329), 1.0f);
    DrawRect(hueX, paletteY, hueX + hueWidth, paletteY + paletteHeight,
        MakeColor(0x202329), 1.0f);

    const bool paletteHover = Hover(paletteX, paletteY,
        paletteX + paletteWidth, paletteY + paletteHeight);
    const bool hueHover = Hover(hueX, paletteY,
        hueX + hueWidth, paletteY + paletteHeight);

    if (mousePressed_ && paletteHover)
        activeColorArea_ = 0;
    else if (mousePressed_ && hueHover)
        activeColorArea_ = 1;

    if (mouseDown_ && activeColorArea_ == 0 && paletteHover)
    {
        pickerSaturation_ = Clamp(
            (static_cast<float>(mouse_.x) - paletteX) / paletteWidth, 0.0f, 1.0f);
        pickerValue_ = 1.0f - Clamp(
            (static_cast<float>(mouse_.y) - paletteY) / paletteHeight, 0.0f, 1.0f);
        UpdatePickerColorFromHsv();
    }
    else if (mouseDown_ && activeColorArea_ == 1 && hueHover)
    {
        pickerHue_ = Clamp(
            (static_cast<float>(mouse_.y) - paletteY) / paletteHeight, 0.0f, 1.0f);
        UpdatePickerColorFromHsv();
    }

    const float pointX = paletteX + pickerSaturation_ * paletteWidth;
    const float pointY = paletteY + (1.0f - pickerValue_) * paletteHeight;

    SetBrush(MakeColor(0x000000));
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(pointX, pointY), 5.5f, 5.5f), brush_, 3.0f);
    SetBrush(MakeColor(0xFFFFFF));
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(pointX, pointY), 5.5f, 5.5f), brush_, 1.4f);

    const float hueMarkerY = paletteY + pickerHue_ * paletteHeight;
    FillRect(hueX - 3.0f, hueMarkerY - 2.0f,
        hueX + hueWidth + 3.0f, hueMarkerY + 2.0f, MakeColor(0xFFFFFF));
    DrawRect(hueX - 4.0f, hueMarkerY - 3.0f,
        hueX + hueWidth + 4.0f, hueMarkerY + 3.0f, MakeColor(0x000000), 1.0f);

    FillRound(x + 18.0f, y + 244.0f, x + 49.0f, y + 266.0f, 4.0f, pickerWorking_);
    DrawRound(x + 18.0f, y + 244.0f, x + 49.0f, y + 266.0f, 4.0f,
        WithAlpha(MakeColor(0xFFFFFF), 0.18f), 1.0f);

    wchar_t hexText[16]{};
    const int red = std::clamp(static_cast<int>(std::round(pickerWorking_.r * 255.0f)), 0, 255);
    const int green = std::clamp(static_cast<int>(std::round(pickerWorking_.g * 255.0f)), 0, 255);
    const int blue = std::clamp(static_cast<int>(std::round(pickerWorking_.b * 255.0f)), 0, 255);
    swprintf_s(hexText, L"#%02X%02X%02X", red, green, blue);

    DrawTextLine(hexText, x + 58.0f, y + 239.0f,
        x + 165.0f, y + 270.0f, Theme::Text, smallFont_);

    const float buttonY = y + boxHeight - 37.0f;
    const float cancelX = x + boxWidth - 158.0f;
    const float applyX = x + boxWidth - 81.0f;

    const bool cancelHover = Hover(cancelX, buttonY, cancelX + 68.0f, buttonY + 25.0f);
    const bool applyHover = Hover(applyX, buttonY, applyX + 63.0f, buttonY + 25.0f);

    FillRound(cancelX, buttonY, cancelX + 68.0f, buttonY + 25.0f, 6.0f,
        cancelHover ? Theme::InputHover : Theme::Input);
    DrawRound(cancelX, buttonY, cancelX + 68.0f, buttonY + 25.0f, 6.0f,
        Theme::CardBorder, 1.0f);
    DrawTextLine(L"Cancel", cancelX, buttonY, cancelX + 68.0f, buttonY + 25.0f,
        Theme::Text, smallFont_, DWRITE_TEXT_ALIGNMENT_CENTER);

    FillRound(applyX, buttonY, applyX + 63.0f, buttonY + 25.0f, 6.0f,
        applyHover ? AccentBright() : accentColor_);
    DrawTextLine(L"Apply", applyX, buttonY, applyX + 63.0f, buttonY + 25.0f,
        MakeColor(0xFFFFFF), smallFont_, DWRITE_TEXT_ALIGNMENT_CENTER);

    if (mousePressed_ && cancelHover)
    {
        *pickerTarget_ = pickerOriginal_;
        colorPickerOpen_ = false;
        pickerTarget_ = nullptr;
        activeColorArea_ = -1;
    }
    else if (mousePressed_ && applyHover)
    {
        *pickerTarget_ = pickerWorking_;
        colorPickerOpen_ = false;
        pickerTarget_ = nullptr;
        activeColorArea_ = -1;
    }
}

void App::DrawAimbotCard(float x, float y, float w, float h)
{
    const float gap = 8.0f;
    const float columnW = (w - gap) * 0.5f;
    const float left = x;
    const float right = x + columnW + gap;

    Card(left, y, columnW, h, L"TARGETING");
    Toggle(L"Enabled", aimEnabled_, left + 10.0f, y + 44.0f, columnW - 20.0f);
    Toggle(L"Team check", teamCheck_, left + 10.0f, y + 72.0f, columnW - 20.0f);
    Toggle(L"Visible only", visibleOnly_, left + 10.0f, y + 100.0f, columnW - 20.0f);
    Divider(left + 10.0f, y + 131.0f, columnW - 20.0f);
    Dropdown(L"Target point", { L"Head", L"Neck", L"Chest", L"Stomach", L"Closest" },
        targetBone_, left + 10.0f, y + 141.0f, columnW - 20.0f);
    Slider(L"FOV px", aimFov_, 1.0f, 1000.0f, false,
        left + 10.0f, y + 180.0f, columnW - 20.0f, 1);
    Slider(L"Smooth", aimSmooth_, 0.0f, 30.0f, true,
        left + 10.0f, y + 211.0f, columnW - 20.0f, 2);
    Slider(L"Distance", aimDistance_, 10.0f, 1000.0f, false,
        left + 10.0f, y + 242.0f, columnW - 20.0f, 3);
    KeybindControl(L"Hold to aim", aimHoldKey_, left + 10.0f, y + 278.0f, columnW - 20.0f);

    Card(right, y, columnW, h, L"RECOIL CONTROL");
    Divider(right + 10.0f, y + 44.0f, columnW - 20.0f);
    Toggle(L"Enabled", rcsEnabled_, right + 10.0f, y + 57.0f, columnW - 20.0f);
    Slider(L"Legit amount", rcsLegit_, 0.0f, 100.0f, false,
        right + 10.0f, y + 91.0f, columnW - 20.0f, 24);
    Slider(L"Game sens.", rcsSensitivity_, 0.1f, 5.0f, true,
        right + 10.0f, y + 130.0f, columnW - 20.0f, 25);
    DrawTextLine(L"0 = full aggressive (fast & strong). 100 = max legit (soft & human). Active while Mouse 1 is held.",
        right + 10.0f, y + 175.0f, right + columnW - 10.0f, y + 235.0f,
        Theme::Muted2, smallFont_);
}

void App::DrawVisualFeatureRow(const std::wstring& label, bool& value, int popupId, float x, float y, float w)
{
    (void)value; // Feature state is edited in the properties header, like the reference UI.
    constexpr float rowH = 25.0f;
    const bool hovered = Hover(x, y, x + w, y + rowH);
    const bool selected = activeVisualPopup_ == popupId;

    if (selected)
        FillRound(x + 1.0f, y + 1.0f, x + w - 1.0f, y + rowH - 1.0f,
            6.0f, WithAlpha(accentColor_, 0.27f));
    else if (hovered)
        FillRound(x + 1.0f, y + 1.0f, x + w - 1.0f, y + rowH - 1.0f,
            6.0f, Theme::RowHover);

    if (selected)
        FillRound(x + 1.0f, y + 5.0f, x + 3.0f, y + rowH - 5.0f,
            1.5f, AccentBright());

    const wchar_t* glyph = Icon::Info;
    switch (popupId)
    {
    case 1: glyph = Icon::Package; break;
    case 2: glyph = Icon::Heart; break;
    case 3: glyph = Icon::Shield; break;
    case 4: glyph = Icon::People; break;
    case 5: glyph = Icon::Link; break;
    case 7: glyph = Icon::Info; break;
    default: break;
    }
    DrawIcon(glyph, x + 5.0f, y + 1.0f, x + 29.0f, y + rowH - 1.0f,
        selected ? AccentBright() : Theme::Text);
    DrawTextLine(label, x + 34.0f, y, x + w - 5.0f, y + rowH,
        selected ? Theme::Text : (hovered ? Theme::Text : MakeColor(0xF0EDF5)), font_);

    if (!colorPickerOpen_ && !dropdownOpen_ && hovered && mousePressed_)
        activeVisualPopup_ = popupId;
}

void App::DrawVisualFeaturePopup(float cardX, float cardY, float cardW)
{
    TeamVisuals& v = ActiveVisuals();
    constexpr float listColumnW = 88.0f;
    constexpr float columnGap = 16.0f;
    const float rightX = cardX + 12.0f + listColumnW + columnGap;
    const float rightW = cardW - 24.0f - listColumnW - columnGap;
    const float top = cardY + 63.0f;
    const float px = rightX + 2.0f;
    const float pw = rightW - 4.0f;

    const wchar_t* heading = L"Box";
    bool* enabled = &v.box.enabled;
    switch (activeVisualPopup_)
    {
    case 2: heading = L"Health"; enabled = &v.health.enabled; break;
    case 3: heading = L"Armor"; enabled = &v.armor.enabled; break;
    case 4: heading = L"Skeleton"; enabled = &v.skeleton.enabled; break;
    case 5: heading = L"Snapline"; enabled = &v.snapline.enabled; break;
    case 7: heading = L"Info"; enabled = &infoEnabled_; break;
    default: heading = L"Box"; enabled = &v.box.enabled; break;
    }

    if (activeVisualPopup_ == 7)
    {
        const bool wasEnabled = infoEnabled_;
        ToggleCompact(heading, infoEnabled_, px, top, pw);
        if (wasEnabled != infoEnabled_)
        {
            v.name.enabled = infoEnabled_;
            v.weapon.enabled = infoEnabled_;
            v.distance.enabled = infoEnabled_;
        }
    }
    else if (enabled)
        ToggleCompact(heading, *enabled, px, top, pw);
    else
        DrawTextLine(heading, px + 2.0f, top, px + pw - 2.0f, top + 23.0f,
            Theme::Text, titleFont_);
    DrawLine(rightX + 2.0f, top + 27.0f, rightX + rightW - 2.0f, top + 27.0f,
        Theme::Divider, 1.0f);

    const float controlsTop = top + 35.0f;
    switch (activeVisualPopup_)
    {
    case 1:
    {
        Dropdown(L"Style", { L"Normal", L"Cornered" }, boxStyleView_, px, controlsTop, pw);
        ColorRow(L"Box color", v.box.color, px, controlsTop + 27.0f, pw);
        ColorRow(L"Outline color", v.boxOutline, px, controlsTop + 54.0f, pw);
        ColorRow(L"Fill color", v.boxFill, px, controlsTop + 81.0f, pw);
        Slider(L"Thickness", v.boxThickness, 1.0f, 4.0f, true,
            px, controlsTop + 108.0f, pw, 40, 0.1f);
        ToggleCompact(L"Fill", v.boxFillEnabled, px, controlsTop + 137.0f, pw);
        Slider(L"Fill opacity", v.boxFillOpacity, 0.0f, 1.0f, true,
            px, controlsTop + 164.0f, pw, 41, 0.01f);
        break;
    }
    case 2:
        Dropdown(L"Style", { L"Normal", L"Segmented", L"Gradient" }, v.healthStyle, px, controlsTop, pw);
        Dropdown(L"Animation", { L"Instant", L"Smooth", L"Delayed", L"Spring" }, v.healthAnimationMode, px, controlsTop + 31.0f, pw);
        Dropdown(L"Side", { L"Left", L"Right", L"Top", L"Bottom" }, v.healthSide, px, controlsTop + 62.0f, pw);
        Slider(L"Bar width", v.healthWidth, 0.0f, 11.0f, false, px, controlsTop + 94.0f, pw, 52);
        ColorRow(L"Primary", v.health.color, px, controlsTop + 122.0f, pw);
        ColorRow(L"Secondary", v.healthSecondary, px, controlsTop + 150.0f, pw);
        break;
    case 3:
        Dropdown(L"Side", { L"Left", L"Right", L"Top", L"Bottom" }, v.armorSide, px, controlsTop, pw);
        Dropdown(L"Style", { L"Bar", L"Segments", L"Gradient", L"Plates" },
            v.armor.thickness, px, controlsTop + 31.0f, pw, 1);
        Slider(L"Bar width", v.armorWidth, 0.0f, 11.0f, false, px, controlsTop + 63.0f, pw, 53);
        ColorRow(L"Primary", v.armor.color, px, controlsTop + 91.0f, pw);
        ColorRow(L"Secondary", v.armorSecondary, px, controlsTop + 121.0f, pw);
        break;
    case 4:
        Dropdown(L"Style", { L"Clean", L"Minimal", L"Glow", L"Pulse" },
            v.skeleton.thickness, px, controlsTop, pw, 1);
        ColorRow(L"Skeleton color", v.skeleton.color, px, controlsTop + 33.0f, pw);
        Slider(L"Thickness", v.skeletonThickness, 1.0f, 4.0f, true,
            px, controlsTop + 62.0f, pw, 42, 0.1f);
        break;
    case 5:
        Dropdown(L"Width", { L"1px", L"2px", L"3px" },
            v.snapline.thickness, px, controlsTop, pw, 1);
        ColorRow(L"Color", v.snapline.color, px, controlsTop + 33.0f, pw);
        break;
    case 7:
    {
        const bool before = infoEnabled_;
        ToggleCompact(L"Name", v.name.enabled, px, controlsTop, pw);
        ColorRow(L"Name color", v.name.color, px, controlsTop + 25.0f, pw);
        ToggleCompact(L"Weapon icon", v.weapon.enabled, px, controlsTop + 53.0f, pw);
        ColorRow(L"Weapon color", v.weapon.color, px, controlsTop + 78.0f, pw);
        ToggleCompact(L"Distance", v.distance.enabled, px, controlsTop + 106.0f, pw);
        ColorRow(L"Distance color", v.distance.color, px, controlsTop + 131.0f, pw);
        infoEnabled_ = v.name.enabled || v.weapon.enabled || v.distance.enabled;
        if (before != infoEnabled_ && !infoEnabled_)
        {
            v.name.enabled = false;
            v.weapon.enabled = false;
            v.distance.enabled = false;
        }
        break;
    }
    default:
        break;
    }
}

void App::DrawVisualsCard(float x, float y, float w, float h)
{
    TeamVisuals& v = ActiveVisuals();
    DrawTextLine(L"Enemy Visuals", x + 4.0f, y + 1.0f,
        x + w - 4.0f, y + 27.0f, Theme::Text, logoFont_);
    DrawTextLine(L"Customize ESP for enemy players.", x + 5.0f, y + 27.0f,
        x + w - 4.0f, y + 45.0f, Theme::Muted, font_);
    FillRound(x + 5.0f, y + 53.0f, x + 34.0f, y + 55.0f, 1.0f, AccentBright());
    DrawLine(x + 34.0f, y + 54.0f, x + w - 3.0f, y + 54.0f,
        Theme::Divider, 0.8f);

    constexpr float listColumnW = 88.0f;
    constexpr float columnGap = 16.0f;
    const float listX = x + 12.0f;
    const float listW = listColumnW;
    const float editorX = listX + listW + columnGap;
    const float editorW = w - 4.0f - listW - columnGap;
    const float listTop = y + UI::FeatureListTopOffset;
    const float listBottom = y + h - UI::FeatureListBottomInset;
    const float viewportH = MaxValue(1.0f, listBottom - listTop);
    constexpr float rowStep = 27.0f;
    constexpr float contentH = rowStep * 6.0f;
    featureListMaxScroll_ = MaxValue(0.0f, contentH - viewportH);
    featureListScroll_ = Clamp(featureListScroll_, 0.0f, featureListMaxScroll_);

    // Fine separator between the feature rail and its editor, as in the reference.
    DrawLine(editorX - 16.0f, listTop - 3.0f, editorX - 16.0f, listBottom - 2.0f,
        Theme::Divider, 0.8f);

    const D2D1_RECT_F listClip = D2D1::RectF(listX, listTop, listX + listW, listBottom);
    target_->PushAxisAlignedClip(listClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float rowStart = listTop + 1.0f - featureListScroll_;
    DrawVisualFeatureRow(L"Box", v.box.enabled, 1, listX, rowStart + rowStep * 0.0f, listW);
    DrawVisualFeatureRow(L"Health", v.health.enabled, 2, listX, rowStart + rowStep * 1.0f, listW);
    DrawVisualFeatureRow(L"Armor", v.armor.enabled, 3, listX, rowStart + rowStep * 2.0f, listW);
    DrawVisualFeatureRow(L"Skeleton", v.skeleton.enabled, 4, listX, rowStart + rowStep * 3.0f, listW);
    DrawVisualFeatureRow(L"Snapline", v.snapline.enabled, 5, listX, rowStart + rowStep * 4.0f, listW);
    infoEnabled_ = v.name.enabled || v.weapon.enabled || v.distance.enabled;
    DrawVisualFeatureRow(L"Info", infoEnabled_, 7, listX, rowStart + rowStep * 5.0f, listW);
    target_->PopAxisAlignedClip();

    DrawVisualFeaturePopup(x, y, w);
}

void App::DrawSidebar(float height)
{
    FillRect(0.0f, UI::TopBarHeight, UI::SidebarWidth, height, Theme::Sidebar);
    DrawLine(UI::SidebarWidth, UI::TopBarHeight, UI::SidebarWidth, height,
        Theme::Divider, 0.8f);

    struct SidebarEntry { const wchar_t* label; const wchar_t* icon; int page; };
    const SidebarEntry entries[] = {
        { L"Visuals", Icon::Eye, 0 },
        { L"Aimbot", Icon::Mouse, 1 },
        { L"Settings", Icon::Settings, 2 }
    };

    const float left = 8.0f;
    const float right = UI::SidebarWidth - 8.0f;
    const float top = 65.0f;
    const float itemH = 31.0f;
    const float itemGap = 7.0f;

    for (const auto& entry : entries)
    {
        const float y = top + static_cast<float>(entry.page) * (itemH + itemGap);
        const bool hovered = Hover(left, y, right, y + itemH);
        const bool selected = activePage_ == entry.page;
        if (selected)
        {
            FillRect(left, y, right, y + itemH, WithAlpha(accentColor_, 0.18f));
            FillRect(left, y + 1.0f, left + 2.0f, y + itemH - 1.0f, AccentBright());
        }
        else if (hovered)
            FillRect(left, y, right, y + itemH, Theme::RowHover);

        DrawIcon(entry.icon, left + 8.0f, y + 3.0f, left + 29.0f, y + itemH - 3.0f,
            selected ? MakeColor(0xEAD8FF) : Theme::Muted);
        DrawTextLine(entry.label, left + 36.0f, y, right - 5.0f, y + itemH,
            selected ? Theme::Text : Theme::Muted, smallFont_);

        if (!colorPickerOpen_ && !dropdownOpen_ && !keybindCapture_ && mousePressed_ && hovered)
        {
            activePage_ = entry.page;
            activeSlider_ = -1;
            dropdownOpen_ = false;
            dropdownOverlay_ = DropdownOverlay{};
            mousePressed_ = false;
        }
    }
}


void App::DrawCards()
{
    const float margin = UI::SidebarWidth + 12.0f;
    const float cardY = UI::VisualContentTop;
    const float cardWidth = static_cast<float>(UI::Width) - margin - 10.0f;
    const float cardHeight = static_cast<float>(UI::Height) - cardY - 8.0f;

    if (activePage_ == 1)
        DrawAimbotCard(margin, cardY, cardWidth, cardHeight);
    else if (activePage_ == 2)
        DrawSettingsPage();
    else
        DrawVisualsCard(margin, cardY, cardWidth, cardHeight);
}

void App::DrawSettingsPage()
{
    const float x = UI::SidebarWidth + 12.0f;
    const float y = 42.0f;
    const float w = static_cast<float>(UI::Width) - x - 12.0f;
    const float h = static_cast<float>(UI::Height) - y - 10.0f;
    Card(x, y, w, h, L"SETTINGS");

    const float innerLeft = x + 10.0f;
    const float innerRight = x + w - 10.0f;
    const float innerTop = y + 42.0f;
    const float innerBottom = y + h - 12.0f;
    const float innerWidth = innerRight - innerLeft;

    FillRound(innerLeft, innerTop, innerRight, innerBottom, 8.0f,
        MakeColor(0x090B0F, 0.92f));
    DrawRound(innerLeft, innerTop, innerRight, innerBottom, 8.0f,
        Theme::CardBorder, 0.8f);

    // scrollable content
    const float contentH = 430.0f;
    const float viewportH = MaxValue(1.0f, innerBottom - innerTop - 8.0f);
    settingsMaxScroll_ = MaxValue(0.0f, contentH - viewportH);
    settingsScroll_ = Clamp(settingsScroll_, 0.0f, settingsMaxScroll_);
    const float scroll = settingsScroll_;

    const D2D1_RECT_F clip = D2D1::RectF(innerLeft + 1.0f, innerTop + 4.0f,
        innerRight - 10.0f, innerBottom - 4.0f);
    target_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float base = innerTop + 7.0f - scroll;
    const float textR = innerRight - 18.0f;
    const float pad = innerLeft + 12.0f;
    const float fieldW = innerWidth - 28.0f;

    DrawTextLine(L"THEME", pad, base, textR, base + 22.0f, Theme::Text, titleFont_);
    ColorRow(L"Accent colour", accentColor_, pad, base + 28.0f, fieldW);
    ColorRow(L"Window tint", backgroundColor_, pad, base + 59.0f, fieldW);

    Divider(pad, base + 97.0f, fieldW);

    DrawTextLine(L"PREVIEW", pad, base + 107.0f, textR, base + 130.0f, Theme::Text, titleFont_);
    Slider(L"Preview size", previewScale_, 0.55f, 1.15f, true,
        pad, base + 136.0f, fieldW, 12);
    DrawTextLine(L"The character preview stays inside the main window.",
        pad, base + 170.0f, textR, base + 191.0f, Theme::Muted, smallFont_);
    DrawTextLine(L"Preview appearance follows the active visual profile.",
        pad, base + 192.0f, textR, base + 213.0f, Theme::Muted, smallFont_);

    Divider(pad, base + 226.0f, fieldW);
    DrawTextLine(L"HOTKEYS", pad, base + 235.0f, textR, base + 257.0f, Theme::Text, titleFont_);
    KeybindControl(L"Open / close menu", menuToggleKey_,
        pad, base + 263.0f, fieldW);
    KeybindControl(L"Close program", exitKey_,
        pad, base + 298.0f, fieldW);

    {
        const float btnW = 140.0f;
        const float btnH = 32.0f;
        const float bx = pad;
        const float by = base + 343.0f;
        const bool hover = Hover(bx, by, bx + btnW, by + btnH);
        FillRound(bx, by, bx + btnW, by + btnH, 6.0f,
            hover ? MakeColor(0xB33A3A) : MakeColor(0x8B2E2E));
        DrawRound(bx, by, bx + btnW, by + btnH, 6.0f, Theme::CardBorder, 1.0f);
        DrawTextLine(L"Close program", bx, by, bx + btnW, by + btnH,
            Theme::Text, font_, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!colorPickerOpen_ && !dropdownOpen_ && hover && mousePressed_)
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

    DrawTextLine(L"Aimbot and RCS are disabled while this menu is open.",
        pad, base + 388.0f, textR, base + 409.0f, Theme::Muted, smallFont_);

    target_->PopAxisAlignedClip();

    // scrollbar
    if (settingsMaxScroll_ > 0.5f)
    {
        const float trackL = innerRight - 8.0f;
        const float trackR = innerRight - 3.0f;
        const float trackT = innerTop + 6.0f;
        const float trackB = innerBottom - 6.0f;
        const float trackH = MaxValue(1.0f, trackB - trackT);
        const float thumbH = MaxValue(28.0f, trackH * (viewportH / contentH));
        const float ratio = Clamp(settingsScroll_ / settingsMaxScroll_, 0.0f, 1.0f);
        const float thumbT = trackT + ratio * (trackH - thumbH);
        FillRound(trackL, trackT, trackR, trackB, 2.0f, MakeColor(0x1A1D24));
        FillRound(trackL, thumbT, trackR, thumbT + thumbH, 2.0f, AccentBright());
    }
}

void App::DrawShell(float width, float height)
{
    dropdownOverlay_.valid = false;
    dropdownOverlay_.items.clear();

    FillRect(0.0f, 0.0f, width, height, Theme::Window);
    FillRect(0.0f, 0.0f, width, UI::TopBarHeight, Theme::TopBar);
    FillRect(0.0f, UI::TopBarHeight - 0.5f, width, UI::TopBarHeight + 0.5f,
        Theme::Divider);

    // Compact purple MAOTAW mark and title.
    DrawLine(22.0f, 25.0f, 22.0f, 11.0f, AccentBright(), 5.0f);
    DrawLine(22.0f, 11.0f, 34.0f, 24.0f, AccentBright(), 5.0f);
    DrawLine(34.0f, 24.0f, 46.0f, 11.0f, AccentBright(), 5.0f);
    DrawLine(46.0f, 11.0f, 46.0f, 25.0f, AccentBright(), 5.0f);
    DrawTextLine(L"MAOTAW", 58.0f, 3.0f, 142.0f, 34.0f,
        Theme::Text, titleFont_);

    // Subtle purple strokes in the top-right header, matching the reference accent treatment.
    DrawLine(width - 205.0f, 36.0f, width - 150.0f, 2.0f,
        WithAlpha(accentColor_, 0.27f), 1.2f);
    DrawLine(width - 160.0f, 36.0f, width - 112.0f, 4.0f,
        WithAlpha(accentColor_, 0.18f), 1.0f);

    DrawSidebar(height);
    DrawCards();
    DrawDropdownOverlay();
    DrawRect(0.8f, 0.8f, width - 0.8f, height - 0.8f,
        WithAlpha(accentColor_, 0.55f), 1.1f);
    DrawColorPicker();
}

void App::SyncPreviewSettings()
{
    PreviewRenderer::Settings settings{};
    settings.characterScale = previewScale_;

    TeamVisuals& v = ActiveVisuals();
    // The single Enemy profile drives the embedded character preview.
    settings.espEnabled = v.enabled;
    settings.onlyVisible = false;
    settings.box = v.enabled && v.box.enabled;
    settings.boxStyle = v.boxStyle;
    settings.cornerBox = v.boxStyle == 2 || v.boxStyle == 3;
    settings.filled = v.box.enabled && v.boxFillEnabled;
    settings.boxOutlineEnabled = v.boxOutlineEnabled;
    settings.boxThickness = Clamp(v.boxThickness, 1.0f, 4.0f);
    settings.boxRounding = v.boxRounding;
    settings.boxFillOpacity = v.boxFillOpacity;

    settings.healthBar = v.enabled && v.health.enabled;
    settings.healthStyle = v.healthStyle;
    settings.healthAnimationMode = v.healthAnimationMode;
    settings.healthSide = v.healthSide;
    settings.healthBarWidth = 1.0f + v.healthWidth;

    settings.armorBar = v.enabled && v.armor.enabled;
    settings.armorSide = v.armorSide;
    settings.armorStyle = std::clamp(v.armor.thickness - 1, 0, 3);
    settings.armorBarWidth = 1.0f + v.armorWidth;

    settings.skeleton = false; // Deliberately never render a skeleton in this preview.
    settings.skeletonStyle = 0;
    settings.skeletonThickness = 1.0f;
    settings.snapline = v.enabled && v.snapline.enabled;
    settings.snaplineThickness = static_cast<float>(std::clamp(v.snapline.thickness, 1, 3));
    settings.trail = false; // Removed from the UI and preview.
    settings.trailThickness = 1.0f;
    settings.trailStyle = 0;
    settings.name = v.enabled && v.name.enabled;
    settings.weapon = v.enabled && v.weapon.enabled;
    settings.distance = v.enabled && v.distance.enabled;
    settings.maxDistance = v.maxDistance;
    settings.damage = false; // Keep floating damage labels disabled.
    settings.visualGlow = false; // Removed from the UI and preview.
    settings.visualGlowThickness = 0.0f;


    auto copyColour = [](const D2D1_COLOR_F& source, float destination[4])
    {
        destination[0] = source.r;
        destination[1] = source.g;
        destination[2] = source.b;
        destination[3] = source.a;
    };

    copyColour(accentColor_, settings.accent);
    copyColour(v.box.color, settings.boxColour);
    copyColour(v.boxFill, settings.filledColour);
    copyColour(v.boxOutline, settings.boxOutlineColour);
    copyColour(v.health.color, settings.healthColour);
    copyColour(v.healthSecondary, settings.healthSecondaryColour);
    copyColour(v.armor.color, settings.armorColour);
    copyColour(v.armorSecondary, settings.armorSecondaryColour);
    copyColour(v.skeleton.color, settings.skeletonColour);
    copyColour(v.snapline.color, settings.lineColour);
    copyColour(v.trail.color, settings.trailColour);
    copyColour(v.name.color, settings.nameColour);
    copyColour(v.weapon.color, settings.weaponColour);
    copyColour(v.distance.color, settings.distanceColour);
    copyColour(visualGlowColor_, settings.visualGlowColour);
    const D2D1_COLOR_F previewBackground = BlendColor(Theme::Window, backgroundColor_, 0.035f);
    copyColour(previewBackground, settings.backgroundColour);
    PreviewRenderer::SetSettings(settings);
}

void App::SyncRuntimeSettings()
{
    const TeamVisuals& source = enemyVisuals_;
    auto toRuntimeColor = [](const D2D1_COLOR_F& color)
    {
        HSV result{};
        ColorToHsv(color, result.h, result.s, result.v);
        return result;
    };
    auto copyElement = [&](const VisualElement& from,
                           MaotawRuntime::VisualElement& to)
    {
        to.enabled = from.enabled;
        to.color = toRuntimeColor(from.color);
        to.thickness = std::clamp(from.thickness, 1, 5);
    };

    MaotawRuntime::TeamVisuals live{};
    live.enabled = source.enabled;
    live.onlyVisible = false;

    copyElement(source.box, live.box);
    live.box.thickness = std::clamp(static_cast<int>(std::lround(source.boxThickness)), 1, 4);
    live.boxThickness = Clamp(source.boxThickness, 1.0f, 4.0f);
    live.boxFill = toRuntimeColor(source.boxFill);
    live.boxOutline = toRuntimeColor(source.boxOutline);
    live.boxOutlineEnabled = source.boxOutlineEnabled;
    live.boxStyle = ((source.boxStyle == 2 || source.boxStyle == 3) ? 2 : 0) +
                    (source.boxFillEnabled ? 1 : 0);
    live.boxFillOpacity = Clamp(source.boxFillOpacity, 0.0f, 1.0f);
    live.boxRounding = 0.0f;

    copyElement(source.health, live.health);
    live.healthSecondary = toRuntimeColor(source.healthSecondary);
    live.healthStyle = std::clamp(source.healthStyle, 0, 2);
    live.healthAnimationMode = std::clamp(source.healthAnimationMode, 0, 3);
    live.healthSide = std::clamp(source.healthSide, 0, 3);
    live.healthWidth = Clamp(source.healthWidth / 11.0f, 0.0f, 1.0f);

    copyElement(source.armor, live.armor);
    live.armorSecondary = toRuntimeColor(source.armorSecondary);
    live.armorSide = std::clamp(source.armorSide, 0, 3);
    live.armor.thickness = std::clamp(source.armor.thickness, 1, 4);
    live.armorWidth = Clamp(source.armorWidth / 11.0f, 0.0f, 1.0f);

    live.skeleton.enabled = source.skeleton.enabled;
    live.skeleton.color = toRuntimeColor(source.skeleton.color);
    live.skeleton.thickness = std::clamp(static_cast<int>(std::lround(source.skeletonThickness)), 1, 4);
    live.skeletonThickness = Clamp(source.skeletonThickness, 1.0f, 4.0f);
    live.skeletonStyle = std::clamp(source.skeleton.thickness - 1, 0, 3);
    copyElement(source.snapline, live.snapline);
    live.snapline.thickness = std::clamp(source.snapline.thickness, 1, 3);

    // Trail is intentionally not exposed in this ZIP's menu; keep it disabled.
    copyElement(source.trail, live.trail);
    live.trail.enabled = false;
    live.trailPersonality = std::clamp(source.trailPersonality, 0, 5);

    copyElement(source.name, live.name);
    copyElement(source.weapon, live.weapon);
    copyElement(source.distance, live.distance);

    // Runtime uses 20m + normalized*480m for its supported 20..500m range.
    live.maxDistance = Clamp((source.maxDistance - 20.0f) / 480.0f, 0.0f, 1.0f);

    // Publish a complete model at once; Tick() immediately snapshots these values
    // before updating the entity cache and drawing the next overlay frame.
    g_enemy = live;
    g_local.enabled = false;
    g_worldRadar = false;

    MaotawRuntime::g_aimAssist.enabled = aimEnabled_;
    MaotawRuntime::g_aimAssist.menuOpen = windowsVisible_;
    MaotawRuntime::g_aimAssist.teamCheck = teamCheck_;
    MaotawRuntime::g_aimAssist.visibleOnly = visibleOnly_;
    MaotawRuntime::g_aimAssist.targetBone = std::clamp(targetBone_, 0, 4);
    MaotawRuntime::g_aimAssist.activationKey = aimHoldKey_;
    MaotawRuntime::g_aimAssist.fovPixels = Clamp(aimFov_, 1.0f, 1000.0f);
    MaotawRuntime::g_aimAssist.smoothing = Clamp(aimSmooth_, 0.0f, 30.0f);
    MaotawRuntime::g_aimAssist.maxDistanceMeters = Clamp(aimDistance_, 10.0f, 1000.0f);
    MaotawRuntime::g_aimAssist.recoilEnabled = rcsEnabled_;
    MaotawRuntime::g_aimAssist.sensitivity = Clamp(rcsSensitivity_, 0.1f, 5.0f);

    // rcsLegit_ 0..100 → interpolate between Aggressive and Legit
    // 0   = full aggressive (fast, strong, no extra down bias)
    // 100 = max legit (smooth, softer, small down help)
    {
        const float t = Clamp(rcsLegit_, 0.0f, 100.0f) / 100.0f; // 0=agg, 1=legit
        const float smoothAgg = 2.2f,  smoothLeg = 4.8f;
        const float scaleAgg  = 2.18f, scaleLeg  = 1.95f;
        const float jitAgg    = 0.22f, jitLeg    = 0.40f;
        const float biasAgg   = 0.0f,  biasLeg   = 0.14f;

        MaotawRuntime::g_aimAssist.recoilSmoothing = smoothAgg + (smoothLeg - smoothAgg) * t;
        MaotawRuntime::g_aimAssist.recoilScale     = scaleAgg  + (scaleLeg  - scaleAgg)  * t;
        MaotawRuntime::g_aimAssist.recoilJitter    = jitAgg    + (jitLeg    - jitAgg)    * t;
        MaotawRuntime::g_aimAssist.downwardBias    = biasAgg   + (biasLeg   - biasAgg)   * t;
    }
}

void App::Render()
{
    if (FAILED(CreateDeviceResources()))
        return;

    RECT client{};
    GetClientRect(hwnd_, &client);
    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);

    if (!mouseDown_)
        activeSlider_ = -1;

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(Theme::Window);

    DrawShell(width, height);
    SaveAutoConfigIfChanged();
    SyncPreviewSettings();

    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();

    mousePressed_ = false;
}

LRESULT CALLBACK App::StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* app = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
    }
    else
    {
        app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    return app ? app->WindowProc(message, wParam, lParam)
        : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
    {
        SyncRuntimeSettings();
        MaotawRuntime::Tick();

        const ULONGLONG now = GetTickCount64();
        if (windowsVisible_ && now >= nextUiRefresh_)
        {
            // Keep the menu above the transparent game overlay while it is open.
            SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            // Only invalidate the options side — never the character child area.
            RECT optionsRect{ 0, 0, UI::Width, UI::Height };
            InvalidateRect(hwnd_, &optionsRect, FALSE);
            nextUiRefresh_ = now + 16;
        }
        return 0;
    }

    case WM_MOVE:
        // Preview is a child of the main window; it moves automatically.
        return 0;

    case WM_NCHITTEST:
    {
        const LRESULT result = DefWindowProcW(hwnd_, message, wParam, lParam);
        if (result != HTCLIENT)
            return result;

        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd_, &pt);

        RECT client{};
        GetClientRect(hwnd_, &client);

        const LONG totalWidth = static_cast<LONG>(UI::Width + UI::PreviewWidth);
        if (pt.y >= 0 && pt.y < static_cast<LONG>(UI::DragHeight) &&
            pt.x >= 0 && pt.x < totalWidth)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEMOVE:
        mouse_.x = GET_X_LPARAM(lParam);
        mouse_.y = GET_Y_LPARAM(lParam);
        if (featureListDragging_)
        {
            const float trackTop = UI::VisualContentTop + UI::FeatureListTopOffset;
            const float trackBottom = static_cast<float>(UI::Height) - UI::FeatureListBottomInset;
            const float trackH = MaxValue(1.0f, trackBottom - trackTop);
            const float viewportH = trackH + 1.0f;
            const float contentH = 27.0f * 7.0f;
            const float thumbH = MaxValue(24.0f, trackH * viewportH / contentH);
            const float ratio = Clamp((static_cast<float>(mouse_.y) - trackTop - featureScrollbarGrabOffset_) /
                MaxValue(1.0f, trackH - thumbH), 0.0f, 1.0f);
            featureListScroll_ = ratio * featureListMaxScroll_;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd_, &point);
        const float direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1.0f : 1.0f;
        const float listTop = UI::VisualContentTop + UI::FeatureListTopOffset;
        const float listBottom = static_cast<float>(UI::Height) - UI::FeatureListBottomInset + 1.0f;
        const float listLeft = UI::SidebarWidth + 12.0f;
        const float listRight = listLeft + 115.0f;
        if (Hit(static_cast<float>(point.x), static_cast<float>(point.y),
            listLeft, listTop, listRight, listBottom))
        {
            featureListScroll_ = Clamp(featureListScroll_ + direction * 27.0f,
                0.0f, featureListMaxScroll_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (activePage_ == 2)
        {
            settingsScroll_ = Clamp(settingsScroll_ + direction * 36.0f,
                0.0f, settingsMaxScroll_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        // Mouse buttons are valid activation keys just like keyboard keys.
        // The click that opened the field is ignored until its matching up.
        if (keybindCapture_)
        {
            if (!keybindIgnoreMouseUntilRelease_)
            {
                CompleteKeybindCapture(VK_LBUTTON);
                return 0;
            }
            return 0;
        }

        mouse_.x = GET_X_LPARAM(lParam);
        mouse_.y = GET_Y_LPARAM(lParam);

        // Start dragging the feature-list scrollbar from its far-right track.
        if (mouse_.x >= UI::Width - 6 && mouse_.x <= UI::Width &&
            mouse_.y >= UI::VisualContentTop + UI::FeatureListTopOffset &&
            mouse_.y <= UI::Height - UI::FeatureListBottomInset)
        {
            const float trackTop = UI::VisualContentTop + UI::FeatureListTopOffset;
            const float trackBottom = static_cast<float>(UI::Height) - UI::FeatureListBottomInset;
            const float trackH = MaxValue(1.0f, trackBottom - trackTop);
            const float viewportH = trackH + 1.0f;
            const float contentH = 27.0f * 7.0f;
            const float thumbH = MaxValue(24.0f, trackH * viewportH / contentH);
            const float ratio = featureListMaxScroll_ <= 0.0f ? 0.0f
                : Clamp(featureListScroll_ / featureListMaxScroll_, 0.0f, 1.0f);
            const float thumbTop = trackTop + ratio * MaxValue(0.0f, trackH - thumbH);
            featureListDragging_ = true;
            featureScrollbarGrabOffset_ = (static_cast<float>(mouse_.y) >= thumbTop &&
                static_cast<float>(mouse_.y) <= thumbTop + thumbH)
                ? static_cast<float>(mouse_.y) - thumbTop : thumbH * 0.5f;
        }



        if (!colorPickerOpen_ && dropdownOpen_ && dropdownOverlay_.valid)
        {
            const float popupBottom = dropdownOverlay_.top +
                dropdownOverlay_.itemHeight * static_cast<float>(dropdownOverlay_.items.size());

            const bool insideButton = Hit(
                static_cast<float>(mouse_.x), static_cast<float>(mouse_.y),
                dropdownOverlay_.buttonLeft, dropdownOverlay_.buttonTop,
                dropdownOverlay_.buttonRight, dropdownOverlay_.buttonBottom);

            const bool insidePopup = Hit(
                static_cast<float>(mouse_.x), static_cast<float>(mouse_.y),
                dropdownOverlay_.left, dropdownOverlay_.top,
                dropdownOverlay_.right, popupBottom);

            if (!insideButton && !insidePopup)
            {
                dropdownOpen_ = false;
                dropdownOverlay_ = DropdownOverlay{};
                mousePressed_ = false; // Consume the click: never activate a control underneath.
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        }

        SetCapture(hwnd_);
        mouseDown_ = true;
        mousePressed_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
        mouseDown_ = false;
        if (keybindCapture_)
            keybindIgnoreMouseUntilRelease_ = false;
        featureListDragging_ = false;

        // End color-palette/hue dragging immediately. Without this reset,
        // the next click on Apply could still be treated as a palette drag.
        // Because Apply is below the palette, the value was clamped to 0,
        // which changed the selected color to black.
        activeColorArea_ = -1;

        ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;


    case WM_RBUTTONDOWN:
        if (keybindCapture_ && !keybindIgnoreMouseUntilRelease_)
        {
            CompleteKeybindCapture(VK_RBUTTON);
            return 0;
        }
        if (keybindCapture_)
            return 0;
        break;

    case WM_MBUTTONDOWN:
        if (keybindCapture_ && !keybindIgnoreMouseUntilRelease_)
        {
            CompleteKeybindCapture(VK_MBUTTON);
            return 0;
        }
        if (keybindCapture_)
            return 0;
        break;

    case WM_XBUTTONDOWN:
        if (keybindCapture_)
        {
            if (!keybindIgnoreMouseUntilRelease_)
            {
                const WORD button = GET_XBUTTON_WPARAM(wParam);
                CompleteKeybindCapture(button == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);
            }
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const int virtualKey = static_cast<int>(wParam);
        const bool isRepeat = (lParam & (1LL << 30)) != 0;

        if (keybindCapture_)
        {
            if (virtualKey == VK_ESCAPE)
            {
                keybindCapture_ = false;
                keybindTarget_ = nullptr;
                keybindIgnoreMouseUntilRelease_ = false;
            }
            else if (virtualKey == VK_BACK)
            {
                if (keybindTarget_)
                    *keybindTarget_ = 0;
                keybindCapture_ = false;
                keybindTarget_ = nullptr;
                keybindIgnoreMouseUntilRelease_ = false;
            }
            else if (!IsModifierKey(virtualKey))
            {
                CompleteKeybindCapture(virtualKey);
            }

            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        if (virtualKey == (menuToggleKey_ & 0xFF) && !isRepeat)
        {
            SaveAutoConfigIfChanged(true);
            windowsVisible_ = !windowsVisible_;
            ShowWindow(hwnd_, windowsVisible_ ? SW_SHOW : SW_HIDE);
            if (windowsVisible_)
                SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (sideHwnd_)
                ShowWindow(sideHwnd_, windowsVisible_ ? SW_SHOW : SW_HIDE);
            SyncRuntimeSettings(); // updates menuOpen so aim stops
            return 0;
        }

        if (virtualKey == (exitKey_ & 0xFF) && !isRepeat)
        {
            SaveAutoConfigIfChanged(true);
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return 0;
        }

        if (virtualKey == VK_ESCAPE)
        {
            if (colorPickerOpen_ && pickerTarget_)
            {
                *pickerTarget_ = pickerOriginal_;
                colorPickerOpen_ = false;
                pickerTarget_ = nullptr;
                activeColorArea_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            else
            {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
            return 0;
        }

        return 0;
    }

    case WM_HOTKEY:
        if (wParam == 2) // menu toggle
        {
            SaveAutoConfigIfChanged(true);
            windowsVisible_ = !windowsVisible_;
            ShowWindow(hwnd_, windowsVisible_ ? SW_SHOW : SW_HIDE);
            if (sideHwnd_)
                ShowWindow(sideHwnd_, windowsVisible_ ? SW_SHOW : SW_HIDE);
            if (windowsVisible_)
            {
                SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetForegroundWindow(hwnd_);
            }
            SyncRuntimeSettings();
        }
        else if (wParam == 3) // exit
        {
            SaveAutoConfigIfChanged(true);
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_SIZE:
        if (target_)
            target_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd_, &ps);
        Render();
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_DESTROY:
        SaveAutoConfigIfChanged(true);
        KillTimer(hwnd_, 1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    App app;
    if (!app.Initialize(instance))
    {
        MessageBoxW(nullptr, L"Failed to initialize Direct2D UI.", L"Error", MB_OK | MB_ICONERROR);
        app.Shutdown();
        return 1;
    }

    const int result = app.Run();
    app.Shutdown();
    return result;
}