#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif  // DOOMGENERIC_RESX

#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif  // DOOMGENERIC_RESY


#ifdef CMAP256

typedef uint8_t pixel_t;

#else  // CMAP256

typedef uint32_t pixel_t;

#endif  // CMAP256


extern pixel_t* DG_ScreenBuffer;

#ifdef __cplusplus
extern "C" {
#endif

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();


//Implement below functions for your platform
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

#ifdef DG_PLATFORM_EXIT
// Called instead of returning from I_Quit or exiting from I_Error.
void DG_Quit(void);
void DG_Error(const char *message);
#endif

#ifdef DG_NET_LOBBY
#include "net_defs.h"
// Player name sent when connecting, and the controller's game settings.
const char *DG_NetPlayerName(void);
void DG_NetGameSettings(net_gamesettings_t *settings);
#endif

#ifdef __cplusplus
}
#endif

#endif //DOOM_GENERIC
