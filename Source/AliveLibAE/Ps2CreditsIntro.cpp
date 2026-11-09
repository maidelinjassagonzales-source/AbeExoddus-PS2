#include "stdafx.h"
#include "Ps2CreditsIntro.hpp"

#ifdef __PS2__

    #include "Ps2CreditsText.hpp"
    #include "Ps2Trace.hpp"
    #include "Renderer/IRenderer.hpp"
    #include <SDL3/SDL.h>
    #include <cmath>
    #include <vector>

// Credits intro for the PS2 port: golden embers rising over a dark green glow, a rotating ring
// of lights (a nod to the Oddworld logo) and the port credits fading in with a light sweep.
// Drawn in software into a 640x480 RGB565 frame and shown through the game's own renderer.

namespace {

constexpr s32 kW = 640;
constexpr s32 kH = 480;
constexpr f32 kDuration = 8.5f;

struct Rgb final
{
    s32 r, g, b;
};

inline u16 Pack(s32 r, s32 g, s32 b)
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return static_cast<u16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline Rgb Unpack(u16 p)
{
    const s32 r = (p >> 11) & 0x1F;
    const s32 g = (p >> 5) & 0x3F;
    const s32 b = p & 0x1F;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}

// 0..1 ramp between t0 and t1.
inline f32 Ramp(f32 t, f32 t0, f32 t1)
{
    if (t <= t0)
    {
        return 0.0f;
    }
    if (t >= t1)
    {
        return 1.0f;
    }
    const f32 x = (t - t0) / (t1 - t0);
    return x * x * (3.0f - 2.0f * x); // smoothstep
}

// Adds light (glows, embers): additive blend clamped to white.
inline void AddLight(u16* pFrame, s32 x, s32 y, s32 r, s32 g, s32 b)
{
    if (x < 0 || y < 0 || x >= kW || y >= kH)
    {
        return;
    }
    u16& px = pFrame[y * kW + x];
    const Rgb c = Unpack(px);
    px = Pack(c.r + r, c.g + g, c.b + b);
}

void DrawGlow(u16* pFrame, f32 cx, f32 cy, f32 radius, Rgb colour, f32 intensity)
{
    const s32 r = static_cast<s32>(radius) + 1;
    for (s32 dy = -r; dy <= r; dy++)
    {
        for (s32 dx = -r; dx <= r; dx++)
        {
            const f32 d = std::sqrt(static_cast<f32>(dx * dx + dy * dy)) / radius;
            if (d >= 1.0f)
            {
                continue;
            }
            const f32 k = (1.0f - d) * (1.0f - d) * intensity;
            AddLight(pFrame, static_cast<s32>(cx) + dx, static_cast<s32>(cy) + dy,
                     static_cast<s32>(colour.r * k), static_cast<s32>(colour.g * k), static_cast<s32>(colour.b * k));
        }
    }
}

// Blends a text mask, tinted, with an optional moving highlight band.
void DrawText(u16* pFrame, const Ps2CreditsMask& mask, s32 cy, f32 alpha, Rgb colour, f32 sweepX)
{
    if (alpha <= 0.0f)
    {
        return;
    }
    const s32 x0 = (kW - mask.mWidth) / 2;
    const s32 y0 = cy - mask.mHeight / 2;
    for (s32 pass = 0; pass < 2; pass++)
    {
        // pass 0: dark drop shadow, pass 1: the text itself
        const s32 ox = pass == 0 ? 3 : 0;
        const s32 oy = pass == 0 ? 3 : 0;
        for (s32 y = 0; y < mask.mHeight; y++)
        {
            const s32 py = y0 + y + oy;
            if (py < 0 || py >= kH)
            {
                continue;
            }
            const u8* pRow = mask.mAlpha + y * mask.mWidth;
            for (s32 x = 0; x < mask.mWidth; x++)
            {
                const s32 a = pRow[x];
                if (a == 0)
                {
                    continue;
                }
                const s32 px = x0 + x + ox;
                if (px < 0 || px >= kW)
                {
                    continue;
                }
                const s32 k = static_cast<s32>(a * alpha); // 0..255
                u16& dst = pFrame[py * kW + px];
                const Rgb d = Unpack(dst);
                Rgb c = pass == 0 ? Rgb{0, 0, 0} : colour;
                if (pass == 1)
                {
                    // Light sweep: brighten a soft band travelling left to right, plus a vertical gradient.
                    const f32 band = 1.0f - std::fabs(static_cast<f32>(px) - sweepX) / 60.0f;
                    const s32 boost = band > 0.0f ? static_cast<s32>(band * 140.0f) : 0;
                    const s32 grad = (mask.mHeight / 2 - y) * 40 / (mask.mHeight > 0 ? mask.mHeight : 1);
                    c = {c.r + boost + grad, c.g + boost + grad, c.b + boost / 2};
                }
                const s32 kk = pass == 0 ? k * 3 / 5 : k;
                dst = Pack(d.r + (c.r - d.r) * kk / 255, d.g + (c.g - d.g) * kk / 255, d.b + (c.b - d.b) * kk / 255);
            }
        }
    }
}

struct Ember final
{
    f32 x, y, speed, sway, phase, size;
};

void BuildBackground(std::vector<u16>& bg)
{
    // Dark green radial glow behind the ring, fading to black at the edges (vignette).
    for (s32 y = 0; y < kH; y++)
    {
        for (s32 x = 0; x < kW; x++)
        {
            const f32 dx = (x - kW / 2) / 380.0f;
            const f32 dy = (y - 150) / 300.0f;
            f32 k = 1.0f - std::sqrt(dx * dx + dy * dy);
            k = k < 0.0f ? 0.0f : k * k;
            // 4x4 ordered dither: RGB565 has too few greens for a smooth glow and showed rings.
            static const s32 kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            const s32 d = kBayer[(y & 3) * 4 + (x & 3)];
            bg[y * kW + x] = Pack(static_cast<s32>(6 + 20 * k) + d / 2, static_cast<s32>(12 + 70 * k) + d / 4, static_cast<s32>(8 + 30 * k) + d / 2);
        }
    }
}

bool SkipRequested()
{
    SDL_Event e;
    bool skip = false;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type == SDL_EVENT_KEY_DOWN)
        {
            skip = true;
        }
    }
    return skip;
}

} // namespace

void Ps2_PlayCreditsIntro()
{
    IRenderer* pRenderer = IRenderer::GetRenderer();
    if (!pRenderer)
    {
        return;
    }

    std::vector<u16> background(kW * kH);
    std::vector<u16> frame(kW * kH);
    BuildBackground(background);

    std::vector<Ember> embers(110);
    u32 seed = 0x5EED1998u;
    auto rnd = [&seed]()
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<f32>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    for (Ember& e : embers)
    {
        e = {rnd() * kW, rnd() * kH, 18.0f + rnd() * 40.0f, 6.0f + rnd() * 16.0f, rnd() * 6.283f, 1.5f + rnd() * 2.5f};
    }

    pRenderer->CreateBackBuffer(false, SDL_PIXELFORMAT_RGB565, kW, kH);

    const Rgb gold = {255, 196, 92};
    const Rgb ember = {255, 140, 40};
    const Rgb ringLight = {120, 255, 150};

    const u64 start = SDL_GetTicks();
    u64 last = start;
    f32 skipAt = -1.0f;
    PS2_TRACE("credits_start", static_cast<u32>(start));
    for (u32 frameNo = 0;; frameNo++)
    {
        const u64 now = SDL_GetTicks();
        if ((frameNo % 16) == 0)
        {
            PS2_TRACE("credits_frame", frameNo, static_cast<u32>(now - start));
        }
        // Safety net: never more than ~12 s worth of frames, even if the timer misbehaves.
        if (frameNo > 720)
        {
            break;
        }
        const f32 t = (now - start) / 1000.0f;
        const f32 dt = (now - last) / 1000.0f;
        last = now;

        if (skipAt < 0.0f && t > 0.5f && SkipRequested())
        {
            skipAt = t;
        }
        const f32 end = skipAt >= 0.0f ? skipAt + 0.5f : kDuration;
        if (t >= end)
        {
            break;
        }

        // Global fade in/out.
        const f32 fadeOutStart = skipAt >= 0.0f ? skipAt : kDuration - 1.0f;
        const f32 master = Ramp(t, 0.0f, 0.8f) * (1.0f - Ramp(t, fadeOutStart, end));

        memcpy(frame.data(), background.data(), frame.size() * sizeof(u16));

        // Embers rising and swaying.
        for (Ember& e : embers)
        {
            e.y -= e.speed * dt;
            if (e.y < -10.0f)
            {
                e.y = kH + 10.0f;
                e.x = rnd() * kW;
            }
            const f32 x = e.x + std::sin(t * 1.3f + e.phase) * e.sway;
            const f32 flicker = 0.6f + 0.4f * std::sin(t * 7.0f + e.phase * 3.0f);
            DrawGlow(frame.data(), x, e.y, e.size * 1.8f, ember, 0.9f * flicker);
        }

        // Ring of twelve lights turning around the top centre.
        const f32 ringAlpha = Ramp(t, 0.3f, 1.3f);
        if (ringAlpha > 0.0f)
        {
            const f32 cx = kW / 2.0f;
            const f32 cy = 118.0f;
            const f32 radius = 52.0f;
            for (s32 i = 0; i < 64; i++)
            {
                const f32 a = i * 6.283f / 64.0f;
                DrawGlow(frame.data(), cx + std::cos(a) * radius, cy + std::sin(a) * radius, 2.0f, ringLight, 0.35f * ringAlpha);
            }
            for (s32 i = 0; i < 12; i++)
            {
                const f32 a = t * 0.9f + i * 6.283f / 12.0f;
                const f32 pulse = 0.55f + 0.45f * std::sin(t * 3.0f + i * 0.9f);
                DrawGlow(frame.data(), cx + std::cos(a) * radius, cy + std::sin(a) * radius, 9.0f, ringLight, 1.2f * pulse * ringAlpha);
            }
            DrawGlow(frame.data(), cx, cy, 34.0f, gold, (0.5f + 0.2f * std::sin(t * 2.0f)) * ringAlpha);
        }

        // Credits.
        const f32 sweep = -100.0f + (t - 4.2f) * 900.0f;
        DrawText(frame.data(), kTextPortFor, 200, Ramp(t, 1.0f, 1.6f), {200, 230, 200}, -1000.0f);
        DrawText(frame.data(), kTextAroy, 262, Ramp(t, 1.8f, 2.6f), gold, sweep);
        DrawText(frame.data(), kTextAnd, 322, Ramp(t, 2.8f, 3.2f), {220, 180, 110}, sweep);
        DrawText(frame.data(), kTextWilliam, 378, Ramp(t, 3.2f, 4.0f), gold, sweep - 120.0f);
        DrawText(frame.data(), kTextGame, 445, Ramp(t, 4.5f, 5.0f), {150, 190, 150}, -1000.0f);

        // Apply the master fade.
        if (master < 0.999f)
        {
            const s32 k = static_cast<s32>(master * 256.0f);
            for (u16& px : frame)
            {
                const Rgb c = Unpack(px);
                px = Pack(c.r * k >> 8, c.g * k >> 8, c.b * k >> 8);
            }
        }

        pRenderer->UpdateBackBuffer(frame.data(), kW * static_cast<s32>(sizeof(u16)));
        s32 outW = 0;
        s32 outH = 0;
        pRenderer->OutputSize(&outW, &outH);
        SDL_Rect dst = {0, 0, outW, outH};
        pRenderer->Clear(0, 0, 0);
        pRenderer->BltBackBuffer(nullptr, &dst);
        pRenderer->EndFrame();
    }
    PS2_TRACE("credits_end");
}

#else

void Ps2_PlayCreditsIntro()
{
}

#endif
