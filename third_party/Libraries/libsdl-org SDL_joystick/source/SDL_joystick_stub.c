#include <SDL.h>
#include <SDL_joystick.h>

int SDL_JoystickEventState(int state) {
    return state;
}

SDL_Joystick *SDL_JoystickOpen(int device_index) {
    return NULL;
}

void SDL_JoystickClose(SDL_Joystick *joystick) {}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick) {
    return -1;
}
