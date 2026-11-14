#include "PreviewRenderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <random>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

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
#include "EmbeddedCharacterMesh.h"

namespace PreviewRenderer
{
namespace
{
    struct alignas(16) Constants
    {
        XMFLOAT4X4 worldViewProjection;
        XMFLOAT4X4 world;
        XMFLOAT4 colour;
        float outlinePass;
        float outlineWidthPixels;
        float viewportWidth;
        float viewportHeight;
    };
    static_assert(sizeof(Constants) % 16 == 0, "Constants must be 16-byte aligned");

    struct GlowLayer { float widthMultiplier; float alpha; };
    static constexpr std::array<GlowLayer, 7> glowLayers{{
        { 4.20f, 0.035f }, { 3.45f, 0.055f }, { 2.80f, 0.085f },
        { 2.20f, 0.130f }, { 1.68f, 0.210f }, { 1.35f, 0.380f },
        { 1.00f, 0.980f }
    }};

    HWND g_window = nullptr;
    float g_displayHealth = 100.0f;
    float g_healthVelocity = 0.0f;
    float g_targetHealth = 100.0f;
    float g_nextDamageAt = 1.0f;
    int g_lastDamage = 0;
    float g_damageStartedAt = -10.0f;
    std::mt19937 g_rng{ 0x4D414F54u };
    UINT g_width = 260, g_height = 470;
    Settings g_settings{};
    float g_time = 0.0f;

    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext> g_context;
    ComPtr<IDXGISwapChain> g_swapChain;
    ComPtr<ID3D11RenderTargetView> g_renderTarget;
    ComPtr<ID3D11Texture2D> g_depthTexture;
    ComPtr<ID3D11DepthStencilView> g_depthView;
    ComPtr<ID3D11VertexShader> g_vertexShader;
    ComPtr<ID3D11PixelShader> g_pixelShader;
    ComPtr<ID3D11InputLayout> g_inputLayout;
    ComPtr<ID3D11Buffer> g_vertexBuffer, g_indexBuffer, g_constantBuffer;
    ComPtr<ID3D11RasterizerState> g_rasterizer;
    ComPtr<ID3D11RasterizerState> g_outlineRasterizer;
    ComPtr<ID3D11DepthStencilState> g_depthState;
    ComPtr<ID3D11DepthStencilState> g_outlineDepthState;
    ComPtr<ID3D11BlendState> g_alphaBlend;
    ComPtr<ID3D11BlendState> g_colourWriteDisabledBlend;
    ComPtr<ID3D11Texture2D> g_albedoTexture;
    ComPtr<ID3D11ShaderResourceView> g_albedoSRV;
    ComPtr<ID3D11SamplerState> g_sampler;

    ComPtr<ID2D1Factory1> g_d2dFactory;
    ComPtr<ID2D1Device> g_d2dDevice;
    ComPtr<ID2D1DeviceContext> g_d2dContext;
    ComPtr<ID2D1Bitmap1> g_d2dTarget;
    ComPtr<ID2D1SolidColorBrush> g_brush;
    ComPtr<IDWriteFactory> g_writeFactory;
    ComPtr<IDWriteTextFormat> g_text, g_small, g_bold;

    D2D1_COLOR_F Colour(const float c[4], float alpha = -1.0f)
    {
        return D2D1::ColorF(c[0], c[1], c[2], alpha < 0.0f ? c[3] : alpha);
    }

    D2D1_COLOR_F MixColour(const float first[4], const float second[4], float amount)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        const float inverse = 1.0f - amount;
        return D2D1::ColorF(
            first[0] * inverse + second[0] * amount,
            first[1] * inverse + second[1] * amount,
            first[2] * inverse + second[2] * amount,
            first[3] * inverse + second[3] * amount);
    }

    HRESULT Compile(const char* source, const char* entry, const char* target, ComPtr<ID3DBlob>& blob)
    {
        ComPtr<ID3DBlob> errors;
        return D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
            entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &errors);
    }

    bool CreateD2DTarget()
    {
        g_d2dTarget.Reset();
        ComPtr<IDXGISurface> surface;
        if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
        if (FAILED(g_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, &g_d2dTarget))) return false;
        g_d2dContext->SetTarget(g_d2dTarget.Get());
        return true;
    }

    bool CreateSizeResources()
    {
        if (!g_swapChain) return false;
        g_d2dContext->SetTarget(nullptr);
        g_d2dTarget.Reset(); g_renderTarget.Reset(); g_depthView.Reset(); g_depthTexture.Reset();
        RECT rc{}; GetClientRect(g_window, &rc);
        g_width = MaxValue<UINT>(1u, static_cast<UINT>(rc.right - rc.left));
        g_height = MaxValue<UINT>(1u, static_cast<UINT>(rc.bottom - rc.top));
        if (FAILED(g_swapChain->ResizeBuffers(0, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0))) return false;
        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
        if (FAILED(g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderTarget))) return false;
        D3D11_TEXTURE2D_DESC dd{}; dd.Width=g_width; dd.Height=g_height; dd.MipLevels=1; dd.ArraySize=1;
        dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count=1; dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(g_device->CreateTexture2D(&dd, nullptr, &g_depthTexture))) return false;
        if (FAILED(g_device->CreateDepthStencilView(g_depthTexture.Get(), nullptr, &g_depthView))) return false;
        return CreateD2DTarget();
    }

    void Line(float x1,float y1,float x2,float y2,D2D1_COLOR_F c,float width=1.0f)
    { g_brush->SetColor(c); g_d2dContext->DrawLine({x1,y1},{x2,y2},g_brush.Get(),width); }
    void Rect(float l,float t,float r,float b,D2D1_COLOR_F c,float width=1.0f)
    { g_brush->SetColor(c); g_d2dContext->DrawRectangle({l,t,r,b},g_brush.Get(),width); }
    void Fill(float l,float t,float r,float b,D2D1_COLOR_F c)
    { g_brush->SetColor(c); g_d2dContext->FillRectangle({l,t,r,b},g_brush.Get()); }
    void Text(const wchar_t* s,float l,float t,float r,float b,D2D1_COLOR_F c,IDWriteTextFormat* f,DWRITE_TEXT_ALIGNMENT align)
    {
        f->SetTextAlignment(align); g_brush->SetColor(c);
        g_d2dContext->DrawTextW(s, static_cast<UINT32>(wcslen(s)), f, {l,t,r,b}, g_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void CornerBox(float l, float t, float r, float b, D2D1_COLOR_F c,
        float rounding, float thickness)
    {
        const float amount = std::clamp(rounding, 0.0f, 10.0f) / 10.0f;
        const float x = (r - l) * (0.18f + amount * 0.10f);
        const float y = (b - t) * (0.13f + amount * 0.07f);
        Line(l, t, l + x, t, c, thickness); Line(l, t, l, t + y, c, thickness);
        Line(r - x, t, r, t, c, thickness); Line(r, t, r, t + y, c, thickness);
        Line(l, b - y, l, b, c, thickness); Line(l, b, l + x, b, c, thickness);
        Line(r, b - y, r, b, c, thickness); Line(r - x, b, r, b, c, thickness);
    }

    void DrawOverlay()
    {
        g_d2dContext->BeginDraw();
        g_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());

        g_brush->SetColor(D2D1::ColorF(0.20f, 0.15f, 0.26f, 0.92f));
        g_d2dContext->DrawLine({ 0.7f, 0.0f }, { 0.7f, static_cast<float>(g_height) }, g_brush.Get(), 0.8f);

        const float scale = std::clamp(g_settings.characterScale, 0.55f, 1.15f);
        const float boxW = 158.0f * scale;
        const float boxH = 320.0f * scale;
        const float cx = g_width * 0.5f;
        const float cy = g_height * 0.51f;
        const float l = cx - boxW * 0.5f;
        const float r = cx + boxW * 0.5f;
        const float t = cy - boxH * 0.5f;
        const float b = cy + boxH * 0.5f;


        // The demo character is always visible in this preview. In the actual
        // game runtime, visibility would come from scene/occlusion data; this
        // preview therefore applies Only Visible to a known-visible sample.
        constexpr bool sampleActorVisible = true;
        const bool drawSampleEsp = g_settings.espEnabled &&
            (!g_settings.onlyVisible || sampleActorVisible);

        if (drawSampleEsp)
        {
            if (g_settings.box && g_settings.filled)
                Fill(l, t, r, b, Colour(g_settings.filledColour,
                    std::clamp(g_settings.boxFillOpacity, 0.0f, 1.0f)));
            if (g_settings.box)
            {
                const float thickness = std::clamp(g_settings.boxThickness, 1.0f, 4.0f);
                if (g_settings.boxOutlineEnabled)
                {
                    if (g_settings.cornerBox)
                        CornerBox(l - 1.0f, t - 1.0f, r + 1.0f, b + 1.0f,
                            Colour(g_settings.boxOutlineColour), g_settings.boxRounding, thickness);
                    else
                        Rect(l - 1.0f, t - 1.0f, r + 1.0f, b + 1.0f,
                            Colour(g_settings.boxOutlineColour), thickness + 1.0f);
                }
                if (g_settings.cornerBox)
                    CornerBox(l, t, r, b, Colour(g_settings.boxColour), g_settings.boxRounding, thickness);
                else
                {
                    D2D1_COLOR_F boxColour = Colour(g_settings.boxColour);
                    if (g_settings.boxStyle == 4 || g_settings.boxStyle == 5)
                    {
                        const float hue = std::fmod(g_time * 0.12f, 1.0f);
                        const float h6 = hue * 6.0f;
                        const int sector = static_cast<int>(h6) % 6;
                        const float f = h6 - std::floor(h6);
                        const float q = 1.0f - f;
                        const float rgb[6][3] = {
                            {1.0f, f, 0.0f}, {q, 1.0f, 0.0f}, {0.0f, 1.0f, f},
                            {0.0f, q, 1.0f}, {f, 0.0f, 1.0f}, {1.0f, 0.0f, q}
                        };
                        boxColour = D2D1::ColorF(rgb[sector][0], rgb[sector][1], rgb[sector][2], 1.0f);
                    }
                    Rect(l, t, r, b, boxColour, thickness);
                }
            }
        }

        // Synthetic health/armor values are fixed demo data, never read from a game.
        if (g_time >= g_nextDamageAt)
        {
            std::uniform_int_distribution<int> damageDistribution(5, 22);
            std::uniform_real_distribution<float> delayDistribution(1.8f, 2.9f);
            g_lastDamage = damageDistribution(g_rng);
            g_targetHealth = MaxValue(0.0f, g_targetHealth - static_cast<float>(g_lastDamage));
            if (g_targetHealth <= 0.0f)
                g_targetHealth = 100.0f;
            g_damageStartedAt = g_time;
            g_nextDamageAt = g_time + delayDistribution(g_rng);
        }
        constexpr float previewDt = 1.0f / 60.0f;
        switch (std::clamp(g_settings.healthAnimationMode, 0, 3))
        {
        case 0: // Instant
            g_displayHealth = g_targetHealth;
            g_healthVelocity = 0.0f;
            break;
        case 2: // Delayed / slower follow
            g_displayHealth += (g_targetHealth - g_displayHealth) * 0.018f;
            break;
        case 3: // Spring
        {
            const float acceleration = (g_targetHealth - g_displayHealth) * 34.0f - g_healthVelocity * 11.0f;
            g_healthVelocity += acceleration * previewDt;
            g_displayHealth += g_healthVelocity * previewDt;
            break;
        }
        default: // Smooth
        {
            const float healthLerp = 1.0f - std::exp(-4.8f * previewDt);
            g_displayHealth += (g_targetHealth - g_displayHealth) * healthLerp;
            break;
        }
        }

        auto drawStatusBar = [&](float ratio, int side, float thickness,
                                 bool segmented, bool gradient,
                                 const float primary[4], const float secondary[4])
        {
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            thickness = std::clamp(thickness, 1.0f, 12.0f);
            float bl = l, bt = t, br = r, bb = b;
            if (side == 0) { br = l - 7.0f; bl = br - thickness; }
            else if (side == 1) { bl = r + 7.0f; br = bl + thickness; }
            else if (side == 2) { bb = t - 7.0f; bt = bb - thickness; }
            else { bt = b + 7.0f; bb = bt + thickness; }

            Fill(bl, bt, br, bb, Colour(g_settings.healthBackColour, 0.92f));
            const bool vertical = (side == 0 || side == 1);
            if (vertical)
            {
                const float filledTop = bb - (bb - bt) * ratio;
                if (segmented)
                {
                    constexpr int segments = 10;
                    for (int i = 0; i < segments; ++i)
                    {
                        const float segBottom = bb - (bb - bt) * static_cast<float>(i) / segments;
                        const float segTop = bb - (bb - bt) * static_cast<float>(i + 1) / segments + 1.6f;
                        if (segBottom > filledTop)
                        {
                            const float part = static_cast<float>(i) / (segments - 1);
                            const D2D1_COLOR_F colour = gradient
                                ? MixColour(secondary, primary, part)
                                : Colour(primary);
                            g_brush->SetColor(colour);
                            g_d2dContext->FillRectangle(
                                D2D1::RectF(bl + 1.0f, MaxValue(segTop, filledTop), br - 1.0f, segBottom - 0.5f),
                                g_brush.Get());
                        }
                    }
                }
                else if (gradient)
                {
                    constexpr int strips = 16;
                    for (int i = 0; i < strips; ++i)
                    {
                        const float top = bb - (bb - bt) * static_cast<float>(i + 1) / strips;
                        const float bottom = bb - (bb - bt) * static_cast<float>(i) / strips;
                        if (bottom <= filledTop) continue;
                        const float part = static_cast<float>(i) / (strips - 1);
                        g_brush->SetColor(MixColour(secondary, primary, part));
                        g_d2dContext->FillRectangle(D2D1::RectF(bl + 1.0f,
                            MaxValue(top, filledTop), br - 1.0f, bottom), g_brush.Get());
                    }
                }
                else
                {
                    Fill(bl + 1.0f, filledTop, br - 1.0f, bb - 0.8f, Colour(primary));
                }
            }
            else
            {
                const float filledRight = bl + (br - bl) * ratio;
                if (segmented)
                {
                    constexpr int segments = 10;
                    for (int i = 0; i < segments; ++i)
                    {
                        const float segLeft = bl + (br - bl) * static_cast<float>(i) / segments;
                        const float segRight = bl + (br - bl) * static_cast<float>(i + 1) / segments - 1.6f;
                        if (segLeft < filledRight)
                        {
                            const float part = static_cast<float>(i) / (segments - 1);
                            const D2D1_COLOR_F colour = gradient
                                ? MixColour(secondary, primary, part)
                                : Colour(primary);
                            g_brush->SetColor(colour);
                            g_d2dContext->FillRectangle(D2D1::RectF(segLeft + 0.5f, bt + 1.0f,
                                MinValue(segRight, filledRight), bb - 1.0f), g_brush.Get());
                        }
                    }
                }
                else if (gradient)
                {
                    constexpr int strips = 16;
                    for (int i = 0; i < strips; ++i)
                    {
                        const float left = bl + (br - bl) * static_cast<float>(i) / strips;
                        const float right = bl + (br - bl) * static_cast<float>(i + 1) / strips;
                        if (left >= filledRight) continue;
                        const float part = static_cast<float>(i) / (strips - 1);
                        g_brush->SetColor(MixColour(secondary, primary, part));
                        g_d2dContext->FillRectangle(D2D1::RectF(left, bt + 1.0f,
                            MinValue(right, filledRight), bb - 1.0f), g_brush.Get());
                    }
                }
                else
                {
                    Fill(bl + 0.8f, bt + 1.0f, filledRight, bb - 1.0f, Colour(primary));
                }
            }
        };

        if (drawSampleEsp)
        {
            if (g_settings.healthBar)
            {
                const float ratio = std::clamp(g_displayHealth / 100.0f, 0.0f, 1.0f);
                drawStatusBar(ratio, g_settings.healthSide, g_settings.healthBarWidth,
                    g_settings.healthStyle == 1, g_settings.healthStyle == 2,
                    g_settings.healthColour, g_settings.healthSecondaryColour);
                const std::wstring healthText = std::to_wstring(static_cast<int>(std::round(g_displayHealth)));
                if (g_settings.healthSide == 0 || g_settings.healthSide == 1)
                {
                    const float tx = g_settings.healthSide == 0 ? l - 39.0f : r + 8.0f + g_settings.healthBarWidth + 2.0f;
                    Text(healthText.c_str(), tx, t + 2.0f, tx + 28.0f, t + 18.0f,
                        D2D1::ColorF(0.92f, 0.94f, 0.97f, 1), g_small.Get(),
                        g_settings.healthSide == 0 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            }

            if (g_settings.armorBar)
            {
                // Fixed sample armor value for the preview only.
                drawStatusBar(0.72f, g_settings.armorSide, g_settings.armorBarWidth,
                    g_settings.armorStyle == 1 || g_settings.armorStyle == 3,
                    g_settings.armorStyle == 2,
                    g_settings.armorColour, g_settings.armorSecondaryColour);
            }

            if (g_settings.skeleton)
            {
                const float pulse = 0.72f + 0.28f * std::sin(g_time * 3.4f);
                const D2D1_COLOR_F baseColour = Colour(g_settings.skeletonColour);
                const D2D1_COLOR_F colour = D2D1::ColorF(baseColour.r, baseColour.g, baseColour.b,
                    g_settings.skeletonStyle == 3 ? pulse : 1.0f);
                const float w = std::clamp(g_settings.skeletonThickness, 1.0f, 4.0f);
                if (g_settings.skeletonStyle == 2)
                    Line(cx, t + boxH * 0.12f, cx, b - 5.0f,
                        D2D1::ColorF(baseColour.r, baseColour.g, baseColour.b, 0.22f), w + 3.0f);
                const float headY = t + boxH * 0.12f;
                const float neckY = t + boxH * 0.24f;
                const float chestY = t + boxH * 0.40f;
                const float hipY = t + boxH * 0.61f;
                const float shoulder = boxW * 0.23f;
                const float hip = boxW * 0.15f;
                Line(cx, headY, cx, hipY, colour, w);
                Line(cx, neckY, cx - shoulder, chestY, colour, w);
                Line(cx, neckY, cx + shoulder, chestY, colour, w);
                Line(cx - shoulder, chestY, cx - shoulder * 1.28f, hipY - 2.0f, colour, w);
                Line(cx + shoulder, chestY, cx + shoulder * 1.28f, hipY - 2.0f, colour, w);
                Line(cx, hipY, cx - hip, b - 5.0f, colour, w);
                Line(cx, hipY, cx + hip, b - 5.0f, colour, w);
                if (g_settings.skeletonStyle == 1)
                {
                    const float jointRadius = 2.0f + w * 0.55f;
                    const D2D1_POINT_2F joints[] = {
                        { cx, headY }, { cx, neckY }, { cx, chestY }, { cx, hipY },
                        { cx - shoulder, chestY }, { cx + shoulder, chestY },
                        { cx - shoulder * 1.28f, hipY - 2.0f },
                        { cx + shoulder * 1.28f, hipY - 2.0f },
                        { cx - hip, b - 5.0f }, { cx + hip, b - 5.0f }
                    };
                    g_brush->SetColor(colour);
                    for (const D2D1_POINT_2F& joint : joints)
                        g_d2dContext->FillEllipse(
                            D2D1::Ellipse(joint, jointRadius, jointRadius), g_brush.Get());
                }
            }

            if (g_settings.trail)
            {
                const int dots = 7;
                const float thickness = std::clamp(g_settings.trailThickness, 1.0f, 3.0f);
                float previousX = cx - boxW * 0.16f;
                float previousY = cy + boxH * 0.16f;
                for (int i = 0; i < dots; ++i)
                {
                    const float progress = static_cast<float>(i + 1) / dots;
                    const float alpha = g_settings.trailStyle == 1
                        ? (1.0f - progress) * 0.75f
                        : (g_settings.trailStyle == 4 ? 0.42f + 0.24f * std::sin(g_time * 3.0f + i) : 0.55f);
                    const float x = cx - boxW * 0.16f - progress * (22.0f + 7.0f * g_settings.trailStyle);
                    const float y = cy + boxH * 0.16f + std::sin(g_time * 1.7f + i * 0.55f) * 5.0f;
                    const D2D1_COLOR_F trailColour = Colour(g_settings.trailColour, std::clamp(alpha, 0.05f, 0.9f));
                    if (g_settings.trailStyle == 3)
                        Line(previousX, previousY, x, y, trailColour, thickness + 0.8f);
                    g_brush->SetColor(trailColour);
                    if (g_settings.trailStyle == 2 || g_settings.trailStyle == 4)
                        g_d2dContext->FillEllipse(D2D1::Ellipse({ x, y }, thickness + 1.0f, thickness + 1.0f), g_brush.Get());
                    else if (g_settings.trailStyle != 3)
                        g_d2dContext->FillEllipse(D2D1::Ellipse({ x, y }, thickness + 0.6f, thickness + 0.6f), g_brush.Get());
                    previousX = x;
                    previousY = y;
                }
            }

            if (g_settings.name)
                Text(L"PLAYER", l - 8.0f, t - 24.0f, r + 8.0f, t - 4.0f,
                    Colour(g_settings.nameColour), g_bold.Get(), DWRITE_TEXT_ALIGNMENT_CENTER);
            const bool showDistance = g_settings.distance && g_settings.maxDistance >= 42.0f;
            if (showDistance)
                Text(L"42 m  |  sample", l, b + 5.0f, r, b + 22.0f,
                    Colour(g_settings.distanceColour), g_small.Get(), DWRITE_TEXT_ALIGNMENT_CENTER);
            if (g_settings.weapon)
                Text(L"RIFLE  |  PREVIEW", l, b + (showDistance ? 22.0f : 5.0f), r,
                    b + (showDistance ? 40.0f : 23.0f),
                    Colour(g_settings.weaponColour), g_small.Get(), DWRITE_TEXT_ALIGNMENT_CENTER);
            if (g_settings.damage && g_lastDamage > 0)
            {
                const float phase = std::clamp((g_time - g_damageStartedAt) / 1.15f, 0.0f, 1.0f);
                const float smooth = phase * phase * (3.0f - 2.0f * phase);
                const float rise = smooth * 42.0f;
                const float alpha = std::clamp(1.0f - smooth, 0.0f, 1.0f);
                const std::wstring damageText = L"-" + std::to_wstring(g_lastDamage);
                Text(damageText.c_str(), r - 5.0f, t + 92.0f - rise, r + 58.0f,
                    t + 126.0f - rise, Colour(g_settings.damageColour, alpha),
                    g_bold.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            if (g_settings.snapline)
                Line(g_width * 0.5f, g_height - 4.0f, cx, b,
                    Colour(g_settings.lineColour),
                    std::clamp(g_settings.snaplineThickness, 1.0f, 3.0f));
        }

        if (g_settings.radarEnabled)
        {
            const float radarRadius = std::clamp(g_settings.radarSize * 0.12f, 10.0f, 29.0f);
            const float radarCx = static_cast<float>(g_width) - radarRadius - 9.0f;
            const float radarCy = radarRadius + 38.0f;
            const D2D1_COLOR_F radarColour = Colour(g_settings.radarColour, 0.88f);
            const D2D1_ELLIPSE radar = D2D1::Ellipse({ radarCx, radarCy }, radarRadius, radarRadius);
            g_brush->SetColor(D2D1::ColorF(0.015f, 0.02f, 0.028f, 0.78f));
            g_d2dContext->FillEllipse(radar, g_brush.Get());
            g_brush->SetColor(radarColour);
            g_d2dContext->DrawEllipse(radar, g_brush.Get(), 1.0f);
            Line(radarCx - radarRadius, radarCy, radarCx + radarRadius, radarCy,
                D2D1::ColorF(radarColour.r, radarColour.g, radarColour.b, 0.36f), 1.0f);
            Line(radarCx, radarCy - radarRadius, radarCx, radarCy + radarRadius,
                D2D1::ColorF(radarColour.r, radarColour.g, radarColour.b, 0.36f), 1.0f);
            g_brush->SetColor(D2D1::ColorF(0.92f, 0.94f, 0.97f, 1.0f));
            g_d2dContext->FillEllipse(D2D1::Ellipse({ radarCx, radarCy }, 2.0f, 2.0f), g_brush.Get());
            g_brush->SetColor(radarColour);
            g_d2dContext->FillEllipse(D2D1::Ellipse(
                { radarCx + radarRadius * 0.42f, radarCy - radarRadius * 0.22f }, 2.0f, 2.0f), g_brush.Get());
            g_d2dContext->FillEllipse(D2D1::Ellipse(
                { radarCx - radarRadius * 0.28f, radarCy + radarRadius * 0.34f }, 2.0f, 2.0f), g_brush.Get());
            const std::wstring rangeText = std::to_wstring(static_cast<int>(std::round(g_settings.radarRange))) + L"m";
            Text(rangeText.c_str(), radarCx - radarRadius, radarCy + radarRadius + 2.0f,
                radarCx + radarRadius, radarCy + radarRadius + 17.0f,
                radarColour, g_small.Get(), DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        // Draw the visible right and bottom edges of the shared parent frame.
        const D2D1_COLOR_F frameColour = D2D1::ColorF(0.55f, 0.30f, 0.76f, 0.72f);
        Line(static_cast<float>(g_width) - 1.0f, 4.0f,
            static_cast<float>(g_width) - 1.0f, static_cast<float>(g_height) - 10.0f,
            frameColour, 1.0f);
        Line(10.0f, static_cast<float>(g_height) - 1.0f,
            static_cast<float>(g_width) - 10.0f, static_cast<float>(g_height) - 1.0f,
            frameColour, 1.0f);

        g_d2dContext->EndDraw();
    }
}

void SetSettings(const Settings& settings) { g_settings = settings; }

bool Initialize(HWND window)
{
    g_window=window; RECT rc{}; GetClientRect(window,&rc);
    g_width = static_cast<UINT>(MaxValue<LONG>(1L, rc.right - rc.left));
    g_height = static_cast<UINT>(MaxValue<LONG>(1L, rc.bottom - rc.top));
    DXGI_SWAP_CHAIN_DESC swap{}; swap.BufferCount=2; swap.BufferDesc.Width=g_width; swap.BufferDesc.Height=g_height;
    swap.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.OutputWindow=window; swap.SampleDesc.Count=1; swap.Windowed=TRUE; swap.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level{};
    HRESULT deviceResult = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &swap, &g_swapChain, &g_device, &level, &g_context);
    // WARP fallback keeps the preview available on systems without a suitable GPU/device.
    if (FAILED(deviceResult))
    {
        deviceResult = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &swap, &g_swapChain, &g_device, &level, &g_context);
    }
    if (FAILED(deviceResult)) return false;

    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),&fo,
        reinterpret_cast<void**>(g_d2dFactory.GetAddressOf())))) return false;
    ComPtr<IDXGIDevice> dxgiDevice; if (FAILED(g_device.As(&dxgiDevice))) return false;
    if (FAILED(g_d2dFactory->CreateDevice(dxgiDevice.Get(),&g_d2dDevice))) return false;
    if (FAILED(g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&g_d2dContext))) return false;
    if (FAILED(g_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&g_brush))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf())))) return false;
    auto fmt=[&](float size,DWRITE_FONT_WEIGHT weight,ComPtr<IDWriteTextFormat>& out){
        if (FAILED(g_writeFactory->CreateTextFormat(L"Segoe UI",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-US",&out))) return false;
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); return true; };
    if (!fmt(11,DWRITE_FONT_WEIGHT_NORMAL,g_text)||!fmt(9,DWRITE_FONT_WEIGHT_NORMAL,g_small)||!fmt(11,DWRITE_FONT_WEIGHT_SEMI_BOLD,g_bold)) return false;
    if (!CreateSizeResources()) return false;

    static const char* shader=R"(
cbuffer Constants : register(b0)
{
    matrix worldViewProjection;
    matrix world;
    float4 objectColour;
    float outlinePass;
    float outlineWidthPixels;
    float viewportWidth;
    float viewportHeight;
};
Texture2D albedoMap : register(t0);
SamplerState albedoSampler : register(s0);
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct PSInput { float4 position : SV_POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 clipPosition = mul(float4(input.position, 1.0f), worldViewProjection);
    if (outlinePass > 0.5f && outlineWidthPixels > 0.0f)
    {
        const float normalProbeDistance = 0.05f;
        float4 normalClipPosition = mul(float4(input.position + normalize(input.normal) * normalProbeDistance, 1.0f), worldViewProjection);
        float safeClipW = abs(clipPosition.w) < 0.00001f ? (clipPosition.w < 0.0f ? -0.00001f : 0.00001f) : clipPosition.w;
        float safeNormalW = abs(normalClipPosition.w) < 0.00001f ? (normalClipPosition.w < 0.0f ? -0.00001f : 0.00001f) : normalClipPosition.w;
        float2 positionNdc = clipPosition.xy / safeClipW;
        float2 normalNdc = normalClipPosition.xy / safeNormalW;
        float safeViewportWidth = max(viewportWidth, 1.0f);
        float safeViewportHeight = max(viewportHeight, 1.0f);
        float2 projectedNormalPixels = float2((normalNdc.x-positionNdc.x)*safeViewportWidth*0.5f, -(normalNdc.y-positionNdc.y)*safeViewportHeight*0.5f);
        float projectedLength = length(projectedNormalPixels);
        if (projectedLength > 0.0001f)
        {
            float2 pixelDirection = projectedNormalPixels / projectedLength;
            float2 pixelOffset = pixelDirection * outlineWidthPixels;
            float2 ndcOffset = float2(pixelOffset.x * 2.0f / safeViewportWidth, -pixelOffset.y * 2.0f / safeViewportHeight);
            clipPosition.xy += ndcOffset * clipPosition.w;
        }
    }
    output.position = clipPosition;
    output.normal = normalize(mul(float4(input.normal,0),world).xyz);
    output.uv = input.uv;
    return output;
}
float4 PSMain(PSInput input) : SV_TARGET
{
    if (outlinePass > 0.5f) return objectColour;
    float3 albedo = albedoMap.Sample(albedoSampler, input.uv).rgb;
    float3 n=normalize(input.normal); float3 ld=normalize(float3(-.45,.75,-.55));
    float d=saturate(dot(n,ld)); float rim=pow(1-saturate(dot(n,float3(0,0,-1))),2.2);
    float3 c=albedo*(.22+d*.78); c+=float3(.12,.04,.05)*rim*0.55;
    return float4(c,1);
})";
    ComPtr<ID3DBlob> vs,ps; if(FAILED(Compile(shader,"VSMain","vs_4_0",vs))||FAILED(Compile(shader,"PSMain","ps_4_0",ps)))return false;
    if(FAILED(g_device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&g_vertexShader))||FAILED(g_device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&g_pixelShader)))return false;
    const D3D11_INPUT_ELEMENT_DESC il[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    if(FAILED(g_device->CreateInputLayout(il,3,vs->GetBufferPointer(),vs->GetBufferSize(),&g_inputLayout)))return false;
    D3D11_BUFFER_DESC vb{};vb.ByteWidth=sizeof(vertices);vb.Usage=D3D11_USAGE_IMMUTABLE;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA vd{};vd.pSysMem=vertices;if(FAILED(g_device->CreateBuffer(&vb,&vd,&g_vertexBuffer)))return false;
    D3D11_BUFFER_DESC ib{};ib.ByteWidth=sizeof(indices);ib.Usage=D3D11_USAGE_IMMUTABLE;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;D3D11_SUBRESOURCE_DATA id{};id.pSysMem=indices;if(FAILED(g_device->CreateBuffer(&ib,&id,&g_indexBuffer)))return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;if(FAILED(g_device->CreateBuffer(&cb,nullptr,&g_constantBuffer)))return false;

    // Compact base-color atlas (64px tiles from GLB).
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kAtlasWidth; td.Height = kAtlasHeight; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<unsigned char> rgba(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4);
        for (UINT y = 0; y < kAtlasHeight; ++y)
        {
            for (UINT x = 0; x < kAtlasWidth; ++x)
            {
                const size_t si = (static_cast<size_t>(y) * kAtlasWidth + x) * 3;
                const size_t di = (static_cast<size_t>(y) * kAtlasWidth + x) * 4;
                rgba[di + 0] = kAtlasRGB[si + 0];
                rgba[di + 1] = kAtlasRGB[si + 1];
                rgba[di + 2] = kAtlasRGB[si + 2];
                rgba[di + 3] = 255;
            }
        }
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = rgba.data();
        sd.SysMemPitch = kAtlasWidth * 4;
        if (FAILED(g_device->CreateTexture2D(&td, &sd, &g_albedoTexture))) return false;
        if (FAILED(g_device->CreateShaderResourceView(g_albedoTexture.Get(), nullptr, &g_albedoSRV))) return false;
        D3D11_SAMPLER_DESC samp{};
        samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samp.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(g_device->CreateSamplerState(&samp, &g_sampler))) return false;
    }
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode=D3D11_FILL_SOLID; rs.CullMode=D3D11_CULL_BACK; rs.FrontCounterClockwise=FALSE;
    rs.DepthClipEnable=TRUE; rs.ScissorEnable=TRUE;
    if(FAILED(g_device->CreateRasterizerState(&rs,&g_rasterizer)))return false;
    D3D11_RASTERIZER_DESC outlineRs=rs; outlineRs.CullMode=D3D11_CULL_FRONT;
    if(FAILED(g_device->CreateRasterizerState(&outlineRs,&g_outlineRasterizer)))return false;

    D3D11_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable=TRUE; ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(g_device->CreateDepthStencilState(&ds,&g_depthState)))return false;
    D3D11_DEPTH_STENCIL_DESC outlineDs=ds; outlineDs.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    if(FAILED(g_device->CreateDepthStencilState(&outlineDs,&g_outlineDepthState)))return false;

    D3D11_BLEND_DESC blend{}; blend.RenderTarget[0].BlendEnable=TRUE;
    blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA; blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD; blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(g_device->CreateBlendState(&blend,&g_alphaBlend)))return false;
    D3D11_BLEND_DESC depthOnly{}; depthOnly.RenderTarget[0].BlendEnable=FALSE; depthOnly.RenderTarget[0].RenderTargetWriteMask=0;
    if(FAILED(g_device->CreateBlendState(&depthOnly,&g_colourWriteDisabledBlend)))return false;
    SetTimer(window,2,16,nullptr); return true;
}

void Render()
{
    if(!g_context||!g_renderTarget)return;
    g_time+=0.016f;
    const float clear[4]={g_settings.backgroundColour[0],g_settings.backgroundColour[1],g_settings.backgroundColour[2],1.0f};
    g_context->OMSetRenderTargets(1,g_renderTarget.GetAddressOf(),g_depthView.Get());
    g_context->ClearRenderTargetView(g_renderTarget.Get(),clear);
    g_context->ClearDepthStencilView(g_depthView.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);

    D3D11_VIEWPORT vp{}; vp.Width=(float)g_width; vp.Height=(float)g_height; vp.MaxDepth=1;
    g_context->RSSetViewports(1,&vp);
    D3D11_RECT scissor{0,0,static_cast<LONG>(g_width),static_cast<LONG>(g_height)};
    g_context->RSSetScissorRects(1,&scissor);

    UINT stride=sizeof(Vertex),offset=0;
    g_context->IASetInputLayout(g_inputLayout.Get());
    g_context->IASetVertexBuffers(0,1,g_vertexBuffer.GetAddressOf(),&stride,&offset);
    g_context->IASetIndexBuffer(g_indexBuffer.Get(),DXGI_FORMAT_R32_UINT,0);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_vertexShader.Get(),nullptr,0);
    g_context->PSSetShader(g_pixelShader.Get(),nullptr,0);
    g_context->VSSetConstantBuffers(0,1,g_constantBuffer.GetAddressOf());
    g_context->PSSetConstantBuffers(0,1,g_constantBuffer.GetAddressOf());
    g_context->PSSetShaderResources(0,1,g_albedoSRV.GetAddressOf());
    g_context->PSSetSamplers(0,1,g_sampler.GetAddressOf());

    const float aspect=(float)g_width/(float)g_height;
    const float scale=std::clamp(g_settings.characterScale,0.55f,1.15f);
    const float rotation=g_time*0.45f;
    // Feet near bottom, body centered in the preview panel, lifted slightly.
    const XMMATRIX world=XMMatrixScaling(scale,scale,scale)*XMMatrixRotationY(rotation)*XMMatrixTranslation(0.0f, -0.12f, 0.0f);
    const XMMATRIX view=XMMatrixLookAtLH(XMVectorSet(0.0f, 0.78f, -4.55f, 1.0f), XMVectorSet(0.0f, 0.78f, 0.0f, 1.0f), XMVectorSet(0,1,0,0));
    const XMMATRIX proj=XMMatrixPerspectiveFovLH(XMConvertToRadians(28.5f),aspect,.05f,100);

    Constants constants{};
    XMStoreFloat4x4(&constants.worldViewProjection,XMMatrixTranspose(world*view*proj));
    XMStoreFloat4x4(&constants.world,XMMatrixTranspose(world));
    constants.viewportWidth=static_cast<float>(g_width);
    constants.viewportHeight=static_cast<float>(g_height);

    auto drawMesh=[&](bool outline,float widthPixels,const XMFLOAT4& colour)
    {
        constants.outlinePass=outline?1.0f:0.0f;
        constants.outlineWidthPixels=widthPixels;
        constants.colour=colour;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(SUCCEEDED(g_context->Map(g_constantBuffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
        {
            std::memcpy(mapped.pData,&constants,sizeof(constants));
            g_context->Unmap(g_constantBuffer.Get(),0);
            g_context->DrawIndexed(kEmbeddedIndexCount,0,0);
        }
    };

    const float blendFactor[4]={0,0,0,0};
    if(g_settings.visualGlow)
    {
        // 1) Depth-only normal mesh pre-pass.
        g_context->RSSetState(g_rasterizer.Get());
        g_context->OMSetDepthStencilState(g_depthState.Get(),0);
        g_context->OMSetBlendState(g_colourWriteDisabledBlend.Get(),blendFactor,0xFFFFFFFFu);
        drawMesh(false,0.0f,XMFLOAT4(.63f,.66f,.72f,1.0f));

        // 2) Screen-space inverted-hull layers, widest/weakest first.
        g_context->RSSetState(g_outlineRasterizer.Get());
        g_context->OMSetDepthStencilState(g_outlineDepthState.Get(),0);
        g_context->OMSetBlendState(g_alphaBlend.Get(),blendFactor,0xFFFFFFFFu);
        const float visibleThickness=std::clamp(g_settings.visualGlowThickness,0.5f,6.0f);
        for(const GlowLayer& layer:glowLayers)
        {
            drawMesh(true,visibleThickness*layer.widthMultiplier,
                XMFLOAT4(g_settings.visualGlowColour[0],g_settings.visualGlowColour[1],g_settings.visualGlowColour[2],layer.alpha));
        }
    }

    // 3) Normal lit character last, covering all interior outline fragments.
    g_context->RSSetState(g_rasterizer.Get());
    g_context->OMSetDepthStencilState(g_depthState.Get(),0);
    g_context->OMSetBlendState(g_alphaBlend.Get(),blendFactor,0xFFFFFFFFu);
    drawMesh(false,0.0f,XMFLOAT4(.63f,.66f,.72f,1.0f));
    g_context->OMSetBlendState(nullptr,blendFactor,0xFFFFFFFFu);

    DrawOverlay();
    // Present without blocking on vsync twice (main UI also runs ~60fps).
    // DO_NOT_WAIT avoids stalling if the previous frame is still presenting.
    g_swapChain->Present(0, 0);
}

void Shutdown()
{
    if (g_window)
        KillTimer(g_window, 2);
    if (g_d2dContext.Get())
        g_d2dContext->SetTarget(nullptr);

    g_bold.Reset();
    g_small.Reset();
    g_text.Reset();
    g_writeFactory.Reset();
    g_brush.Reset();
    g_d2dTarget.Reset();
    g_d2dContext.Reset();
    g_d2dDevice.Reset();
    g_d2dFactory.Reset();
    g_sampler.Reset();
    g_albedoSRV.Reset();
    g_albedoTexture.Reset();
    g_colourWriteDisabledBlend.Reset();
    g_alphaBlend.Reset();
    g_outlineDepthState.Reset();
    g_depthState.Reset();
    g_outlineRasterizer.Reset();
    g_rasterizer.Reset();
    g_constantBuffer.Reset();
    g_indexBuffer.Reset();
    g_vertexBuffer.Reset();
    g_inputLayout.Reset();
    g_pixelShader.Reset();
    g_vertexShader.Reset();
    g_depthView.Reset();
    g_depthTexture.Reset();
    g_renderTarget.Reset();
    g_swapChain.Reset();
    g_context.Reset();
    g_device.Reset();
    g_window = nullptr;
}

LRESULT HandleMessage(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,bool& handled)
{
    handled=true;
    switch(message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wParam == 2)
        {
            Render();
            return 0;
        }
        break;
    case WM_PAINT:
    {
        // Only validate the region. Rendering is driven solely by the timer
        // so we never double-Present in the same frame (main cause of flicker).
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (g_swapChain && wParam != SIZE_MINIMIZED)
            CreateSizeResources();
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        handled = false;
        break;
    }
    return 0;
}
}
