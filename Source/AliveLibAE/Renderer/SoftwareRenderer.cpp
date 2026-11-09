#include "stdafx.h"
#include "SoftwareRenderer.hpp"
#include "PsxRender.hpp"
#include "Psx.hpp"
#include "VRam.hpp"
#include "../Ps2Coop.hpp"

#ifdef __PS2__
extern "C" volatile u32 gPs2FrameHeartbeat; // hang watchdog, relive/Exe.cpp
#endif

#ifdef RELIVE_PS2_HOSTFS
// Test builds write a frame to the game folder every 300 frames so a run in PCSX2 can be checked
// without looking at the emulator window.
static void DumpFrameBmp(u32 index, const void* pPixels, s32 pitch, s32 w, s32 h, s32 format)
{
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(static_cast<SDL_PixelFormat>(format));
    if (!details || w <= 0 || h <= 0)
    {
        return;
    }

    char_type name[32] = {};
    snprintf(name, sizeof(name), "ps2shot_%05u.bmp", index);
    FILE* f = fopen(name, "wb");
    if (!f)
    {
        return;
    }

    const u32 rowSize = (static_cast<u32>(w) * 3 + 3) & ~3u;
    const u32 dataSize = rowSize * static_cast<u32>(h);
    u8 header[54] = {'B', 'M'};
    auto put32 = [&](u32 off, u32 v)
    {
        header[off] = static_cast<u8>(v);
        header[off + 1] = static_cast<u8>(v >> 8);
        header[off + 2] = static_cast<u8>(v >> 16);
        header[off + 3] = static_cast<u8>(v >> 24);
    };
    put32(2, 54 + dataSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<u32>(w));
    put32(22, static_cast<u32>(h));
    header[26] = 1;
    header[28] = 24;
    put32(34, dataSize);
    fwrite(header, 1, sizeof(header), f);

    std::vector<u8> row(rowSize, 0);
    for (s32 y = h - 1; y >= 0; y--)
    {
        const u8* src = static_cast<const u8*>(pPixels) + y * pitch;
        for (s32 x = 0; x < w; x++)
        {
            u32 pixel = 0;
            memcpy(&pixel, src + x * details->bytes_per_pixel, details->bytes_per_pixel);
            u8 r = 0, g = 0, b = 0;
            SDL_GetRGB(pixel, details, nullptr, &r, &g, &b);
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        fwrite(row.data(), 1, rowSize, f);
    }
    fclose(f);
    LOG_INFO("Frame dump " << name << " (" << w << "x" << h << ", " << SDL_GetPixelFormatName(static_cast<SDL_PixelFormat>(format)) << ")");
}
#endif

void SoftwareRenderer::Destroy()
{
    SDL_DestroyTexture(mBackBufferTexture);
}

bool SoftwareRenderer::Create(TWindowHandleType window)
{
    mRenderer = SDL_CreateRenderer(window, nullptr);
    return mRenderer != nullptr;
}

void SoftwareRenderer::Clear(u8 r, u8 g, u8 b)
{
    SDL_SetRenderDrawColor(mRenderer, r, g, b, 255);
    SDL_RenderClear(mRenderer);
}

void SoftwareRenderer::StartFrame(s32 xOff, s32 yOff)
{
    if (!mFrameStarted)
    {
        mFrame_xOff = xOff;
        mFrame_yOff = yOff;
        mFrameStarted = true;
    }
    else
    {
        LOG_WARNING("Start frame called before EndFrame");
    }
}

void SoftwareRenderer::PalFree(const IRenderer::PalRecord& record)
{
    Pal_free_483390(PSX_Point{record.x, record.y}, record.depth);
}

bool SoftwareRenderer::PalAlloc(IRenderer::PalRecord& record)
{
    PSX_RECT rect = {};
    const bool ret = Pal_Allocate_483110(&rect, record.depth);
    record.x = rect.x;
    record.y = rect.y;
    return ret;
}

void SoftwareRenderer::PalSetData(const IRenderer::PalRecord& record, const u8* pPixels)
{
    PSX_RECT rect = {};
    rect.x = record.x;
    rect.y = record.y;
    rect.w = record.depth;
    rect.h = 1;
    PSX_LoadImage16_4F5E20(&rect, pPixels);
}

void SoftwareRenderer::EndFrame()
{
    SDL_RenderPresent(mRenderer);
#ifdef __PS2__
    gPs2FrameHeartbeat++;
#endif
    mFrameStarted = false;
}

void SoftwareRenderer::BltBackBuffer(const SDL_Rect* pCopyRect, const SDL_Rect* pDst)
{
    SDL_FRect fcopyRect = {};
    SDL_FRect fDst = {};

    if (pCopyRect)
    {
        SDL_RectToFRect(pCopyRect, &fcopyRect);
    }

    if (pDst)
    {
        SDL_RectToFRect(pDst, &fDst);
    }

    SDL_RenderTexture(
        mRenderer,
        mBackBufferTexture,
        pCopyRect ? &fcopyRect : nullptr,
        pDst ? &fDst : nullptr);
}

void SoftwareRenderer::OutputSize(s32* w, s32* h)
{
    SDL_GetCurrentRenderOutputSize(mRenderer, w, h);
}

#ifdef __PS2__
// RGB565 -> ABGR1555, two pixels per 32-bit word. The green low bit is dropped.
static void Convert565To1555(const void* pSrc, s32 srcPitch, void* pDst, s32 dstPitch, s32 w, s32 h)
{
    for (s32 y = 0; y < h; y++)
    {
        const u32* src = reinterpret_cast<const u32*>(static_cast<const u8*>(pSrc) + y * srcPitch);
        u32* dst = reinterpret_cast<u32*>(static_cast<u8*>(pDst) + y * dstPitch);
        for (s32 x = 0; x < w / 2; x++)
        {
            const u32 p = src[x];
            const u32 r = (p >> 11) & 0x001F001F;
            const u32 g = (p >> 6) & 0x001F001F;
            const u32 b = p & 0x001F001F;
            dst[x] = r | (g << 5) | (b << 10) | 0x80008000;
        }
    }
}
#endif

void SoftwareRenderer::SetUploadRect(const SDL_Rect* pRect)
{
    mHasUploadRect = pRect != nullptr;
    if (pRect)
    {
        mUploadRect = *pRect;
    }
}

bool SoftwareRenderer::UpdateBackBuffer(const void* pPixels, s32 pitch)
{
    if (!mBackBufferTexture)
    {
        return false;
    }
#ifdef __PS2__
    if (mConvert565To1555)
    {
        void* pTexPixels = nullptr;
        s32 texPitch = 0;
        if (SDL_LockTexture(mBackBufferTexture, nullptr, &pTexPixels, &texPitch))
        {
            // In game the source is the whole 1024x512 PSX VRAM but only the 640x240 display area is
            // shown: converting just that cut the upload from ~18 ms to a third.
            s32 x = 0;
            s32 y = 0;
            s32 w = mLastW;
            s32 h = mLastH;
            if (mHasUploadRect)
            {
                x = mUploadRect.x & ~1;
                y = mUploadRect.y;
                w = mUploadRect.w + (mUploadRect.x & 1);
                h = mUploadRect.h;
                if (x + w > mLastW)
                {
                    w = mLastW - x;
                }
                if (y + h > mLastH)
                {
                    h = mLastH - y;
                }
            }
            Convert565To1555(static_cast<const u8*>(pPixels) + y * pitch + x * 2, pitch,
                             static_cast<u8*>(pTexPixels) + y * texPitch + x * 2, texPitch, (w + 1) & ~1, h);
            // In-game frames (they come with an upload rect): co-op hints on top of the visible area.
            if (mHasUploadRect)
            {
                Ps2Coop_DrawOverlay(static_cast<u8*>(pTexPixels) + y * texPitch + x * 2, texPitch, w, h);
            }
            // The hint is for this upload only; other callers (credits intro) send full frames.
            mHasUploadRect = false;
    #ifdef RELIVE_PS2_HOSTFS
            // Dump what the GS really receives, to check the colour conversion.
            if ((mFrameCount + 1) == 10 || (mFrameCount + 1) % 600 == 0)
            {
                DumpFrameBmp(mFrameCount + 1, pTexPixels, texPitch, mLastW, mLastH, SDL_PIXELFORMAT_ABGR1555);
            }
    #endif
            SDL_UnlockTexture(mBackBufferTexture);
        }
    }
    else
#endif
    {
        SDL_UpdateTexture(mBackBufferTexture, nullptr, pPixels, pitch);
    }
#ifdef RELIVE_PS2_HOSTFS
    ++mFrameCount;
    if (mFrameCount % 30 == 0)
    {
        LOG_INFO("Frame " << mFrameCount << " at " << SDL_GetTicks() << " ms");
    }
    if (!mConvert565To1555 && (mFrameCount == 10 || mFrameCount % 600 == 0))
    {
        DumpFrameBmp(mFrameCount, pPixels, pitch, mLastW, mLastH, mFormat);
    }
#endif
    return true;
}

void SoftwareRenderer::CreateBackBuffer(bool filter, s32 format, s32 w, s32 h)
{
    if (!mBackBufferTexture || mLastW != w || mLastH != h)
    {
        if (mBackBufferTexture)
        {
            SDL_DestroyTexture(mBackBufferTexture);
        }
        s32 textureFormat = format;
#ifdef __PS2__
        mConvert565To1555 = (format == SDL_PIXELFORMAT_RGB565);
        if (mConvert565To1555)
        {
            textureFormat = SDL_PIXELFORMAT_ABGR1555;
        }
#endif
        mBackBufferTexture = SDL_CreateTexture(mRenderer, static_cast<SDL_PixelFormat>(textureFormat), SDL_TextureAccess::SDL_TEXTUREACCESS_STREAMING, w, h);

        if (filter)
        {
            SDL_SetTextureScaleMode(mBackBufferTexture, SDL_SCALEMODE_LINEAR);
        }
        else
        {
            SDL_SetTextureScaleMode(mBackBufferTexture, SDL_SCALEMODE_NEAREST);
        }

        mLastW = w;
        mLastH = h;
        mFormat = format;
        LOG_INFO("Back buffer " << w << "x" << h << " format " << SDL_GetPixelFormatName(static_cast<SDL_PixelFormat>(format)) << (mBackBufferTexture ? "" : " FAILED: ") << (mBackBufferTexture ? "" : SDL_GetError()));
    }
}

void SoftwareRenderer::SetTPage(s16 tPage)
{
    PSX_TPage_Change_4F6430(tPage);
}

void SoftwareRenderer::SetClip(Prim_PrimClipper& clipper)
{
    sPSX_EMU_DrawEnvState_C3D080.field_0_clip.x = clipper.field_C_x;
    sPSX_EMU_DrawEnvState_C3D080.field_0_clip.y = clipper.field_E_y;
    sPSX_EMU_DrawEnvState_C3D080.field_0_clip.w = clipper.mBase.header.mRect.w;
    sPSX_EMU_DrawEnvState_C3D080.field_0_clip.h = clipper.mBase.header.mRect.h;
    PSX_SetDrawEnv_Impl_4FE420(
        16 * clipper.field_C_x,
        16 * clipper.field_E_y,
        (16 * (clipper.field_C_x + clipper.mBase.header.mRect.w)) - 16,
        (16 * (clipper.field_E_y + clipper.mBase.header.mRect.h)) - 16,
        1000 / 2,
        nullptr);
}

void SoftwareRenderer::SetScreenOffset(Prim_ScreenOffset& /*offset*/)
{
}

void SoftwareRenderer::Draw(Prim_Sprt& sprt)
{
    PrimAny any;
    any.mSprt = &sprt;
    DrawOTag_Render_SPRT(any, static_cast<s16>(mFrame_xOff), static_cast<s16>(mFrame_yOff), any.mSprt->field_14_w, any.mSprt->field_16_h);
}

void SoftwareRenderer::Draw(Prim_GasEffect& gasEffect)
{
    PSX_RenderLaughingGasEffect_4F7B80(gasEffect.x, gasEffect.y, gasEffect.w, gasEffect.h, gasEffect.pData);
}

void SoftwareRenderer::Draw(Prim_Tile& tile)
{
    PrimAny any;
    any.mTile = &tile;
    DrawOTag_Render_TILE(any, static_cast<s16>(mFrame_xOff) + any.mSprt->mBase.vert.x, static_cast<s16>(mFrame_yOff) + any.mSprt->mBase.vert.y, any.mTile->field_14_w, any.mTile->field_16_h);
}

void SoftwareRenderer::Draw(Line_F2& line)
{
    PSX_Render_Line_Prim_4F7D90(&line, static_cast<s16>(mFrame_xOff), static_cast<s16>(mFrame_yOff));
}

void SoftwareRenderer::Draw(Line_G2& line)
{
    PSX_Render_Line_Prim_4F7D90(&line, static_cast<s16>(mFrame_xOff), static_cast<s16>(mFrame_yOff));
}

void SoftwareRenderer::Draw(Line_G4& line)
{
    PSX_Render_Line_Prim_4F7D90(&line, static_cast<s16>(mFrame_xOff), static_cast<s16>(mFrame_yOff));
}

void SoftwareRenderer::Draw(Poly_F3& poly)
{
    PrimAny any;
    any.mPolyF3 = &poly;
    DrawPoly(any);
}

void SoftwareRenderer::Draw(Poly_G3& poly)
{
    PrimAny any;
    any.mPolyG3 = &poly;
    DrawPoly(any);
}

void SoftwareRenderer::Draw(Poly_F4& poly)
{
    PrimAny any;
    any.mPolyF4 = &poly;
    DrawPoly(any);
}

void SoftwareRenderer::Draw(Poly_FT4& poly)
{
    PrimAny any;
    any.mPolyFT4 = &poly;
    DrawPoly(any);
}

void SoftwareRenderer::Draw(Poly_G4& poly)
{
    PrimAny any;
    any.mPolyG4 = &poly;
    DrawPoly(any);
}

void SoftwareRenderer::DrawPoly(PrimAny& any)
{
    // This works by func 1 populating some data structure and then func 2 does the actual rendering
    // for POLY_FT4 it may return nullptr as it s16 circuits this logic and renders the polygon itself in some cases.
    OT_Prim* pPolyBuffer = PSX_Render_Convert_Polys_To_Internal_Format_4F7110(any.mVoid, static_cast<s16>(mFrame_xOff), static_cast<s16>(mFrame_yOff));
    if (pPolyBuffer)
    {
        PSX_Render_Internal_Format_Polygon_4F7960(pPolyBuffer, static_cast<s16>(mFrame_xOff), static_cast<s16>(mFrame_yOff));
    }
}

void SoftwareRenderer::Upload(BitDepth bitDepth, const PSX_RECT& rect, const u8* pPixels)
{
    switch (bitDepth)
    {
        case BitDepth::e16Bit:
            PSX_LoadImage16_4F5E20(&rect, pPixels);
            break;

        case BitDepth::e8Bit:
        case BitDepth::e4Bit:
            PSX_LoadImage_4F5FB0(&rect, pPixels);
            break;

        default:
            ALIVE_FATAL("unknown bit depth");
            break;
    }
}
