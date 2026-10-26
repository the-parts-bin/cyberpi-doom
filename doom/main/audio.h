#pragma once

// Enables the speaker amp and starts the mixer task on core 0. Must run before
// doomgeneric_Create: OPL detection waits on the mixer to advance time.
void audio_init(void);
