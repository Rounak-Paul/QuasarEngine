#ifndef ED_CAMERA_H
#define ED_CAMERA_H

#include "qs_renderer.h"
#include "qs_input.h"

/**
 * EditorCamera — Unity-style editor camera controller.
 *
 * Controls:
 *   Right mouse drag          : FPS fly (WASD/QE to move, mouse delta to look)
 *   Alt + Left mouse drag     : Orbit around target
 *   Middle mouse drag         : Pan (translate position + target in camera plane)
 *   Scroll wheel              : Dolly (move position + target along view axis)
 *   F key                     : Focus on a point with a given radius
 */
typedef struct EditorCamera {
    float position[3];      /* World-space eye position           */
    float target[3];        /* World-space look-at point          */
    float yaw;              /* Degrees — horizontal rotation      */
    float pitch;            /* Degrees — vertical rotation (±89)  */
    float orbit_distance;   /* Distance from position to target   */
    float fly_speed;        /* Units per second in fly mode       */
    float zoom_speed;       /* Units per scroll tick              */
    float orbit_speed;      /* Degrees per pixel                  */
    float pan_speed;        /* Units per pixel                    */
} EditorCamera;

/**
 * Initialise an EditorCamera with sensible defaults.
 * The position and target are set to match the editor's initial scene view.
 */
void ed_camera_init(EditorCamera *cam);

/**
 * Poll input and update the camera state, then write position/target/up
 * to the renderer's active Qs_Camera for this frame.
 *
 * @param cam               The editor camera state.
 * @param renderer          The scene renderer whose camera will be updated.
 * @param dt                Delta-time in seconds for the current frame.
 * @param viewport_hovered  True when the mouse is currently inside the scene
 *                          viewport.  Scroll-dolly is suppressed when false so
 *                          scrolling other panels does not move the camera.
 */
void ed_camera_update(EditorCamera *cam, Qs_Renderer *renderer, float dt,
                      bool viewport_hovered);

/**
 * Smoothly reframe the camera to fit a sphere (center + radius) in view.
 * Useful for the "F to focus" operation.
 *
 * @param cam    The editor camera state.
 * @param center World-space centre of the bounding sphere.
 * @param radius Radius of the bounding sphere.
 */
void ed_camera_focus(EditorCamera *cam, const float center[3], float radius);

#endif /* ED_CAMERA_H */
