#pragma once

// Tiny frame profiler for the PS2 port. Uses the EE COP0 Count register (CPU clock, 294.912 MHz).
// Compiles to nothing on other platforms or when RELIVE_PS2_PROFILE is not defined.

#include "Types.hpp"

#if defined(__PS2__) && defined(RELIVE_PS2_PROFILE)

    #include <stdio.h>

enum class Ps2ProfSlot : u32
{
    DdvRead,
    DdvDecode,
    VgaBlit,
    VgaUpload,
    VgaDraw,
    VgaPresent,
    GameUpdate,
    GameRender,
    DrawOT,
    VSyncWait,
    Frame,
    Count
};

struct Ps2ProfData
{
    u64 mCycles[static_cast<u32>(Ps2ProfSlot::Count)] = {};
    u32 mFrames = 0;
};

inline Ps2ProfData& Ps2Prof()
{
    static Ps2ProfData data;
    return data;
}

inline u32 Ps2CycleCount()
{
    u32 count;
    asm volatile("mfc0 %0, $9" : "=r"(count));
    return count;
}

class Ps2ProfScope final
{
public:
    explicit Ps2ProfScope(Ps2ProfSlot slot)
        : mSlot(slot)
        , mStart(Ps2CycleCount())
    {
    }

    ~Ps2ProfScope()
    {
        Ps2Prof().mCycles[static_cast<u32>(mSlot)] += Ps2CycleCount() - mStart;
    }

private:
    Ps2ProfSlot mSlot;
    u32 mStart;
};

// Call once per presented frame; prints the average milliseconds per frame of every slot.
inline void Ps2ProfEndFrame()
{
    static const char_type* kNames[] = {"ddv_read", "ddv_decode", "vga_blit", "vga_upload", "vga_draw", "vga_present", "update", "render", "draw_ot", "vsync_wait", "frame"};
    static u32 sLastFrameStart = Ps2CycleCount();

    Ps2ProfData& data = Ps2Prof();
    const u32 now = Ps2CycleCount();
    data.mCycles[static_cast<u32>(Ps2ProfSlot::Frame)] += now - sLastFrameStart;
    sLastFrameStart = now;

    if (++data.mFrames < 30)
    {
        return;
    }

    printf("[prof] ms/frame over %u frames:", data.mFrames);
    for (u32 i = 0; i < static_cast<u32>(Ps2ProfSlot::Count); i++)
    {
        const u32 usPerFrame = static_cast<u32>(data.mCycles[i] / data.mFrames / 295);
        printf(" %s=%u.%01u", kNames[i], usPerFrame / 1000, (usPerFrame % 1000) / 100);
        data.mCycles[i] = 0;
    }
    printf("\n");
    data.mFrames = 0;
}

    #define PS2_PROF_SCOPE(slot) Ps2ProfScope ps2ProfScope_##slot(Ps2ProfSlot::slot)
    #define PS2_PROF_END_FRAME() Ps2ProfEndFrame()
    // For spans that are not a single block: PS2_PROF_BEGIN(Update); ... PS2_PROF_END(Update);
    #define PS2_PROF_BEGIN(slot) const u32 ps2ProfStart_##slot = Ps2CycleCount()
    #define PS2_PROF_END(slot) Ps2Prof().mCycles[static_cast<u32>(Ps2ProfSlot::slot)] += Ps2CycleCount() - ps2ProfStart_##slot

#else

    #define PS2_PROF_SCOPE(slot)
    #define PS2_PROF_END_FRAME()
    #define PS2_PROF_BEGIN(slot)
    #define PS2_PROF_END(slot)

#endif
