#include "stdafx.h"
#include "Ps2Tour.hpp"

#if defined(__PS2__) && defined(RELIVE_PS2_HOSTFS)

    #include "Abe.hpp"
    #include "Game.hpp"
    #include "Map.hpp"
    #include "PathData.hpp"
    #include "Sys_common.hpp"

extern PerLvlData gPerLvlData_561700[17]; // MainMenu.cpp: the game's level-select list

namespace {

constexpr s32 kEntries = 15;          // last two entries are the credits and the menu
constexpr s32 kFramesPerStop = 600;   // ~20 s of game steps
constexpr s32 kCheckAfterFrames = 90; // ~3 s after the jump: is the level up and the hero alive?

s32 sStop = -1;
s32 sFrames = 0;
bool sChecked = false;
bool sEnabled = false;
bool sInitialised = false;

void GoTo(const PerLvlData& entry)
{
    Abe* pHero = sActiveHero_5C1B68;

    // Same steps as the game's own level select (MainMenu, CheatLevelSelectLoading).
    pHero->SetUpdateDelay(1);
    gMap_5C3030.SetActiveCam_480D30(entry.field_4_level, entry.field_6_path, entry.field_8_camera, CameraSwapEffects::eInstantChange_0, 0, 0);

    const PathBlyRec* pPathData = Path_Get_Bly_Record_460F30(entry.field_4_level, entry.field_6_path);
    pHero->field_B8_xpos = FP_FromInteger(entry.field_C_abe_x_off - pPathData->field_4_pPathData->field_1A_abe_start_xpos);
    pHero->field_BC_ypos = FP_FromInteger(entry.field_E_abe_y_off - pPathData->field_4_pPathData->field_1C_abe_start_ypos);
    pHero->field_C2_lvl_number = entry.field_4_level;
    pHero->field_C0_path_number = static_cast<s16>(entry.field_6_path);

    const s16 scale = static_cast<s16>(entry.field_A_id);
    if (scale == -1)
    {
        pHero->field_CC_sprite_scale = FP_FromDouble(1.0);
        pHero->field_D6_scale = 1;
        pHero->field_20_animation.field_C_render_layer = Layer::eLayer_AbeMenu_32;
    }
    else if (scale == -2)
    {
        pHero->field_CC_sprite_scale = FP_FromDouble(0.5);
        pHero->field_D6_scale = 0;
        pHero->field_20_animation.field_C_render_layer = Layer::eLayer_AbeMenu_Half_13;
    }

    // Drop onto whatever floor is there.
    pHero->field_106_current_motion = 3;
    pHero->field_1AC_flags.Set(Abe::e1AC_Bit7_land_softly);
    pHero->field_100_pCollisionLine = nullptr;
    pHero->field_F8_LastLineYPos = pHero->field_BC_ypos;
}

} // namespace

void Ps2Tour_Frame()
{
    if (!sInitialised)
    {
        sInitialised = true;
        if (FILE* f = fopen("ps2tour.txt", "r"))
        {
            fclose(f);
            sEnabled = true;
            LOG_INFO("Tour: enabled, " << kEntries << " stops");
        }
    }
    if (!sEnabled || sStop >= kEntries)
    {
        return;
    }

    // Start once the first level is running with a hero.
    Abe* pHero = sActiveHero_5C1B68;
    if (gMap_5C3030.field_0_current_level == LevelIds::eMenu_0 || !pHero || pHero == spAbe_554D5C)
    {
        return;
    }

    if (++sFrames == kCheckAfterFrames && sStop >= 0)
    {
        const PerLvlData& entry = gPerLvlData_561700[sStop];
        const bool levelOk = gMap_5C3030.field_0_current_level == entry.field_4_level && gMap_5C3030.field_2_current_path == static_cast<s16>(entry.field_6_path);
        LOG_INFO("Tour: stop " << sStop + 1 << " '" << entry.field_0_display_name << "' " << (levelOk ? "OK" : "WRONG PLACE")
                 << " level " << static_cast<s32>(gMap_5C3030.field_0_current_level) << " path " << gMap_5C3030.field_2_current_path
                 << " cam " << gMap_5C3030.field_4_current_camera << " hero " << FP_GetExponent(pHero->field_B8_xpos) << "," << FP_GetExponent(pHero->field_BC_ypos)
                 << " health " << FP_GetDouble(pHero->field_10C_health) << " motion " << pHero->field_106_current_motion);
        sChecked = true;
    }

    if (sStop < 0 || sFrames >= kFramesPerStop)
    {
        sStop++;
        sFrames = 0;
        sChecked = false;
        if (sStop >= kEntries)
        {
            LOG_INFO("Tour: finished");
            return;
        }
        LOG_INFO("Tour: going to stop " << sStop + 1 << " '" << gPerLvlData_561700[sStop].field_0_display_name << "'");
        GoTo(gPerLvlData_561700[sStop]);
    }
}

#else

void Ps2Tour_Frame()
{
}

#endif
