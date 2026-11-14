#pragma once

// Runtime-mutable schema fields. Defaults match the 2026-11-12 dump.
// Values can be overridden at startup from output/offsets.ini next to the exe.

#include <cstddef>

namespace cs2_dumper {
namespace schemas {
namespace client_dll {

namespace CBasePlayerController {
    inline std::ptrdiff_t m_iszPlayerName = 0x6FC;
}

namespace CCSPlayerController {
    inline std::ptrdiff_t m_hPlayerPawn = 0x92C;
}

namespace CPlayer_WeaponServices {
    inline std::ptrdiff_t m_hActiveWeapon = 0x60;
}

namespace CSkeletonInstance {
    inline std::ptrdiff_t m_modelState = 0x140;
}

namespace C_AttributeContainer {
    inline std::ptrdiff_t m_Item = 0x50;
}

namespace C_BaseEntity {
    inline std::ptrdiff_t m_pGameSceneNode = 0x330;
    inline std::ptrdiff_t m_iHealth = 0x34C;
    inline std::ptrdiff_t m_iTeamNum = 0x3E7;
}

namespace C_BasePlayerPawn {
    inline std::ptrdiff_t m_pWeaponServices = 0x12F0;
    inline std::ptrdiff_t m_vOldOrigin = 0x14A4;
}

namespace C_CSPlayerPawn {
    inline std::ptrdiff_t m_ArmorValue = 0x1F00;
    inline std::ptrdiff_t m_iShotsFired = 0x1EE8;
    inline std::ptrdiff_t m_pAimPunchServices = 0x1598;
}

namespace C_CSPlayerPawnBase {
    inline std::ptrdiff_t m_entitySpottedState = 0x1EB8;
}

// not always in dumps — keep default; loader can still override if present
namespace CCSPlayer_AimPunchServices {
    inline std::ptrdiff_t m_aimPunchCache = 0x88;
}

namespace C_EconEntity {
    inline std::ptrdiff_t m_AttributeManager = 0x1290;
}

namespace C_EconItemView {
    inline std::ptrdiff_t m_iItemDefinitionIndex = 0x1BA;
}

namespace EntitySpottedState_t {
    inline std::ptrdiff_t m_bSpotted = 0x8;
}

// extras used by aimbot path (often not in schema dump)
namespace AimExtras {
    inline std::ptrdiff_t m_bDormant = 0x103;
    inline std::ptrdiff_t m_vecViewOffset = 0xF60;
}

} // namespace client_dll
} // namespace schemas
} // namespace cs2_dumper
