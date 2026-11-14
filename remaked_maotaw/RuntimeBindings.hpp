#pragma once

// Shared menu/runtime settings for the CS2 overlay and optional aim/recoil controls.
// The menu's own TeamVisuals model remains unchanged and is copied into this
// runtime model by App::SyncRuntimeSettings().
struct HSV {
    float h = 0.0f;
    float s = 1.0f;
    float v = 1.0f;
};

namespace MaotawRuntime {
    struct VisualElement {
        bool enabled = false;
        HSV color{};
        int thickness = 1;
    };

    struct AimAssistSettings {
        bool enabled = false;
        bool menuOpen = false;   // true while menu window is visible; disables aim
        bool recoilEnabled = true;
        bool teamCheck = true;
        bool visibleOnly = false;
        int targetBone = 0;              // 0 Head, 1 Neck, 2 Chest, 3 Stomach, 4 Closest
        int activationKey = 0x01;        // VK_LBUTTON, optional Ctrl/Alt/Shift flags
        float fovPixels = 55.0f;
        float smoothing = 5.5f;
        float maxDistanceMeters = 250.0f;
        float recoilSmoothing = 3.5f;
        float recoilJitter = 0.4f;
        float downwardBias = 0.2f;
        float sensitivity = 2.11f;
        float recoilScale = 2.0f;
    };

    inline AimAssistSettings g_aimAssist{};

    struct TeamVisuals {
        bool enabled = false;
        bool onlyVisible = false;
        VisualElement box{};
        float boxThickness = 2.0f;
        HSV boxFill{};
        HSV boxOutline{};
        bool boxOutlineEnabled = true;
        VisualElement health{};
        VisualElement armor{};
        HSV healthSecondary{};
        HSV armorSecondary{};
        VisualElement skeleton{};
        float skeletonThickness = 1.0f;
        int skeletonStyle = 0;
        VisualElement snapline{};
        VisualElement trail{};
        VisualElement name{};
        VisualElement weapon{};
        VisualElement distance{};
        int boxStyle = 0;
        float boxFillOpacity = 0.20f;
        float boxRounding = 0.0f;
        int healthStyle = 0;
        int healthAnimationMode = 1;
        int trailPersonality = 0;
        int healthSide = 0;
        int armorSide = 1;
        float maxDistance = 1.0f;
        float healthWidth = 0.0f;
        float armorWidth = 0.0f;
    };
    inline TeamVisuals MakeDisabledLocalVisuals() {
        TeamVisuals value{};
        value.enabled = false;
        return value;
    }
}

// Runtime.cpp-style globals expected by Runtime.hpp. The local profile is
// intentionally disabled; the existing menu controls the enemy profile only.
inline MaotawRuntime::TeamVisuals g_local = MaotawRuntime::MakeDisabledLocalVisuals();
inline MaotawRuntime::TeamVisuals g_enemy{};
inline bool g_worldRadar = false;
inline HSV g_worldRadarColor{ 0.28f, 0.70f, 0.90f };
inline float g_worldRadarRange = 40.0f / 480.0f;
inline float g_worldRadarSize = 60.0f / 180.0f;
inline int g_overlayFpsMode = 6; // Display refresh rate.

#include "Runtime.hpp"
