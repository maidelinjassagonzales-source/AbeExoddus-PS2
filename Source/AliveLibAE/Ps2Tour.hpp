#pragma once

// Test builds only (RELIVE_PS2_HOSTFS): with ps2tour.txt in the game folder, the hero is sent
// through every level-select entry of the game, one every ~20 s, and each stop is logged, so a run
// in PCSX2 checks that every level loads and runs. Call once per game loop frame.
void Ps2Tour_Frame();
