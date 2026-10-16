/*
 * sdl_keyboard.c -- the keyboard as player 1 on Linux (SDL), as the Windows
 * build already has it (xinput_device.c, RECOMP_KEYBOARD=1).
 *
 * The SDL renderers drained the window's events and dropped them, and
 * xbox_FramebufferKeyDown is a stub returning 0 outside Windows, so on Linux
 * RECOMP_KEYBOARD did nothing: a title waiting on PRESS START could only be
 * passed with a real controller (Midnight Club 3, 2026-10-16). Now the
 * renderers hand key events here and xbox_InputGetState reads them as pad 1.
 *
 * Keys named after the Xbox buttons, so a prompt on screen says which key
 * ("A OK", "B Back", "Y Delete profile"); the Windows build keeps its
 * Z X A S scheme (xinput_device.c):
 *
 *   arrows        d-pad             Enter      START
 *   A B X Y       A B X Y           Backspace  BACK
 *   Q E           white black       1 3        triggers
 *   numpad 8/2/4/6 left thumb       I/K/J/L    right thumb
 *   Shift / Ctrl  left / right thumb click
 *
 * Keys arrive only while the game window has the focus; losing it releases
 * everything, so a key held while switching away does not stay down.
 * Written by the window thread, read by the USB thread, one byte each.
 */
#if !defined(_WIN32) && !defined(__SWITCH__)

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "xinput_xbox.h"

static volatile unsigned char s_down[SDL_NUM_SCANCODES];

void xbox_sdl_keyboard_event(int scancode, int down)
{
    if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
        s_down[scancode] = down ? 1 : 0;
}

void xbox_sdl_keyboard_reset(void)
{
    memset((void *)s_down, 0, sizeof s_down);
}

int xbox_sdl_keyboard_down(int scancode)
{
    return scancode >= 0 && scancode < SDL_NUM_SCANCODES && s_down[scancode];
}

/* One window event, from the renderer's event loop (nv2a_gl, nv2a_vk). */
void xbox_sdl_keyboard_handle(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        xbox_sdl_keyboard_event(e->key.keysym.scancode, e->type == SDL_KEYDOWN);
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            xbox_sdl_keyboard_reset();
        break;
    default:
        break;
    }
}

int xbox_sdl_keyboard_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_KEYBOARD");
        on = (v && *v && *v != '0') ? 1 : 0;
    }
    return on;
}

static SHORT axis(int negative, int positive)
{
    int v = 0;
    if (s_down[negative]) v -= 32767;
    if (s_down[positive]) v += 32767;
    return (SHORT)v;
}

/* Merge the keyboard into a state (a real pad's, or an empty one): buttons
 * OR'ed, analog buttons at full pressure, sticks where a key is held. */
void xbox_sdl_keyboard_state(XBOX_INPUT_STATE *s)
{
    WORD b = 0;
    BYTE *an = s->Gamepad.bAnalogButtons;
    SHORT x;

    if (s_down[SDL_SCANCODE_UP])        b |= XBOX_GAMEPAD_DPAD_UP;
    if (s_down[SDL_SCANCODE_DOWN])      b |= XBOX_GAMEPAD_DPAD_DOWN;
    if (s_down[SDL_SCANCODE_LEFT])      b |= XBOX_GAMEPAD_DPAD_LEFT;
    if (s_down[SDL_SCANCODE_RIGHT])     b |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (s_down[SDL_SCANCODE_RETURN] || s_down[SDL_SCANCODE_KP_ENTER])
                                        b |= XBOX_GAMEPAD_START;
    if (s_down[SDL_SCANCODE_BACKSPACE]) b |= XBOX_GAMEPAD_BACK;
    if (s_down[SDL_SCANCODE_LSHIFT] || s_down[SDL_SCANCODE_RSHIFT])
                                        b |= XBOX_GAMEPAD_LEFT_THUMB;
    if (s_down[SDL_SCANCODE_LCTRL] || s_down[SDL_SCANCODE_RCTRL])
                                        b |= XBOX_GAMEPAD_RIGHT_THUMB;
    s->Gamepad.wButtons |= b;

    if (s_down[SDL_SCANCODE_A]) an[XBOX_BUTTON_A] = 255;
    if (s_down[SDL_SCANCODE_B]) an[XBOX_BUTTON_B] = 255;
    if (s_down[SDL_SCANCODE_X]) an[XBOX_BUTTON_X] = 255;
    if (s_down[SDL_SCANCODE_Y]) an[XBOX_BUTTON_Y] = 255;
    if (s_down[SDL_SCANCODE_Q]) an[XBOX_BUTTON_WHITE] = 255;
    if (s_down[SDL_SCANCODE_E]) an[XBOX_BUTTON_BLACK] = 255;
    if (s_down[SDL_SCANCODE_1]) an[XBOX_BUTTON_LTRIGGER] = 255;
    if (s_down[SDL_SCANCODE_3]) an[XBOX_BUTTON_RTRIGGER] = 255;

    if ((x = axis(SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6)) != 0) s->Gamepad.sThumbLX = x;
    if ((x = axis(SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_8)) != 0) s->Gamepad.sThumbLY = x;
    if ((x = axis(SDL_SCANCODE_J, SDL_SCANCODE_L)) != 0)       s->Gamepad.sThumbRX = x;
    if ((x = axis(SDL_SCANCODE_K, SDL_SCANCODE_I)) != 0)       s->Gamepad.sThumbRY = x;
}

#endif
