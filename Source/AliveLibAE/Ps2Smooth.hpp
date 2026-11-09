#pragma once

// 60 fps presentation for the PS2 port. The game logic keeps running at its original 30 steps per
// second; between two steps an extra frame is drawn with every moving object halfway between its
// previous and its new position, so movement looks twice as smooth. No-ops on other platforms.

struct PrimHeader;

// Records object positions; call right before the objects update for a game step.
void Ps2Smooth_SnapshotPositions();

// After the update: draws and shows the in-between frame, using renderFn to fill the ordering table.
void Ps2Smooth_DrawIntermediateFrame(void (*renderFn)(PrimHeader** ppOt));

// The option itself (on by default on PS2).
void Ps2Smooth_SetEnabled(bool enabled);
bool Ps2Smooth_IsEnabled();
