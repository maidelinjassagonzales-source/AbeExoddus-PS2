#pragma once

// Breadcrumb trace for hunting PS2 hangs/crashes. Lines go straight to the EE serial port
// (synchronous, no IOP involved), so the last line in the PCSX2 EE log is the last step reached.
// Only active in RELIVE_PS2_HOSTFS test builds, after Ps2TraceEnable(true), for a bounded number of lines.

#include "Types.hpp"

#if defined(__PS2__) && (defined(RELIVE_PS2_HOSTFS) || defined(RELIVE_PS2_DISCTRACE))

    #include <sio.h>
    #include <stdio.h>

struct Ps2TraceState
{
    bool mEnabled = false;
    u32 mLinesLeft = 4000;
};

inline Ps2TraceState& Ps2TraceGet()
{
    static Ps2TraceState state;
    return state;
}

inline void Ps2TraceEnable(bool enable)
{
    #ifdef RELIVE_PS2_DISCTRACE
    enable = true; // diagnosing a real-console build: trace from the start
    #endif
    Ps2TraceGet().mEnabled = enable;
}

inline void Ps2Trace(const char_type* what, u32 a = 0, u32 b = 0)
{
    #ifdef RELIVE_PS2_DISCTRACE
    // Real console: no serial cable, so the lines only go to the RAM ring shown by the hang
    // screen, with no line limit so the ring always holds the latest steps.
    char_type line[96];
    snprintf(line, sizeof(line), "%s %u %08X\n", what, a, b);
    #else
    Ps2TraceState& state = Ps2TraceGet();
    if (!state.mEnabled || state.mLinesLeft == 0)
    {
        return;
    }
    state.mLinesLeft--;
    char_type line[96];
    snprintf(line, sizeof(line), "[bc] %s %u %08X\n", what, a, b);
    sio_putsn(line);
    #endif
    extern void Ps2LogRemember(const char_type* line); // relive/Exe.cpp: on-screen hang report
    Ps2LogRemember(line);
}

    #define PS2_TRACE(...) Ps2Trace(__VA_ARGS__)
    #define PS2_TRACE_ENABLE(on) Ps2TraceEnable(on)

#else

    #define PS2_TRACE(...)
    #define PS2_TRACE_ENABLE(on)

#endif
