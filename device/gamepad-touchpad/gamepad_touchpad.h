#pragma once

#include <SDL_gamecontroller.h>

/**
 * Exclusive access to a game controller's touchpad input device.
 *
 * Some platforms - notably webOS - route the touchpad of a DualShock 4 or
 * DualSense into the system's touchscreen pipeline, where the compositor
 * consumes edge swipes as navigation gestures before any client sees them.
 * Grabbing the device takes it away from the platform input stack entirely.
 *
 * This only asks the system to stay out of the way. Reading the touchpad is
 * left to SDL's own controller touchpad events.
 */

typedef struct gamepad_touchpad_t gamepad_touchpad_t;

/**
 * Resolve the touchpad input device belonging to a controller and take
 * exclusive access to it.
 *
 * @param controller An opened game controller.
 * @return Handle to release later, or NULL if the controller has no separate
 *         touchpad device or it could not be grabbed.
 */
gamepad_touchpad_t *gamepad_touchpad_grab(SDL_GameController *controller);

/**
 * Release exclusive access and close the device. Safe to call with NULL.
 */
void gamepad_touchpad_release(gamepad_touchpad_t *touchpad);
