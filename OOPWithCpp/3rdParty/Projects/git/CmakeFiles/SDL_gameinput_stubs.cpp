// Stub implementations for SDL GameInput symbols.
// The real GameInput source files require the Windows GameInput SDK (gameinput.h)
// which is unavailable on MinGW. These stubs satisfy the linker for symbols
// referenced unconditionally by other SDL Windows video/joystick source files.

#include "SDL_internal.h"
#include "joystick/SDL_sysjoystick.h"

// Forward declare — the stub doesn't need the full struct definition
struct SDL_VideoDevice;

extern "C" {

static bool GameInputStub_Init(void) { return false; }
static int  GameInputStub_GetCount(void) { return 0; }
static void GameInputStub_Detect(void) {}
static bool GameInputStub_IsDevicePresent(Uint16, Uint16, Uint16, const char *) { return false; }
static const char *GameInputStub_GetDeviceName(int) { return NULL; }
static const char *GameInputStub_GetDevicePath(int) { return NULL; }
static int  GameInputStub_GetDeviceSteamVirtualGamepadSlot(int) { return -1; }
static int  GameInputStub_GetDevicePlayerIndex(int) { return -1; }
static void GameInputStub_SetDevicePlayerIndex(int, int) {}
static SDL_GUID GameInputStub_GetDeviceGUID(int) { SDL_GUID g = {}; return g; }
static SDL_JoystickID GameInputStub_GetDeviceInstanceID(int) { return 0; }
static bool GameInputStub_Open(SDL_Joystick *, int) { return false; }
static bool GameInputStub_Rumble(SDL_Joystick *, Uint16, Uint16) { return false; }
static bool GameInputStub_RumbleTriggers(SDL_Joystick *, Uint16, Uint16) { return false; }
static bool GameInputStub_SetLED(SDL_Joystick *, Uint8, Uint8, Uint8) { return false; }
static bool GameInputStub_SendEffect(SDL_Joystick *, const void *, int) { return false; }
static bool GameInputStub_SetSensorsEnabled(SDL_Joystick *, bool) { return false; }
static void GameInputStub_Update(SDL_Joystick *) {}
static void GameInputStub_Close(SDL_Joystick *) {}
static void GameInputStub_Quit(void) {}
static bool GameInputStub_GetGamepadMapping(int, SDL_GamepadMapping *) { return false; }

SDL_JoystickDriver SDL_GAMEINPUT_JoystickDriver = {
    GameInputStub_Init,
    GameInputStub_GetCount,
    GameInputStub_Detect,
    GameInputStub_IsDevicePresent,
    GameInputStub_GetDeviceName,
    GameInputStub_GetDevicePath,
    GameInputStub_GetDeviceSteamVirtualGamepadSlot,
    GameInputStub_GetDevicePlayerIndex,
    GameInputStub_SetDevicePlayerIndex,
    GameInputStub_GetDeviceGUID,
    GameInputStub_GetDeviceInstanceID,
    GameInputStub_Open,
    GameInputStub_Rumble,
    GameInputStub_RumbleTriggers,
    GameInputStub_SetLED,
    GameInputStub_SendEffect,
    GameInputStub_SetSensorsEnabled,
    GameInputStub_Update,
    GameInputStub_Close,
    GameInputStub_Quit,
    GameInputStub_GetGamepadMapping,
};

bool SDL_UsingGameInputForXInputControllers(void)
{
    return false;
}

bool WIN_InitGameInput(SDL_VideoDevice *_this)
{
    return SDL_Unsupported();
}

bool WIN_UpdateGameInputEnabled(SDL_VideoDevice *_this)
{
    return SDL_Unsupported();
}

void WIN_UpdateGameInput(SDL_VideoDevice *_this)
{
    return;
}

void WIN_QuitGameInput(SDL_VideoDevice *_this)
{
    return;
}

} // extern "C"
