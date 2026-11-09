#include "stdafx.h"
#include "Ps2Coop.hpp"

#ifdef __PS2__

    #include "Abe.hpp"
    #include "Game.hpp"
    #include "Input.hpp"
    #include "Map.hpp"
    #include "PathData.hpp"
    #include "stdlib.hpp"
    #include "AnimResources.hpp"
    #include "Path.hpp"
    #include "Ps2CoopText.hpp"
    #include <libpad.h>
    #include <SDL3/SDL.h>

// How it works: the engine is written around one hero. Almost all of Abe's logic reads the global
// hero (sActiveHero), the global controlled character and the global pad index. Rather than
// rewriting that, player 2 is a normal Abe whose update runs with those three globals pointing at
// player 2 and pad 1, then put back. The camera stays with player 1 (Map refuses camera changes
// requested by player 2), and player 2 is brought back next to player 1 when off screen or dead.

namespace {

Abe* sPlayer2 = nullptr;
// What player 2 is steering: their Abe, or the creature they possessed by chanting. This is player
// 2's own copy of the engine's single "controlled character" slot.
BaseAliveGameObject* sPlayer2Controlled = nullptr;
const BaseGameObject* sDyingPlayer2 = nullptr; // marked dead, destructor not run yet
bool sJoined = false;
s32 sRespawnDelay = 0;
s32 sOffscreenFrames = 0;
s32 sStartHeldFrames = 0;

constexpr s32 kRespawnFrames = 60;   // ~2 s at 30 fps
constexpr s32 kOffscreenFrames = 15; // grace before pulling player 2 back
constexpr s32 kLeaveHoldFrames = 60; // hold START ~2 s to leave

enum class Notice
{
    eNone,
    eJoined,
    eLeft,
};
Notice sNotice = Notice::eNone;
u64 sNoticeUntilMs = 0;
bool sInLevel = false;

void ShowNotice(Notice notice, u32 durationMs)
{
    sNotice = notice;
    sNoticeUntilMs = SDL_GetTicks() + durationMs;
}

bool SecondPadPluggedIn()
{
    // Port 2, slot 0. FINDCTP1/STABLE mean a controller answers in that port.
    const s32 state = padGetState(1, 0);
    return state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
}

bool IsAliveObject(const BaseGameObject* pObj)
{
    for (s32 i = 0; i < gBaseGameObject_list_BB47C4->Size(); i++)
    {
        BaseGameObject* pItem = gBaseGameObject_list_BB47C4->ItemAt(i);
        if (!pItem)
        {
            break;
        }
        if (pItem == pObj)
        {
            return !pItem->field_6_flags.Get(BaseGameObject::eDead_Bit3);
        }
    }
    return false;
}

void RemovePlayer2(s32 respawnDelay)
{
    if (sPlayer2)
    {
        sPlayer2->field_6_flags.Set(BaseGameObject::eDead_Bit3);
        sDyingPlayer2 = sPlayer2;
    }
    sPlayer2 = nullptr;
    sPlayer2Controlled = nullptr;
    sRespawnDelay = respawnDelay;
}

bool Player1OnGround()
{
    Abe* pHero = sActiveHero_5C1B68;
    return pHero && pHero != spAbe_554D5C && pHero->field_10C_health > FP_FromInteger(0) && pHero->field_100_pCollisionLine;
}

void PlaceNextToPlayer1(Abe* pAbe)
{
    Abe* pHero = sActiveHero_5C1B68;
    const FP offset = pHero->field_20_animation.field_4_flags.Get(AnimFlags::eBit5_FlipX) ? FP_FromInteger(18) : FP_FromInteger(-18);

    pAbe->field_B8_xpos = pHero->field_B8_xpos + offset;
    pAbe->field_BC_ypos = pHero->field_BC_ypos;
    pAbe->field_F8_LastLineYPos = pHero->field_BC_ypos;
    pAbe->field_C0_path_number = pHero->field_C0_path_number;
    pAbe->field_C2_lvl_number = pHero->field_C2_lvl_number;
    pAbe->field_100_pCollisionLine = pHero->field_100_pCollisionLine;
    pAbe->field_CC_sprite_scale = pHero->field_CC_sprite_scale;
    pAbe->field_D6_scale = pHero->field_D6_scale;
    pAbe->field_20_animation.field_C_render_layer = pHero->field_20_animation.field_C_render_layer;
    pAbe->field_C4_velx = FP_FromInteger(0);
    pAbe->field_C8_vely = FP_FromInteger(0);
    pAbe->field_106_current_motion = eAbeMotions::Motion_0_Idle_44EEB0;
    pAbe->field_108_next_motion = eAbeMotions::Motion_0_Idle_44EEB0;
}

void SpawnPlayer2()
{
    BaseAliveGameObject* const pControlled = sControlledCharacter_5C1B8C;

    sPlayer2 = ae_new<Abe>();
    if (!sPlayer2)
    {
        return;
    }
    const AnimRecord& rec = AnimRec(AnimId::Mudokon_Walk);
    sPlayer2->ctor_44AD10(rec.mFrameTableOffset, 85, 57, 55);

    // Abe's constructor makes itself the controlled character; that stays player 1's.
    sControlledCharacter_5C1B8C = pControlled;

    PlaceNextToPlayer1(sPlayer2);
    sPlayer2Controlled = sPlayer2;
    sOffscreenFrames = 0;
    LOG_INFO("Co-op: player 2 spawned");
}

#ifdef RELIVE_PS2_HOSTFS
// Test builds: ps2warp.txt ("camX camY x y") moves player 1 there once player 2 has spawned, the
// same way the original debug menu teleports, so co-op features can be tried next to a creature.
void TestWarpPlayer1()
{
    static bool sDone = false;
    static s32 sDelay = 30;
    if (sDone || --sDelay > 0)
    {
        return;
    }
    sDone = true;

    s32 camX = 0;
    s32 camY = 0;
    s32 x = 0;
    s32 y = 0;
    FILE* f = fopen("ps2warp.txt", "r");
    if (!f)
    {
        return;
    }
    const bool ok = fscanf(f, "%d %d %d %d", &camX, &camY, &x, &y) == 4;
    fclose(f);
    if (!ok || camX < 0 || camY < 0 || camX >= sPath_dword_BB47C0->field_6_cams_on_x || camY >= sPath_dword_BB47C0->field_8_cams_on_y)
    {
        return;
    }

    const u8* pPathData = *gMap_5C3030.GetPathResourceBlockPtr(gMap_5C3030.field_2_current_path);
    const auto* pCamName = reinterpret_cast<const CameraName*>(&pPathData[(camX + camY * sPath_dword_BB47C0->field_6_cams_on_x) * sizeof(CameraName)]);
    if (!pCamName->name[0])
    {
        return;
    }
    const s16 camId = static_cast<s16>((pCamName->name[6] - '0') * 10 + (pCamName->name[7] - '0'));

    gMap_5C3030.SetActiveCam_480D30(gMap_5C3030.field_0_current_level, gMap_5C3030.field_2_current_path, camId, CameraSwapEffects::eInstantChange_0, 0, 0);
    Abe* pHero = sActiveHero_5C1B68;
    pHero->field_B8_xpos = FP_FromInteger(x);
    pHero->field_BC_ypos = FP_FromInteger(y);
    pHero->field_106_current_motion = 3; // falling, lands on whatever floor is below
    pHero->field_1AC_flags.Set(Abe::e1AC_Bit7_land_softly);
    pHero->field_100_pCollisionLine = nullptr;
    pHero->field_F8_LastLineYPos = pHero->field_BC_ypos;
    LOG_INFO("Test warp: player 1 to cam " << camId << " (" << camX << "," << camY << ") at " << x << "," << y);
}
#endif

} // namespace

void Ps2Coop_Frame()
{
    sInLevel = gMap_5C3030.field_0_current_level != LevelIds::eMenu_0 && gMap_5C3030.field_0_current_level != LevelIds::eNone;
    if (!sInLevel)
    {
        return;
    }

    const auto& pad2 = Input().field_0_pads[1];
    if (!sJoined)
    {
        if (pad2.field_C_held & InputCommands::Enum::ePause)
        {
            sJoined = true;
            sRespawnDelay = 0;
            ShowNotice(Notice::eJoined, 4000);
            LOG_INFO("Co-op: player 2 joined");
        }
        return;
    }

    // Hold START to leave the game (only while steering their own Abe).
    sStartHeldFrames = (pad2.field_0_pressed & InputCommands::Enum::ePause) ? sStartHeldFrames + 1 : 0;
    if (sStartHeldFrames >= kLeaveHoldFrames && (!sPlayer2 || sPlayer2Controlled == sPlayer2))
    {
        RemovePlayer2(0);
        sJoined = false;
        sStartHeldFrames = 0;
        ShowNotice(Notice::eLeft, 3000);
        LOG_INFO("Co-op: player 2 left");
        return;
    }

    // The possessed creature died or vanished: control goes back to player 2's Abe.
    if (sPlayer2 && sPlayer2Controlled != sPlayer2 && (!sPlayer2Controlled || !IsAliveObject(sPlayer2Controlled)))
    {
        sPlayer2Controlled = sPlayer2;
    }

    if (!sPlayer2)
    {
        if (sRespawnDelay > 0)
        {
            sRespawnDelay--;
        }
        else if (Player1OnGround())
        {
            SpawnPlayer2();
        }
        return;
    }

#ifdef RELIVE_PS2_HOSTFS
    TestWarpPlayer1();
    static u32 sLogTick = 0;
    if (++sLogTick % 60 == 0)
    {
        LOG_INFO("Co-op P2: pos " << FP_GetExponent(sPlayer2->field_B8_xpos) << "," << FP_GetExponent(sPlayer2->field_BC_ypos)
                 << " motion " << sPlayer2->field_106_current_motion << " health " << FP_GetDouble(sPlayer2->field_10C_health)
                 << " controls type " << static_cast<s32>(sPlayer2Controlled ? sPlayer2Controlled->Type() : AETypes::eNone_0)
                 << " pad2 0x" << std::hex << Input().field_0_pads[1].field_0_pressed << std::dec);
    }
#endif

    // Keep player 2 on player 1's screen: the camera never follows player 2.
    const bool onScreen = sPlayer2->field_C2_lvl_number == gMap_5C3030.field_0_current_level
                       && sPlayer2->field_C0_path_number == gMap_5C3030.field_2_current_path
                       && gMap_5C3030.Is_Point_In_Current_Camera_4810D0(sPlayer2->field_C2_lvl_number, sPlayer2->field_C0_path_number, sPlayer2->field_B8_xpos, sPlayer2->field_BC_ypos, 0);
    // Already right next to player 1 (e.g. placed just past the screen edge): leave it alone, or
    // every pull-back resets the motion and a chant could never finish.
    const s32 dx = FP_GetExponent(sPlayer2->field_B8_xpos) - FP_GetExponent(sActiveHero_5C1B68->field_B8_xpos);
    const bool nextToPlayer1 = sPlayer2->field_C0_path_number == sActiveHero_5C1B68->field_C0_path_number && dx > -40 && dx < 40;

    // While possessing, player 2's Abe stands chanting; don't pull it around.
    if (onScreen || nextToPlayer1 || sPlayer2Controlled != sPlayer2)
    {
        sOffscreenFrames = 0;
    }
    else if (++sOffscreenFrames > kOffscreenFrames && Player1OnGround() && sPlayer2->field_10C_health > FP_FromInteger(0))
    {
        PlaceNextToPlayer1(sPlayer2);
        sOffscreenFrames = 0;
    }
}

bool Ps2Coop_IsActive()
{
    return sJoined;
}

bool Ps2Coop_IsPlayer2(const BaseGameObject* pObj)
{
    return pObj && (pObj == sPlayer2 || pObj == sPlayer2Controlled);
}

void Ps2Coop_UpdatePlayer2(BaseGameObject* pObj)
{
    Abe* const pHero = sActiveHero_5C1B68;
    BaseAliveGameObject* const pControlled = sControlledCharacter_5C1B8C;
    const u16 padIndex = sCurrentControllerIndex_5C1BBE;

    // Player 2's Abe and whatever player 2 possessed both run with player 2's view of the world:
    // hero = player 2's Abe, controlled character = player 2's, pad = 2. Possessing sets the slot
    // to the creature, releasing sets it back to "the hero", i.e. player 2's Abe.
    sActiveHero_5C1B68 = sPlayer2;
    sControlledCharacter_5C1B8C = sPlayer2Controlled;
    sCurrentControllerIndex_5C1BBE = 1;

    pObj->VUpdate();

    if (sPlayer2)
    {
        if (sControlledCharacter_5C1B8C != sPlayer2Controlled)
        {
            LOG_INFO("Co-op: player 2 now controls type " << static_cast<s32>(sControlledCharacter_5C1B8C ? sControlledCharacter_5C1B8C->Type() : AETypes::eNone_0));
        }
        sPlayer2Controlled = sControlledCharacter_5C1B8C ? sControlledCharacter_5C1B8C : sPlayer2;
    }

    sActiveHero_5C1B68 = pHero;
    sControlledCharacter_5C1B8C = pControlled;
    sCurrentControllerIndex_5C1BBE = padIndex;
}

bool Ps2Coop_OnAbeDeathReset(const BaseGameObject* pAbe)
{
    if (!pAbe || pAbe != sPlayer2)
    {
        return false;
    }
    // Player 2 died: remove them and bring them back next to player 1 shortly, no level reset.
    RemovePlayer2(kRespawnFrames);
    LOG_INFO("Co-op: player 2 died, respawning");
    return true;
}

bool Ps2Coop_OnAbeDestroyed(const BaseGameObject* pAbe)
{
    if (pAbe && pAbe == sDyingPlayer2)
    {
        sDyingPlayer2 = nullptr;
        return true;
    }
    if (pAbe && pAbe == sPlayer2)
    {
        sPlayer2 = nullptr;
        sPlayer2Controlled = nullptr;
        sRespawnDelay = kRespawnFrames;
        return true;
    }
    return false;
}

namespace {

// ABGR1555: r bits 0-4, g 5-9, b 10-14, bit 15 = opaque.
inline u16 Blend1555(u16 dst, s32 r, s32 g, s32 b, s32 alpha256)
{
    const s32 dr = dst & 0x1F;
    const s32 dg = (dst >> 5) & 0x1F;
    const s32 db = (dst >> 10) & 0x1F;
    const s32 nr = dr + (((r - dr) * alpha256) >> 8);
    const s32 ng = dg + (((g - dg) * alpha256) >> 8);
    const s32 nb = db + (((b - db) * alpha256) >> 8);
    return static_cast<u16>(0x8000 | (nb << 10) | (ng << 5) | nr);
}

void DarkenBox(u8* pPixels, s32 pitch, s32 x0, s32 y0, s32 w, s32 h, s32 fw, s32 fh)
{
    for (s32 y = y0; y < y0 + h; y++)
    {
        if (y < 0 || y >= fh)
        {
            continue;
        }
        u16* pRow = reinterpret_cast<u16*>(pPixels + y * pitch);
        for (s32 x = x0; x < x0 + w; x++)
        {
            if (x >= 0 && x < fw)
            {
                pRow[x] = Blend1555(pRow[x], 0, 0, 0, 150);
            }
        }
    }
}

void DrawMask(u8* pPixels, s32 pitch, const Ps2CoopTextMask& mask, s32 x0, s32 y0, s32 r, s32 g, s32 b, s32 fw, s32 fh)
{
    for (s32 y = 0; y < mask.mHeight; y++)
    {
        const s32 py = y0 + y;
        if (py < 0 || py >= fh)
        {
            continue;
        }
        u16* pRow = reinterpret_cast<u16*>(pPixels + py * pitch);
        const u8* pAlpha = mask.mAlpha + y * mask.mWidth;
        for (s32 x = 0; x < mask.mWidth; x++)
        {
            const s32 px = x0 + x;
            if (pAlpha[x] && px >= 0 && px < fw)
            {
                pRow[px] = Blend1555(pRow[px], r, g, b, pAlpha[x]);
            }
        }
    }
}

// One or two lines of text in a dark box at the top-right corner.
void DrawCornerMessage(u8* pPixels, s32 pitch, s32 fw, s32 fh, const Ps2CoopTextMask& line1, const Ps2CoopTextMask* pLine2, s32 r, s32 g, s32 b)
{
    const s32 margin = 8;
    const s32 padX = 6;
    const s32 padY = 3;
    const s32 gap = 2;
    const s32 textW = pLine2 && pLine2->mWidth > line1.mWidth ? pLine2->mWidth : line1.mWidth;
    const s32 textH = line1.mHeight + (pLine2 ? gap + pLine2->mHeight : 0);
    const s32 boxW = textW + padX * 2;
    const s32 boxH = textH + padY * 2;
    const s32 boxX = fw - boxW - margin;
    const s32 boxY = 4;

    DarkenBox(pPixels, pitch, boxX, boxY, boxW, boxH, fw, fh);
    DrawMask(pPixels, pitch, line1, boxX + boxW - padX - line1.mWidth, boxY + padY, r, g, b, fw, fh);
    if (pLine2)
    {
        DrawMask(pPixels, pitch, *pLine2, boxX + boxW - padX - pLine2->mWidth, boxY + padY + line1.mHeight + gap, 24, 24, 24, fw, fh);
    }
}

} // namespace

void Ps2Coop_DrawOverlay(void* pPixels, int pitchBytes, int w, int h)
{
    if (!sInLevel || !pPixels)
    {
        return;
    }
    u8* pBytes = static_cast<u8*>(pPixels);
    const u64 now = SDL_GetTicks();

    if (sNotice != Notice::eNone && now < sNoticeUntilMs)
    {
        if (sNotice == Notice::eJoined)
        {
            DrawCornerMessage(pBytes, pitchBytes, w, h, kCoopTextJoined, &kCoopTextHowToLeave, 12, 31, 14);
        }
        else
        {
            DrawCornerMessage(pBytes, pitchBytes, w, h, kCoopTextLeft, nullptr, 31, 22, 10);
        }
        return;
    }
    sNotice = Notice::eNone;

    // Invite player 2 while a second controller is plugged in and they have not joined; blinks.
    if (!sJoined && SecondPadPluggedIn() && (now / 400) % 3 != 2)
    {
        DrawCornerMessage(pBytes, pitchBytes, w, h, kCoopTextJoin, nullptr, 31, 25, 8);
    }
}

void Ps2Coop_TintPlayer2(const BaseGameObject* pObj, short* r, short* g, short* b)
{
    if (Ps2Coop_IsPlayer2(pObj))
    {
        // Cool blue Abe: clearly different from player 1 even inside shadow zones.
        *r = static_cast<short>(*r * 2 / 5);
        *g = static_cast<short>(*g * 3 / 5);
        const s32 blue = *b * 8 / 5 + 20;
        *b = static_cast<short>(blue > 255 ? 255 : blue);
    }
}

#else

void Ps2Coop_Frame()
{
}

bool Ps2Coop_IsActive()
{
    return false;
}

bool Ps2Coop_IsPlayer2(const BaseGameObject*)
{
    return false;
}

void Ps2Coop_UpdatePlayer2(BaseGameObject*)
{
}

bool Ps2Coop_OnAbeDeathReset(const BaseGameObject*)
{
    return false;
}

bool Ps2Coop_OnAbeDestroyed(const BaseGameObject*)
{
    return false;
}

void Ps2Coop_DrawOverlay(void*, int, int, int)
{
}

void Ps2Coop_TintPlayer2(const BaseGameObject*, short*, short*, short*)
{
}

#endif
