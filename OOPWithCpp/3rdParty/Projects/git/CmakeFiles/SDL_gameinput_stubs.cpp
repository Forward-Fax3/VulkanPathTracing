// Stub implementations for SDL GameInput symbols.
// The real GameInput source files require the Windows GameInput SDK (gameinput.h)
// which is unavailable on MinGW. These stubs satisfy the linker for symbols
// referenced unconditionally by other SDL Windows video/joystick source files.

#include "SDL_internal.h"
#include "joystick/SDL_sysjoystick.h"

// Forward declare — the stub doesn't need the full struct definition
struct SDL_VideoDevice;

extern "C" {

// Init returns false, so SDL marks this driver as uninitialized and never
// calls into it. Open/Update/Close are required (non-optional) members.
static bool GameInputStub_Init(void) { return false; }
static bool GameInputStub_Open(SDL_Joystick *, SDL_JoystickDevice *) { return SDL_Unsupported(); }
static void GameInputStub_Update(SDL_Joystick *) {}
static void GameInputStub_Close(SDL_Joystick *) {}
static void GameInputStub_Quit(void) {}

// Field order must match struct SDL_JoystickDriver in joystick/SDL_sysjoystick.h
SDL_JoystickDriver SDL_GAMEINPUT_JoystickDriver = {
    GameInputStub_Init,
    NULL, // InitUpdates
    NULL, // QuitUpdates
    NULL, // QueryDevices
    NULL, // DetectDevices
    NULL, // PrepareDevice
    NULL, // UpdateSteamVirtualGamepadSlot
    NULL, // GetGamepadMapping
    GameInputStub_Open,
    NULL, // Rumble
    NULL, // RumbleTriggers
    NULL, // SetLED
    NULL, // SendEffect
    NULL, // SetSensorsEnabled
    NULL, // SetPlayerIndex
    GameInputStub_Update,
    GameInputStub_Close,
    GameInputStub_Quit,
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
