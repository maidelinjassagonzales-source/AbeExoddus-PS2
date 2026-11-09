#include "stdafx.h"
#include <iostream>
#include <string>

#if _WIN32
    #include <windows.h>
    #include "ExportHooker.hpp"
#endif

#include "../AliveLibAE/WinMain.hpp"
#if !defined(__PS2__)
    #define RELIVE_WITH_AO 1 // PS2 build ships Exoddus only: Oddysee's engine would cost ~7 MB of the 32 MB RAM
#endif
#if RELIVE_WITH_AO
#include "../AliveLibAO/WinMain.hpp"
#endif
#include "W32CrashHandler.hpp"
#include <SDL3/SDL.h>
#ifdef __PS2__
    // SDL_RunApp patches the IOP to accept modules from EE RAM (without it the pad driver hangs
    // while loading) and then starts every filesystem driver: USB, HDD (DEV9/ATA), MX4SIO, memory
    // card and CD. On a real console under OPL that clashes with OPL's own modules: its ATA driver
    // reads the game from the HDD, and MX4SIO takes over the SIO2 port the pads use. Disc builds
    // therefore do that setup themselves and load only what they need (Ps2InitIopDrivers).
    #ifndef RELIVE_PS2_HOSTFS
        #define SDL_MAIN_HANDLED
    #endif
    #include <SDL3/SDL_main.h>
    #ifndef RELIVE_PS2_HOSTFS
        #include <sbv_patches.h>
extern "C" {
        #include <ps2_fileXio_driver.h>
        #include <ps2_memcard_driver.h>
        #include <ps2_cdfs_driver.h>
}
    #endif
    #include <sifrpc.h>
    #include <iopcontrol.h>
    #include <malloc.h>
    #include <unistd.h>
    #include <new>

// Overrides SDL's weak reset_IOP. A full IOP reset drops PCSX2's 'host:' device, so builds made
// for running the game files straight from the PC (RELIVE_PS2_HOSTFS) keep the IOP as booted.
extern "C" void reset_IOP(void)
{
    // No IOP reset in any build: it drops PCSX2's host: device, OPL's disc emulation, and the
    // reboot runs the BIOS clearspu module which zeroes the SPU2 volume (silent game).
    SifInitRpc(0);
}

    #include <ee_debug.h>
    #include <sio.h>
    #include <stdarg.h>
    #include <kernel.h>

    #include <debug.h>

// The last report/trace lines, kept in RAM so a real console (no serial cable) can show them on
// screen when the game crashes or hangs.
static constexpr u32 kLogLines = 22;
static char_type sLogRing[kLogLines][100];
static volatile u32 sLogPos = 0;

void Ps2LogRemember(const char_type* line)
{
    char_type* pDst = sLogRing[sLogPos++ % kLogLines];
    strncpy(pDst, line, sizeof(sLogRing[0]) - 1);
    pDst[sizeof(sLogRing[0]) - 1] = 0;
}

// Crash and hang reports go to the EE serial port: it needs no IOP, so it still works when the
// IOP (and with it printf) is the thing that is stuck. PCSX2 shows it in the EE console log.
static void Ps2Report(const char_type* fmt, ...)
{
    char_type buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    sio_putsn(buffer);
    Ps2LogRemember(buffer);
}

// Takes over the display (GS reset by libdebug) and prints the last log lines. Real console only:
// in PCSX2 the serial log already has everything.
static void Ps2ShowReportScreen(const char_type* title)
{
    #ifndef RELIVE_PS2_HOSTFS
    init_scr();
    scr_setCursor(0);
    scr_printf("\n  ABE'S EXODDUS PS2 - INFORME DE ERROR (haz una foto)\n  %s\n\n", title);
    const u32 pos = sLogPos;
    const u32 count = pos < kLogLines ? pos : kLogLines;
    for (u32 i = pos - count; i != pos; i++)
    {
        const char_type* pLine = sLogRing[i % kLogLines];
        scr_printf("  %.76s%s", pLine, (pLine[0] && pLine[strlen(pLine) - 1] == '\n') ? "" : "\n");
    }
    #else
    (void) title;
    #endif
}

// CPU exception reporter. The level-1 handler runs in exception context, so it only saves the
// registers and sends the CPU to Ps2CrashReport(), which prints them in normal context.
static EE_RegFrame sCrashFrame;
static u32 sCrashStack[64];

extern "C" void Ps2CrashReport()
{
    static const char_type* kCauses[] = {"Int", "TLB mod", "TLB load", "TLB store", "Address load", "Address store", "Bus fetch",
                                         "Bus data", "Syscall", "Break", "Reserved instr", "Coprocessor", "Overflow", "Trap"};
    const u32 code = (sCrashFrame.cause >> 2) & 0x1F;
    const char_type* what = sCrashFrame.cause == 0xFFFFFFFF ? "NULL-POINTER WRITE (data breakpoint)" : (code < 14 ? kCauses[code] : "?");
    Ps2Report("[relive-ps2] *** CRASH: %s (code %u) at EPC=%08X badvaddr=%08X RA=%08X SP=%08X\n",
           what, static_cast<u32>(code), sCrashFrame.epc, sCrashFrame.badvaddr, sCrashFrame.ra[0], sCrashFrame.sp[0]);
    Ps2Report("[relive-ps2] regs a0=%08X a1=%08X a2=%08X a3=%08X v0=%08X v1=%08X s0=%08X s1=%08X\n",
           sCrashFrame.a0[0], sCrashFrame.a1[0], sCrashFrame.a2[0], sCrashFrame.a3[0], sCrashFrame.v0[0], sCrashFrame.v1[0], sCrashFrame.s0[0], sCrashFrame.s1[0]);
    Ps2Report("[relive-ps2] stack code addrs:");
    for (u32 i = 0; i < 64; i++)
    {
        if (sCrashStack[i] >= 0x00100000 && sCrashStack[i] < 0x00800000 && (sCrashStack[i] & 3) == 0)
        {
            Ps2Report(" %08X", sCrashStack[i]);
        }
    }
    Ps2Report("\n");
    Ps2ShowReportScreen("CRASH");
    for (;;)
    {
        SleepThread();
    }
}

// Misaligned loads/stores. The game was written for x86, which allows them; the R5900 raises an
// address error (PCSX2 does not emulate that, so they only show on a real console). The handler
// performs the access byte by byte and resumes after the instruction.
    #define PS2_FPR_CASE(n) \
        case n: \
            if (isLoad) \
                asm volatile("mtc1 %0, $f" #n ::"r"(value)); \
            else \
                asm volatile("mfc1 %0, $f" #n : "=r"(value)); \
            break;

static bool Ps2FpuRegAccess(u32 reg, bool isLoad, u32& value)
{
    switch (reg)
    {
        PS2_FPR_CASE(0) PS2_FPR_CASE(1) PS2_FPR_CASE(2) PS2_FPR_CASE(3) PS2_FPR_CASE(4) PS2_FPR_CASE(5) PS2_FPR_CASE(6) PS2_FPR_CASE(7)
        PS2_FPR_CASE(8) PS2_FPR_CASE(9) PS2_FPR_CASE(10) PS2_FPR_CASE(11) PS2_FPR_CASE(12) PS2_FPR_CASE(13) PS2_FPR_CASE(14) PS2_FPR_CASE(15)
        PS2_FPR_CASE(16) PS2_FPR_CASE(17) PS2_FPR_CASE(18) PS2_FPR_CASE(19) PS2_FPR_CASE(20) PS2_FPR_CASE(21) PS2_FPR_CASE(22) PS2_FPR_CASE(23)
        PS2_FPR_CASE(24) PS2_FPR_CASE(25) PS2_FPR_CASE(26) PS2_FPR_CASE(27) PS2_FPR_CASE(28) PS2_FPR_CASE(29) PS2_FPR_CASE(30) PS2_FPR_CASE(31)
        default:
            return false;
    }
    return true;
}
    #undef PS2_FPR_CASE

static u32 sUnalignedFixups = 0;

static bool Ps2FixUnalignedAccess(EE_RegFrame* pFrame)
{
    const u32 code = (pFrame->cause >> 2) & 0x1F;
    if (code != 4 && code != 5) // not an address error
    {
        return false;
    }
    u32(*regs)[4] = reinterpret_cast<u32(*)[4]>(pFrame); // zero, at, v0 ... ra, 128 bits each

    // In a branch delay slot EPC points at the branch: work out where the branch goes (from the
    // registers before the access, as the CPU does), then emulate the access in the slot.
    const bool inDelaySlot = (pFrame->cause & 0x80000000u) != 0;
    u32 resumePc = pFrame->epc + 4;
    u32 linkReg = 0;
    u32 linkValue = 0;
    if (inDelaySlot)
    {
        const u32 br = *reinterpret_cast<const u32*>(pFrame->epc);
        const u32 bop = br >> 26;
        const s32 rs = static_cast<s32>(regs[(br >> 21) & 31][0]);
        const s32 rtv = static_cast<s32>(regs[(br >> 16) & 31][0]);
        const u32 taken = pFrame->epc + 4 + (static_cast<u32>(static_cast<s32>(static_cast<s16>(br & 0xFFFF))) << 2);
        const u32 notTaken = pFrame->epc + 8;
        bool cond = false;
        switch (bop)
        {
            case 0x00: // SPECIAL: jr / jalr
                if ((br & 0x3F) == 0x08 || (br & 0x3F) == 0x09)
                {
                    resumePc = static_cast<u32>(rs);
                    if ((br & 0x3F) == 0x09)
                    {
                        linkReg = (br >> 11) & 31;
                        linkValue = notTaken;
                    }
                    break;
                }
                return false;
            case 0x02: // j
            case 0x03: // jal
                resumePc = ((pFrame->epc + 4) & 0xF0000000u) | ((br & 0x03FFFFFFu) << 2);
                if (bop == 0x03)
                {
                    linkReg = 31;
                    linkValue = notTaken;
                }
                break;
            case 0x01: // REGIMM: bltz, bgez (+likely, +al)
            {
                const u32 kind = (br >> 16) & 31;
                cond = (kind & 1) ? rs >= 0 : rs < 0;
                if (kind == 0x10 || kind == 0x11)
                {
                    linkReg = 31;
                    linkValue = notTaken;
                }
                else if (kind > 3)
                {
                    return false;
                }
                resumePc = cond ? taken : notTaken;
                break;
            }
            case 0x04: case 0x14: cond = rs == rtv; resumePc = cond ? taken : notTaken; break; // beq(l)
            case 0x05: case 0x15: cond = rs != rtv; resumePc = cond ? taken : notTaken; break; // bne(l)
            case 0x06: case 0x16: cond = rs <= 0; resumePc = cond ? taken : notTaken; break; // blez(l)
            case 0x07: case 0x17: cond = rs > 0; resumePc = cond ? taken : notTaken; break; // bgtz(l)
            case 0x11: // COP1 bc1f / bc1t (+likely)
            {
                if (((br >> 21) & 31) != 8)
                {
                    return false;
                }
                u32 fcr31;
                asm volatile("cfc1 %0, $31" : "=r"(fcr31));
                const bool c = (fcr31 >> 23) & 1;
                cond = ((br >> 16) & 1) ? c : !c;
                resumePc = cond ? taken : notTaken;
                break;
            }
            default:
                return false;
        }
    }
    const u32 insn = *reinterpret_cast<const u32*>(inDelaySlot ? pFrame->epc + 4 : pFrame->epc);
    const u32 op = insn >> 26;
    const u32 rt = (insn >> 16) & 31;
    u8* p = reinterpret_cast<u8*>(regs[(insn >> 21) & 31][0] + static_cast<u32>(static_cast<s32>(static_cast<s16>(insn & 0xFFFF))));
    auto load = [p](u32 bytes) -> u64
    {
        u64 v = 0;
        for (u32 i = 0; i < bytes; i++)
        {
            v |= static_cast<u64>(p[i]) << (8 * i);
        }
        return v;
    };
    auto store = [p](u64 v, u32 bytes)
    {
        for (u32 i = 0; i < bytes; i++)
        {
            p[i] = static_cast<u8>(v >> (8 * i));
        }
    };
    auto setReg = [&](u64 v)
    {
        if (rt != 0)
        {
            regs[rt][0] = static_cast<u32>(v);
            regs[rt][1] = static_cast<u32>(v >> 32);
        }
    };
    const u64 rtValue = static_cast<u64>(regs[rt][0]) | (static_cast<u64>(regs[rt][1]) << 32);
    u32 fpr = 0;
    switch (op)
    {
        case 0x21: setReg(static_cast<u64>(static_cast<s64>(static_cast<s16>(load(2))))); break; // lh
        case 0x25: setReg(load(2)); break; // lhu
        case 0x23: setReg(static_cast<u64>(static_cast<s64>(static_cast<s32>(load(4))))); break; // lw
        case 0x27: setReg(load(4)); break; // lwu
        case 0x37: setReg(load(8)); break; // ld
        case 0x29: store(rtValue, 2); break; // sh
        case 0x2B: store(rtValue, 4); break; // sw
        case 0x3F: store(rtValue, 8); break; // sd
        case 0x31: // lwc1
            fpr = static_cast<u32>(load(4));
            Ps2FpuRegAccess(rt, true, fpr);
            break;
        case 0x39: // swc1
            Ps2FpuRegAccess(rt, false, fpr);
            store(fpr, 4);
            break;
        default:
            return false;
    }
    if (linkReg != 0)
    {
        regs[linkReg][0] = linkValue;
        regs[linkReg][1] = 0;
    }
    pFrame->epc = resumePc;
    sUnalignedFixups++;
    return true;
}

// Startup check that misaligned accesses are fixed up (an access that faults on real hardware).
static void Ps2UnalignedSelfTest()
{
    static u8 sBytes[16] __attribute__((aligned(16))) = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    u32 value;
    u8* p = sBytes + 1;
    asm volatile("lw %0, 0(%1)" : "=r"(value) : "r"(p) : "memory");
    Ps2Report("[relive-ps2] misaligned read test %08X (want 55443322), fixups %u\n", value, sUnalignedFixups);
}

// Shows the report screen and stops (used where the game cannot go on, e.g. no pad driver).
void Ps2ShowFatalScreen(const char_type* title)
{
    Ps2Report("[relive-ps2] *** %s\n", title);
    Ps2ShowReportScreen(title);
    for (;;)
    {
        SleepThread();
    }
}

    #ifndef RELIVE_PS2_HOSTFS
// What SDL_RunApp does, minus the drivers that clash with OPL (see the includes at the top).
// The pad (SIO2MAN/PADMAN) and audio (AUDSRV) drivers are loaded later by SDL itself.
static void Ps2InitIopDrivers()
{
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
    const s32 fileXio = init_fileXio_driver();
    const s32 memcard = init_memcard_driver(true);
    const s32 cdfs = init_cdfs_driver();
    Ps2Report("[relive-ps2] iop drivers: fileXio %d memcard %d cdfs %d\n", fileXio, memcard, cdfs);
    SDL_SetMainReady();
}
    #endif

static int Ps2ExceptionHandler(EE_RegFrame* pFrame)
{
    if (Ps2FixUnalignedAccess(pFrame))
    {
        return 1;
    }
    sCrashFrame = *pFrame;
    const u32* pStack = reinterpret_cast<const u32*>(pFrame->sp[0]);
    if (pFrame->sp[0] >= 0x00100000 && pFrame->sp[0] < 0x02000000 - sizeof(sCrashStack))
    {
        for (u32 i = 0; i < 64; i++)
        {
            sCrashStack[i] = pStack[i];
        }
    }
    pFrame->epc = reinterpret_cast<u32>(&Ps2CrashReport);
    return 1;
}

    #ifdef RELIVE_PS2_HOSTFS
// Semaphore 0 belongs to the EE kernel's topThread (libkernel InitThread). Game/library code
// deleting or signalling it makes topThread spin forever at priority 0 and freezes the console.
// The link wraps DeleteSema/SignalSema (-Wl,--wrap) so any such call reports its caller.
extern "C" s32 __real_DeleteSema(s32 semaId);
extern "C" s32 __real_SignalSema(s32 semaId);

extern "C" s32 __wrap_DeleteSema(s32 semaId)
{
    if (semaId == 0)
    {
        Ps2Report("[sema] *** DeleteSema(0) called from %08X\n", reinterpret_cast<u32>(__builtin_return_address(0)));
        // Code addresses found on the stack = the callers further up.
        const u32* pStack = static_cast<const u32*>(__builtin_frame_address(0));
        Ps2Report("[sema] stack:");
        for (u32 i = 0; i < 256; i++)
        {
            const u32 v = pStack[i];
            if (v >= 0x00100000 && v < 0x00600000 && (v & 3) == 0)
            {
                Ps2Report(" %08X", v);
            }
        }
        Ps2Report("\n");
        // Refuse it: deleting the kernel's own semaphore is never what the caller meant.
        return -1;
    }
    return __real_DeleteSema(semaId);
}

extern "C" s32 __wrap_SignalSema(s32 semaId)
{
    if (semaId == 0)
    {
        static u32 sReports = 0;
        if (sReports++ < 8)
        {
            Ps2Report("[sema] *** SignalSema(0) called from %08X\n", reinterpret_cast<u32>(__builtin_return_address(0)));
        }
    }
    return __real_SignalSema(semaId);
}
    #endif

// Data breakpoint hit (level-2 debug exception): the faulting PC is in ErrorEPC.
static int Ps2DataBreakpointHandler(EE_RegFrame* pFrame)
{
    ee_dbg_clr_bps();
    Ps2ExceptionHandler(pFrame);
    sCrashFrame.cause = 0xFFFFFFFF; // marks a data breakpoint for the report
    sCrashFrame.epc = pFrame->errorepc;
    pFrame->errorepc = reinterpret_cast<u32>(&Ps2CrashReport);
    return 1;
}

static void InstallPs2ExceptionReporter()
{
    if (ee_dbg_install(3) != 0)
    {
        Ps2Report("[relive-ps2] could not install exception reporter\n");
        return;
    }
    for (int cause = 1; cause <= 13; cause++)
    {
        if (cause != 8 && cause != 11) // keep syscall and coprocessor-unusable (FPU/VU lazy enable) as they are
        {
            ee_dbg_set_level1_handler(cause, Ps2ExceptionHandler);
        }
    }

    #ifdef RELIVE_PS2_HOSTFS
    // ps2sdk programs run in kernel mode, so a write through a null pointer does not fault: it
    // silently corrupts the EE kernel's low memory. Trap writes to 0x0000-0x0FFF from game code
    // (not from exception level, where the kernel itself runs).
    ee_dbg_set_level2_handler(EE_EXC2_DBG, Ps2DataBreakpointHandler);
    ee_dbg_set_bpw(0x00000000, 0xFFFFF000, EE_BPC_DUE | EE_BPC_DSE | EE_BPC_DKE);
    Ps2Report("[relive-ps2] null-write breakpoint armed\n");
    #endif
}

// Hang watchdog. A vblank interrupt handler samples the interrupted PC (COP0 EPC) into a ring;
// every 5 seconds it wakes a high-priority thread which, if no frame was presented meanwhile,
// prints the most frequent sampled PCs so the stuck loop can be found with addr2line.
extern "C" volatile u32 gPs2FrameHeartbeat = 0;

static constexpr u32 kPcSamples = 256;
static volatile u32 sPcRing[kPcSamples];
static volatile u32 sPcRingPos = 0;
static volatile u32 sVblankCount = 0;
static u32 sHangPeriods = 0; // consecutive 5 s periods without a frame
static s32 sWatchdogSema = -1;
static u8 sWatchdogStack[16 * 1024] __attribute__((aligned(16)));

static int Ps2VblankSampler(int)
{
    u32 epc;
    asm volatile("mfc0 %0, $14" : "=r"(epc));
    sPcRing[sPcRingPos++ % kPcSamples] = epc;
    if (++sVblankCount % 300 == 0)
    {
        iSignalSema(sWatchdogSema);
    }
    return 0;
}

static void Ps2WatchdogThread(void*)
{
    u32 lastHeartbeat = gPs2FrameHeartbeat;
    for (;;)
    {
        WaitSema(sWatchdogSema);
        const u32 heartbeat = gPs2FrameHeartbeat;
        Ps2Report("[relive-ps2] watchdog: vblanks %u frames %u last pc %08X\n", sVblankCount, heartbeat, sPcRing[(sPcRingPos - 1) % kPcSamples]);
        if (heartbeat != lastHeartbeat)
        {
            lastHeartbeat = heartbeat;
            sHangPeriods = 0;
            continue;
        }

        // Count the distinct PCs in the ring and print the busiest ones.
        u32 pcs[kPcSamples];
        u32 counts[kPcSamples];
        u32 distinct = 0;
        for (u32 i = 0; i < kPcSamples; i++)
        {
            const u32 pc = sPcRing[i];
            u32 j = 0;
            while (j < distinct && pcs[j] != pc)
            {
                j++;
            }
            if (j == distinct)
            {
                pcs[distinct] = pc;
                counts[distinct++] = 0;
            }
            counts[j]++;
        }
        Ps2Report("[relive-ps2] *** HANG: no frame for 5 s (heartbeat %u). Busiest PCs:", heartbeat);
        for (u32 n = 0; n < 8 && n < distinct; n++)
        {
            u32 best = 0;
            for (u32 j = 1; j < distinct; j++)
            {
                if (counts[j] > counts[best])
                {
                    best = j;
                }
            }
            Ps2Report(" %08X(%u)", pcs[best], counts[best]);
            counts[best] = 0;
        }
        Ps2Report("\n");
        // Wait for a second hang period before taking the screen: a long load can stall frames once.
        if (++sHangPeriods == 2)
        {
            Ps2ShowReportScreen("CONGELADO: no se dibuja nada desde hace 10 s");
        }
        continue;
        }
}

static void InstallPs2HangWatchdog()
{
    ee_sema_t sema = {};
    sema.init_count = 0;
    sema.max_count = 1;
    sWatchdogSema = CreateSema(&sema);

    ee_thread_t thread = {};
    thread.func = reinterpret_cast<void*>(Ps2WatchdogThread);
    thread.stack = sWatchdogStack;
    thread.stack_size = sizeof(sWatchdogStack);
    thread.gp_reg = &_gp;
    thread.initial_priority = 0; // highest: must run even if the game thread spins
    const s32 threadId = CreateThread(&thread);
    StartThread(threadId, nullptr);
    ee_thread_status_t status = {};
    ReferThreadStatus(GetThreadId(), &status);
    Ps2Report("[relive-ps2] (sio) hang watchdog thread %d, main thread priority %d\n", static_cast<int>(threadId), status.current_priority);

    AddIntcHandler(INTC_VBLANK_S, Ps2VblankSampler, 0);
    EnableIntc(INTC_VBLANK_S);
}

// 32 MB goes fast: report what was being allocated when memory runs out.
void* operator new(size_t size)
{
    void* p = malloc(size ? size : 1);
    if (!p)
    {
        const struct mallinfo mi = mallinfo();
        printf("[relive-ps2] OUT OF MEMORY: new(%u) failed, heap in use %u bytes\n", static_cast<u32>(size), static_cast<u32>(mi.uordblks));
        fflush(stdout);
        throw std::bad_alloc();
    }
    return p;
}

void operator delete(void* p) noexcept
{
    free(p);
}

void operator delete(void* p, size_t) noexcept
{
    free(p);
}
#endif
#include "Sys_common.hpp"

#include "../AliveLibAE/Map.hpp"
#include "../AliveLibAE/Font.hpp"
#include "../AliveLibAE/Abe.hpp"

#if RELIVE_WITH_AO
#include "../AliveLibAO/Map.hpp"
#include "../AliveLibAO/Abe.hpp"
#include "../AliveLibAO/Font.hpp"
#include "../AliveLibAO/Game.hpp"
#include "../AliveLibAO/SwitchStates.hpp"
#endif

#include "../AliveLibAE/GameAutoPlayer.hpp"
#if RELIVE_WITH_AO
#include "../AliveLibAO/GameAutoPlayer.hpp"
#endif

#include "Io.hpp"

enum class GameType : s32
{
    eAo = 0,
    eAe = 1,
};

namespace AutoSplitterData {
struct GuidStr
{
    const s8 guid[39];
};

#if RELIVE_WITH_AO
struct AOGameInfo
{
    GuidStr guid;
    GameType* gameType;    // 0
    AO::LevelIds* levelId; // 1
    s16* pathId;           // 2
    s16* camId;            // 3
    u32* gnFrame;          // 4
    AO::Abe** pAbe;        // 5
    s32 abeYOffSet;        // 6
    s16* isGameRunning;    // 7
    s8* isGameBeaten;      // 8
};
#endif

struct AEGameInfo
{
    GuidStr guid;
    // 1 byte padding/null
    GameType* gameType; // 0
    LevelIds* levelId;  // 1
    s16* pathId;        // 2
    s16* camId;         // 3
    u16* fmvId;         // 4
    u32* gnFrame;       // 5
    Abe** pAbe;         // 6
    s32 abeYOffSet;     // 7
    s8* isPaused;       // 8
};

extern "C"
{
    GameType gameType = GameType::eAo;

// Auto splitter looks for this guid, if it exists then it assumes its the 64bit relive
#ifdef _WIN64
    EXPORT constexpr GuidStr k64BitGuid = {"{069DDB51-609D-49AB-B69D-5CC6D13E73EE}"};

    // If not exported the var will get optimized away
    EXPORT const void* Get64BitInfo()
    {
        return &k64BitGuid;
    }
#endif

#ifdef _WIN32
    EXPORT constexpr AEGameInfo kAeInfo = {
        "{DBC2AE1C-A5DE-465F-A89A-C385BE1DEFCC}",
        // 2 byte padding (32bit)
        &gameType,
        &LocalVar_gMap_5C3030.field_0_current_level,
        &LocalVar_gMap_5C3030.field_2_current_path,
        &LocalVar_gMap_5C3030.field_4_current_camera,
        &LocalVar_gMap_5C3030.field_12_fmv_base_id,
        &LocalVar_sGnFrame_5C1B84,
        &LocalVar_spAbe_554D5C,
        offsetof(Abe, field_BC_ypos),
        &LocalVar_sDisableFontFlicker_5C9304};

    EXPORT const void* GetAeInfo()
    {
        return &kAeInfo;
    }

    EXPORT constexpr AOGameInfo kAoInfo = {
        "{1D2E2B5A-19EE-4776-A0EE-98F49F781370}",
        // 2 byte padding (32bit)
        &gameType,
        &AO::LocalVar_gMap_507BA8.field_0_current_level,
        &AO::LocalVar_gMap_507BA8.field_2_current_path,
        &AO::LocalVar_gMap_507BA8.field_4_current_camera,
        &AO::LocalVar_gnFrameCount_507670,
        &AO::LocalVar_sActiveHero_507678,
        offsetof(AO::Abe, field_AC_ypos) + sizeof(s16), // +2 for exp only
        &AO::LocalVar_sDisableFontFlicker_5080E4,
        &AO::LocalVar_sSwitchStates_505568.mData[70]};

    EXPORT const void* GetAoInfo()
    {
        return &kAoInfo;
    }
#endif
}
} // namespace AutoSplitterData

void PopulateAutoSplitterVars(GameType gameType)
{
    AutoSplitterData::gameType = gameType;
}

static bool FileExists(const char_type* fileName)
{
    FILE* f = fopen(fileName, "r");
    if (f)
    {
        fclose(f);
        return true;
    }
#ifdef __PS2__
    // On a PS2 disc (ISO 9660) the name is upper case with a ";1" version suffix.
    extern const char_type* gPs2DiscDataPrefix; // AliveLibAE/Io.cpp
    char_type isoName[96] = {};
    strcpy(isoName, gPs2DiscDataPrefix);
    size_t i = strlen(isoName);
    for (size_t j = 0; fileName[j] && i < sizeof(isoName) - 3; j++, i++)
    {
        isoName[i] = static_cast<char_type>(toupper(fileName[j]));
    }
    f = fopen(isoName, "r");
    if (!f)
    {
        isoName[i++] = ';';
        isoName[i++] = '1';
        f = fopen(isoName, "r");
    }
    if (f)
    {
        fclose(f);
        return true;
    }
#endif
    return false;
}

// Only used on Windows for logging to help when people have issues launching the game
static void ShowCwd()
{
#ifdef _WIN32
    // Note: Not using SDL_GetBasePath as that returns the executable dir which isn't
    // always the same as the cwd.
    char_type buffer[2048] = {};
    if (::GetCurrentDirectoryA(sizeof(buffer), buffer))
    {
        LOG_INFO("Win32 cwd is: " << buffer);
    }
    else
    {
        LOG_INFO("Failed to get Win32 cwd: " << ::GetLastError());
    }
#elif defined(__PS2__)
    char_type buffer[256] = {};
    LOG_INFO("PS2 cwd is: " << (getcwd(buffer, sizeof(buffer)) ? buffer : "(unknown)"));
#endif
}

static void GameDirListing()
{
    IO_EnumerateDirectory("*.*", [](const char_type* fileName, u32)
                          { LOG_INFO(fileName); });
}

static bool CheckRequiredGameFilesExist(GameType gameType, bool showError)
{
    if (gameType == GameType::eAe)
    {
        if (!FileExists("st.lvl") || !FileExists("mi.lvl"))
        {
            if (showError)
            {
                SDL_Init(SDL_INIT_EVENTS);
                GameDirListing();
                Alive_Show_ErrorMsg("Abes Exoddus/Abes Oddysee cant start because st.lvl or mi.lvl was not found in the working directory. Copy relive files to the root game directory to fix this.");
            }
            return false;
        }
        return true;
    }
    else if (gameType == GameType::eAo)
    {
        if (!FileExists("s1.lvl") || !FileExists("r1.lvl"))
        {
            if (showError)
            {
                SDL_Init(SDL_INIT_EVENTS);
                GameDirListing();
                Alive_Show_ErrorMsg("Abes Oddysee/Abes Exoddus cant start because s1.lvl or r1.lvl was not found in the working directory. Copy relive files to the root game directory to fix this.");
            }
            return false;
        }
        return true;
    }
    else
    {
        return false;
    }
}

#if RELIVE_WITH_AO
static BaseGameAutoPlayer& GetGameAutoPlayerAO()
{
    static AO::GameAutoPlayer autoPlayer;
    return autoPlayer;
}
#endif

static BaseGameAutoPlayer& GetGameAutoPlayerAE()
{
    static GameAutoPlayer autoPlayer;
    return autoPlayer;
}

static BaseGameAutoPlayer* pAutoPlayer = nullptr;

BaseGameAutoPlayer& GetGameAutoPlayer()
{
    return *pAutoPlayer;
}

#if RELIVE_WITH_AO
static s32 AOMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, s32 nShowCmd)
{
    LOG_INFO("AO standalone starting...");
    AO::Static_Inits_AO();
    PopulateAutoSplitterVars(GameType::eAo);
    return AO::WinMain_48EF50(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}
#endif

static s32 AEMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, s32 nShowCmd)
{
    LOG_INFO("AE standalone starting...");
    // In the real game these are called before main, but shouldn't really matter in this case
    Static_Inits_AE();
    PopulateAutoSplitterVars(GameType::eAe);
    return WinMain_4EE631(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}

s32 WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, s32 nShowCmd)
{
    Install_Crash_Handler();
#if _WIN32
    ::AllocConsole();
    ::freopen("CONOUT$", "w", stdout);
    ::SetConsoleTitleA("Debug Console");
    ::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
    RedirectIoStream(true);

    ExportHooker hooker(hInstance);
    hooker.Apply();
#endif
    LOG_INFO("Relive: " << BuildAndBitnesString());

#if !RELIVE_WITH_AO
    ShowCwd();
    if (!CheckRequiredGameFilesExist(GameType::eAe, true))
    {
        return 1;
    }
    pAutoPlayer = &GetGameAutoPlayerAE();
    return AEMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
#else
    // Default to AE but allow switching to AO with a command line, if AO is anywhere in the command line then assume we want to run AO
    GameType gameToRun = strstr(lpCmdLine, "AO") ? GameType::eAo : GameType::eAe;
    ShowCwd();

    if (gameToRun == GameType::eAo)
    {
        LOG_INFO("Checking AO files are present...");
        if (!CheckRequiredGameFilesExist(GameType::eAo, false))
        {
            gameToRun = GameType::eAe;
            LOG_INFO("No AO files found, switch to AE");
        }
    }
    else
    {
        LOG_INFO("Checking AE files are present...");
        if (!CheckRequiredGameFilesExist(GameType::eAe, false))
        {
            gameToRun = GameType::eAo;
            LOG_INFO("No AE files found, switch to AO");
        }
    }

    if (!CheckRequiredGameFilesExist(gameToRun, true))
    {
        return 1;
    }

    if (gameToRun == GameType::eAo)
    {
        pAutoPlayer = &GetGameAutoPlayerAO();
        return AOMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
    }
    else
    {
        pAutoPlayer = &GetGameAutoPlayerAE();
        return AEMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
    }
#endif
}

s32 main(s32 argc, char_type** argv)
{
#ifdef __PS2__
    printf("[relive-ps2] main reached, argv[0]=%s\n", argc > 0 ? argv[0] : "(none)");
    fflush(stdout);
    InstallPs2ExceptionReporter();
    InstallPs2HangWatchdog();
    Ps2UnalignedSelfTest();
    #ifndef RELIVE_PS2_HOSTFS
    Ps2InitIopDrivers();
    // Real console: the game files sit next to the ELF on a USB stick. After SDL_RunApp loads the
    // USB mass-storage driver the stick needs a moment to enumerate, so wait for it (up to 10 s)
    // before the engine checks for st.lvl and gives up.
    // Booted from a disc image (OPL / DVD): the root only holds SYSTEM.CNF and this ELF, the game
    // files are in \DATA (a PS2 disc root with ~150 entries is not read reliably).
    if (argc > 0 && strncmp(argv[0], "cdrom0:", 7) == 0)
    {
        extern const char_type* gPs2DiscDataPrefix; // AliveLibAE/Io.cpp
        // The BIOS cdrom0: device does not take sub-folders through libc paths; ps2sdk's cdfs: does.
        gPs2DiscDataPrefix = "cdfs:/DATA/";
    }
    for (s32 i = 0; i < 100; i++)
    {
        if (FileExists("st.lvl"))
        {
            break;
        }
        SDL_Delay(100);
    }
    {
        extern const char_type* gPs2DiscDataPrefix;
        Ps2Report("[relive-ps2] argv0 '%s' data prefix '%s' st.lvl %s mi.lvl %s\n", argc > 0 ? argv[0] : "", gPs2DiscDataPrefix,
                  FileExists("st.lvl") ? "found" : "MISSING", FileExists("mi.lvl") ? "found" : "MISSING");
    }
    #endif
    // The EE scheduler is strictly priority based: a thread only runs while every higher priority
    // thread is blocked. The game loop never blocks, so at the default priority (1) it starved SDL's
    // audio thread (priority 16) and there was no sound. Run the game below the audio thread.
    ChangeThreadPriority(GetThreadId(), 64);
    #ifdef RELIVE_PS2_HOSTFS
    // TEMP diagnostic: replay the mi.lvl read that freezes the EE during the first level load.
    if (FILE* marker = fopen("ps2iotest.txt", "r"))
    {
        fclose(marker);
        static u8 sTestBuf[32 * 1024] __attribute__((aligned(64)));
        SDL_IOStream* io = SDL_IOFromFile("mi.lvl", "rb");
        Ps2Report("[iotest] open %08X\n", reinterpret_cast<u32>(io));
        if (io)
        {
            for (u32 pass = 0; pass < 3; pass++)
            {
                SDL_SeekIO(io, 0x29C800, SDL_IO_SEEK_SET);
                for (u32 done = 0; done < 124928;)
                {
                    const u32 want = 124928 - done < sizeof(sTestBuf) ? 124928 - done : sizeof(sTestBuf);
                    Ps2Report("[iotest] pass %u read %u at %08X\n", pass, want, static_cast<u32>(SDL_TellIO(io)));
                    const u32 got = static_cast<u32>(SDL_ReadIO(io, sTestBuf, want));
                    Ps2Report("[iotest] got %u\n", got);
                    if (got != want)
                    {
                        break;
                    }
                    done += got;
                }
            }
            SDL_CloseIO(io);
        }
        Ps2Report("[iotest] done\n");
    }
    #endif
#endif
    std::string args;
    for (s32 i = 0; i < argc; i++)
    {
        args += argv[i] + std::string(" ");
    }
    return WinMain(0, 0, const_cast<LPSTR>(args.c_str()), 1);
}

bool RunningAsInjectedDll()
{
    return false;
}

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <unistd.h>

static void __attribute__((constructor)) FixCWD()
{
    char* imagePath = strdup(_dyld_get_image_name(0));
    if (!imagePath)
    {
        return;
    }
    
    for (u32 i = strlen(imagePath); --i;)
    {
        if (imagePath[i] == '/')
        {
            imagePath[i] = 0;
            chdir(imagePath);
            break;
        }
    }
    free(imagePath);
}
#endif
