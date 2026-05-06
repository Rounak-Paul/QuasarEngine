#include "ed_camera.h"
#include "qs_input.h"
#include "qs_math.h"
#include "qs_renderer.h"
#include <math.h>
#include <string.h>

/* ================================================================
   ed_camera_init
   ================================================================ */

void ed_camera_init(EditorCamera *cam)
{
    memset(cam, 0, sizeof(*cam));

    /* Default position matches the initial scene view in editor_create */
    cam->position[0] =  5.0f;
    cam->position[1] =  4.0f;
    cam->position[2] =  8.0f;
    cam->target[0]   =  0.0f;
    cam->target[1]   =  0.5f;
    cam->target[2]   =  0.0f;

    /* Derive yaw/pitch from initial look direction */
    float dir[3];
    qs_v3_sub(cam->target, cam->position, dir);
    float len_xz = qs_safe_sqrtf(dir[0]*dir[0] + dir[2]*dir[2]);
    cam->yaw      = qs_to_deg(atan2f(dir[0], dir[2]));
    cam->pitch    = qs_to_deg(atan2f(-dir[1], len_xz));

    /* Orbit distance = magnitude of position→target vector */
    float d[3];
    qs_v3_sub(cam->target, cam->position, d);
    cam->orbit_distance = qs_v3_len(d);

    cam->fly_speed      = 5.0f;
    cam->zoom_speed     = 1.5f;
    cam->orbit_speed    = 0.25f;   /* deg/pixel */
    cam->pan_speed      = 0.004f;  /* units/pixel (scaled by orbit dist) */
}

/* ================================================================
   Internal helpers
   ================================================================ */

/**
 * Build a normalised forward vector from yaw & pitch.
 * Convention: yaw=0 → +Z, increases clockwise viewed from above.
 */
static void cam_forward(float yaw_deg, float pitch_deg, float out[3])
{
    float y = qs_to_rad(yaw_deg);
    float p = qs_to_rad(pitch_deg);
    out[0] = cosf(p) * sinf(y);
    out[1] = -sinf(p);
    out[2] = cosf(p) * cosf(y);
    qs_v3_norm(out, out);
}

/* World up used for all cross-product calculations. */
static const float WORLD_UP[3] = { 0.0f, 1.0f, 0.0f };

/**
 * Derive right and up vectors from a forward direction.
 * If forward ≈ WORLD_UP, fall back to a stable right.
 */
static void cam_axes(const float fwd[3], float right[3], float up[3])
{
    /* right = normalise(fwd × worldUp), but guard against near-parallel */
    float r[3];
    qs_v3_cross(fwd, WORLD_UP, r);
    float rlen = qs_v3_len(r);
    if (rlen < 0.01f) {
        /* Camera pointing nearly straight up/down: use +X as stable right */
        r[0] = 1.0f; r[1] = 0.0f; r[2] = 0.0f;
    } else {
        float inv = 1.0f / rlen;
        r[0] *= inv; r[1] *= inv; r[2] *= inv;
    }

    float u[3];
    qs_v3_cross(r, fwd, u);
    qs_v3_norm(u, u);

    qs_v3_copy(r, right);
    qs_v3_copy(u, up);
}

/* ================================================================
   ed_camera_update
   ================================================================ */

void ed_camera_update(EditorCamera *cam, Qs_Renderer *renderer, float dt,
                      bool viewport_hovered)
{
    if (!cam || !renderer) return;

    float dx = 0.0f, dy = 0.0f;
    qs_input_mouse_delta(&dx, &dy);

    float sdx = 0.0f, sdy = 0.0f;
    qs_input_mouse_scroll(&sdx, &sdy);

    bool right_down  = qs_input_mouse_down(QS_MOUSE_RIGHT);
    bool middle_down = qs_input_mouse_down(QS_MOUSE_MIDDLE);
    bool alt_held    = qs_input_key_down(QS_KEY_LEFT_ALT) ||
                       qs_input_key_down(QS_KEY_RIGHT_ALT);
    bool left_down   = qs_input_mouse_down(QS_MOUSE_LEFT);

    /* ---- FPS fly mode (right mouse held) ---- */
    if (right_down) {
        /* Rotate yaw/pitch */
        cam->yaw   += dx * cam->orbit_speed;
        cam->pitch  = qs_clampf(cam->pitch - dy * cam->orbit_speed, -89.0f, 89.0f);

        /* WASD/QE movement in camera-local space */
        float fwd[3], right[3], up[3];
        cam_forward(cam->yaw, cam->pitch, fwd);
        cam_axes(fwd, right, up);

        float move[3] = { 0.0f, 0.0f, 0.0f };
        float spd = cam->fly_speed * dt;

        if (qs_input_key_down(QS_KEY_W)) {
            float v[3]; qs_v3_scale(fwd,  spd, v); qs_v3_add(move, v, move); }
        if (qs_input_key_down(QS_KEY_S)) {
            float v[3]; qs_v3_scale(fwd, -spd, v); qs_v3_add(move, v, move); }
        if (qs_input_key_down(QS_KEY_A)) {
            float v[3]; qs_v3_scale(right, -spd, v); qs_v3_add(move, v, move); }
        if (qs_input_key_down(QS_KEY_D)) {
            float v[3]; qs_v3_scale(right,  spd, v); qs_v3_add(move, v, move); }
        if (qs_input_key_down(QS_KEY_Q)) {
            float v[3]; qs_v3_scale(up, -spd, v); qs_v3_add(move, v, move); }
        if (qs_input_key_down(QS_KEY_E)) {
            float v[3]; qs_v3_scale(up,  spd, v); qs_v3_add(move, v, move); }

        qs_v3_add(cam->position, move, cam->position);

        /* Recalculate target from position + direction */
        float t[3]; qs_v3_scale(fwd, cam->orbit_distance, t);
        qs_v3_add(cam->position, t, cam->target);
    }

    /* ---- Orbit mode (Alt + left mouse held) ---- */
    else if (alt_held && left_down) {
        cam->yaw   += dx * cam->orbit_speed;
        cam->pitch  = qs_clampf(cam->pitch - dy * cam->orbit_speed, -89.0f, 89.0f);

        /* Recompute position from target + reversed direction */
        float fwd[3];
        cam_forward(cam->yaw, cam->pitch, fwd);
        float back[3];
        qs_v3_scale(fwd, -cam->orbit_distance, back);
        qs_v3_add(cam->target, back, cam->position);
    }

    /* ---- Pan mode (middle mouse held) ---- */
    else if (middle_down) {
        float fwd[3], right[3], up[3];
        cam_forward(cam->yaw, cam->pitch, fwd);
        cam_axes(fwd, right, up);

        /* Scale pan speed by orbit distance so panning feels consistent
           at any zoom level. */
        float scale = cam->pan_speed * cam->orbit_distance;

        float delta[3] = { 0.0f, 0.0f, 0.0f };
        float r[3], u[3];
        qs_v3_scale(right, -dx * scale, r);
        qs_v3_scale(up,     dy * scale, u);
        qs_v3_add(delta, r, delta);
        qs_v3_add(delta, u, delta);

        qs_v3_add(cam->position, delta, cam->position);
        qs_v3_add(cam->target,   delta, cam->target);
    }

    /* ---- Scroll dolly (only when mouse is over the scene viewport) ---- */
    if (viewport_hovered && sdy != 0.0f) {
        float fwd[3];
        cam_forward(cam->yaw, cam->pitch, fwd);
        float dolly[3];
        qs_v3_scale(fwd, sdy * cam->zoom_speed, dolly);
        qs_v3_add(cam->position, dolly, cam->position);
        qs_v3_add(cam->target,   dolly, cam->target);

        /* Clamp orbit distance away from zero */
        float d[3];
        qs_v3_sub(cam->target, cam->position, d);
        cam->orbit_distance = qs_clampf(qs_v3_len(d), 0.1f, 1000.0f);
    }

    /* ---- Write to scene renderer camera ---- */
    Qs_Camera *rc = qs_renderer_camera(renderer);
    if (rc) {
        qs_v3_copy(cam->position, rc->position);
        qs_v3_copy(cam->target,   rc->target);

        /* Derive camera up from yaw/pitch */
        float fwd[3], right[3], up[3];
        cam_forward(cam->yaw, cam->pitch, fwd);
        cam_axes(fwd, right, up);
        qs_v3_copy(up, rc->up);
    }
}

/* ================================================================
   ed_camera_focus
   ================================================================ */

void ed_camera_focus(EditorCamera *cam, const float center[3], float radius)
{
    if (!cam) return;

    radius = radius < 0.1f ? 0.1f : radius;

    /* Choose a distance so the sphere fills roughly 2/3 of the view.
       Based on default 60° vertical FOV: dist = radius / tan(30°) * 1.2 */
    static const float TAN30 = 0.57735026918962576450914878050f; /* tan(PI/6) */
    float dist = (radius / TAN30) * 1.2f;

    qs_v3_copy(center, cam->target);
    cam->orbit_distance = dist;

    /* Keep current yaw/pitch, just move position back from the new target. */
    float fwd[3];
    cam_forward(cam->yaw, cam->pitch, fwd);
    float back[3];
    qs_v3_scale(fwd, -dist, back);
    qs_v3_add(cam->target, back, cam->position);
}
