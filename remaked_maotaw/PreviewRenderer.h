#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace PreviewRenderer
{
    struct Settings
    {
        float characterScale = 1.00f;
        bool espEnabled = true;
        bool onlyVisible = false;
        bool box = true;
        bool boxOutlineEnabled = true;
        int boxStyle = 0;
        bool cornerBox = false;
        bool filled = true;
        bool healthBar = true;
        bool armorBar = false;
        bool name = true;
        bool distance = false;
        bool snapline = false;
        bool skeleton = false;
        bool trail = false;
        bool weapon = false;
        bool damage = false;
        bool visualGlow = false;
        float visualGlowThickness = 2.0f;
        float boxThickness = 1.2f;
        float boxRounding = 4.0f;
        float boxFillOpacity = 0.14f;
        float snaplineThickness = 1.0f;
        float skeletonThickness = 1.0f;
        float trailThickness = 1.0f;
        int healthStyle = 1;
        int healthAnimationMode = 1;
        int skeletonStyle = 0;
        int healthSide = 0;
        int armorSide = 1;
        int armorStyle = 0;
        int trailStyle = 0;
        float healthBarWidth = 6.0f;
        float armorBarWidth = 6.0f;
        float maxDistance = 500.0f;
        bool radarEnabled = false;
        float radarRange = 60.0f;
        float radarSize = 140.0f;

        float accent[4] = { 0.70f, 0.086f, 0.133f, 1.0f };
        float boxColour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float boxOutlineColour[4] = { 0.02f, 0.02f, 0.02f, 1.0f };
        float filledColour[4] = { 0.70f, 0.086f, 0.133f, 1.0f };
        float healthColour[4] = { 0.258f, 0.839f, 0.420f, 1.0f };
        float healthSecondaryColour[4] = { 0.258f, 0.839f, 0.420f, 1.0f };
        float armorColour[4] = { 0.35f, 0.62f, 0.95f, 1.0f };
        float armorSecondaryColour[4] = { 0.35f, 0.62f, 0.95f, 1.0f };
        float healthBackColour[4] = { 0.02f, 0.025f, 0.03f, 1.0f };
        float damageColour[4] = { 1.0f, 0.25f, 0.31f, 1.0f };
        float visualGlowColour[4] = { 0.70f, 0.086f, 0.133f, 1.0f };
        float lineColour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float skeletonColour[4] = { 0.835f, 0.859f, 1.0f, 1.0f };
        float nameColour[4] = { 0.95f, 0.95f, 0.96f, 1.0f };
        float weaponColour[4] = { 0.86f, 0.87f, 0.90f, 1.0f };
        float distanceColour[4] = { 0.72f, 0.75f, 0.79f, 1.0f };
        float radarColour[4] = { 0.25f, 0.90f, 0.55f, 1.0f };
        float trailColour[4] = { 0.80f, 0.45f, 0.95f, 1.0f };
        float backgroundColour[4] = { 0.004f, 0.007f, 0.011f, 1.0f };
    };

    bool Initialize(HWND window);
    void SetSettings(const Settings& settings);
    void Render();
    void Shutdown();
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled);
}
