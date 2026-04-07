/**
 * qs_math.h — Shared inline math utilities for the Quasar engine.
 *
 * Column-major 4×4 matrices, 3-component vectors, common transforms.
 * All functions are static inline to avoid link-time duplication issues.
 */
#ifndef QS_MATH_H
#define QS_MATH_H

#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef QS_PI
#  define QS_PI 3.14159265358979323846f
#endif

/* ================================================================
   Scalar helpers
   ================================================================ */

static inline float qs_to_rad(float deg)  { return deg * (QS_PI / 180.0f); }
static inline float qs_to_deg(float rad)  { return rad * (180.0f / QS_PI); }
static inline float qs_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float qs_minf(float a, float b) { return a < b ? a : b; }
static inline float qs_maxf(float a, float b) { return a > b ? a : b; }
static inline float qs_absf(float v)          { return v < 0.0f ? -v : v; }
static inline float qs_safe_sqrtf(float v)    { return sqrtf(v < 0.0f ? 0.0f : v); }

/* ================================================================
   Vec3
   ================================================================ */

static inline void qs_v3_set(float out[3], float x, float y, float z)
{
    out[0] = x; out[1] = y; out[2] = z;
}

static inline void qs_v3_copy(const float src[3], float dst[3])
{
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

static inline void qs_v3_add(const float a[3], const float b[3], float out[3])
{
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}

static inline void qs_v3_sub(const float a[3], const float b[3], float out[3])
{
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}

static inline void qs_v3_scale(const float v[3], float s, float out[3])
{
    out[0] = v[0] * s; out[1] = v[1] * s; out[2] = v[2] * s;
}

static inline float qs_v3_dot(const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static inline float qs_v3_len(const float v[3])
{
    return qs_safe_sqrtf(qs_v3_dot(v, v));
}

static inline void qs_v3_norm(const float v[3], float out[3])
{
    float l = qs_v3_len(v);
    if (l < 1e-7f) { out[0] = 0; out[1] = 0; out[2] = 0; return; }
    float i = 1.0f / l;
    out[0] = v[0] * i; out[1] = v[1] * i; out[2] = v[2] * i;
}

static inline void qs_v3_cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

/* ================================================================
   Mat4 (column-major, compatible with Vulkan / OpenGL conventions)
   ================================================================ */

static inline void qs_m4_identity(float m[16])
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static inline void qs_m4_mul(const float a[16], const float b[16], float out[16])
{
    float tmp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            tmp[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1]
                        + a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
    memcpy(out, tmp, 64);
}

static inline void qs_m4_mul_v4(const float m[16], const float v[4], float out[4])
{
    out[0] = m[0]*v[0] + m[4]*v[1] + m[ 8]*v[2] + m[12]*v[3];
    out[1] = m[1]*v[0] + m[5]*v[1] + m[ 9]*v[2] + m[13]*v[3];
    out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14]*v[3];
    out[3] = m[3]*v[0] + m[7]*v[1] + m[11]*v[2] + m[15]*v[3];
}

static inline bool qs_m4_inverse(const float m[16], float inv[16])
{
    float t[12];
    t[ 0]=m[10]*m[15]-m[14]*m[11]; t[ 1]=m[ 6]*m[15]-m[14]*m[ 7];
    t[ 2]=m[ 6]*m[11]-m[10]*m[ 7]; t[ 3]=m[ 2]*m[15]-m[14]*m[ 3];
    t[ 4]=m[ 2]*m[11]-m[10]*m[ 3]; t[ 5]=m[ 2]*m[ 7]-m[ 6]*m[ 3];
    t[ 6]=m[ 8]*m[13]-m[12]*m[ 9]; t[ 7]=m[ 4]*m[13]-m[12]*m[ 5];
    t[ 8]=m[ 4]*m[ 9]-m[ 8]*m[ 5]; t[ 9]=m[ 0]*m[13]-m[12]*m[ 1];
    t[10]=m[ 0]*m[ 9]-m[ 8]*m[ 1]; t[11]=m[ 0]*m[ 5]-m[ 4]*m[ 1];

    inv[ 0]=  m[ 5]*t[0] - m[ 9]*t[1] + m[13]*t[2];
    inv[ 1]=-(m[ 1]*t[0] - m[ 9]*t[3] + m[13]*t[4]);
    inv[ 2]=  m[ 1]*t[1] - m[ 5]*t[3] + m[13]*t[5];
    inv[ 3]=-(m[ 1]*t[2] - m[ 5]*t[4] + m[ 9]*t[5]);
    inv[ 4]=-(m[ 4]*t[0] - m[ 8]*t[1] + m[12]*t[2]);
    inv[ 5]=  m[ 0]*t[0] - m[ 8]*t[3] + m[12]*t[4];
    inv[ 6]=-(m[ 0]*t[1] - m[ 4]*t[3] + m[12]*t[5]);
    inv[ 7]=  m[ 0]*t[2] - m[ 4]*t[4] + m[ 8]*t[5];
    inv[ 8]=  m[ 7]*t[6] - m[11]*t[7] + m[15]*t[8];
    inv[ 9]=-(m[ 3]*t[6] - m[11]*t[9] + m[15]*t[10]);
    inv[10]=  m[ 3]*t[7] - m[ 7]*t[9] + m[15]*t[11];
    inv[11]=-(m[ 3]*t[8] - m[ 7]*t[10]+ m[11]*t[11]);
    inv[12]=-(m[ 6]*t[6] - m[10]*t[7] + m[14]*t[8]);
    inv[13]=  m[ 2]*t[6] - m[10]*t[9] + m[14]*t[10];
    inv[14]=-(m[ 2]*t[7] - m[ 6]*t[9] + m[14]*t[11]);
    inv[15]=  m[ 2]*t[8] - m[ 6]*t[10]+ m[10]*t[11];

    float det = m[0]*inv[0] + m[4]*inv[1] + m[8]*inv[2] + m[12]*inv[3];
    if (qs_absf(det) < 1e-12f) return false;
    float idet = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] *= idet;
    return true;
}

/* ================================================================
   Camera / projection matrices (Vulkan conventions: Y-flipped, depth [0,1])
   ================================================================ */

static inline void qs_m4_look_at(float out[16], const float eye[3],
                                  const float center[3], const float up[3])
{
    float fx = center[0]-eye[0], fy = center[1]-eye[1], fz = center[2]-eye[2];
    float len = qs_safe_sqrtf(fx*fx + fy*fy + fz*fz);
    if (len > 1e-6f) { fx /= len; fy /= len; fz /= len; }
    float sx = fy*up[2] - fz*up[1], sy = fz*up[0] - fx*up[2], sz = fx*up[1] - fy*up[0];
    len = qs_safe_sqrtf(sx*sx + sy*sy + sz*sz);
    if (len > 1e-6f) { sx /= len; sy /= len; sz /= len; }
    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;
    memset(out, 0, 64);
    out[0] = sx;  out[4] = sy;  out[8]  = sz;  out[12] = -(sx*eye[0]+sy*eye[1]+sz*eye[2]);
    out[1] = ux;  out[5] = uy;  out[9]  = uz;  out[13] = -(ux*eye[0]+uy*eye[1]+uz*eye[2]);
    out[2] = -fx; out[6] = -fy; out[10] = -fz; out[14] =  (fx*eye[0]+fy*eye[1]+fz*eye[2]);
    out[3] = 0;   out[7] = 0;   out[11] = 0;   out[15] = 1.0f;
}

static inline void qs_m4_perspective(float out[16], float fov_rad, float aspect,
                                      float near_p, float far_p)
{
    memset(out, 0, 64);
    float t = tanf(fov_rad * 0.5f);
    out[0]  =  1.0f / (aspect * t);
    out[5]  = -1.0f / t;                               /* Vulkan Y-flip */
    out[10] = -(far_p + near_p) / (far_p - near_p);
    out[11] = -1.0f;
    out[14] = -(2.0f * far_p * near_p) / (far_p - near_p);
}

static inline void qs_m4_ortho(float out[16], float half_h, float aspect,
                                float near_p, float far_p)
{
    memset(out, 0, 64);
    float half_w = half_h * aspect;
    out[0]  =  1.0f / half_w;
    out[5]  = -1.0f / half_h;                          /* Vulkan Y-flip */
    out[10] = -2.0f / (far_p - near_p);
    out[14] = -(far_p + near_p) / (far_p - near_p);
    out[15] =  1.0f;
}

static inline void qs_m4_ortho_lrtbnf(float out[16],
                                       float l, float r, float b, float t,
                                       float n, float f)
{
    memset(out, 0, 64);
    out[0]  =  2.0f / (r - l);
    out[5]  =  2.0f / (t - b);
    out[10] = -2.0f / (f - n);
    out[12] = -(r + l) / (r - l);
    out[13] = -(t + b) / (t - b);
    out[14] = -(f + n) / (f - n);
    out[15] =  1.0f;
}

/* ================================================================
   Quaternion → TRS matrix
   ================================================================ */

static inline void qs_m4_from_trs(float m[16],
                                   const float pos[3],
                                   const float quat[4],
                                   const float scl[3])
{
    float qx = quat[0], qy = quat[1], qz = quat[2], qw = quat[3];
    float sx = scl[0],  sy = scl[1],  sz = scl[2];
    float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;
    m[ 0] = (1.0f - 2.0f*(yy+zz)) * sx;  m[ 1] = (2.0f*(xy+wz)) * sx;
    m[ 2] = (2.0f*(xz-wy)) * sx;          m[ 3] = 0.0f;
    m[ 4] = (2.0f*(xy-wz)) * sy;          m[ 5] = (1.0f - 2.0f*(xx+zz)) * sy;
    m[ 6] = (2.0f*(yz+wx)) * sy;          m[ 7] = 0.0f;
    m[ 8] = (2.0f*(xz+wy)) * sz;          m[ 9] = (2.0f*(yz-wx)) * sz;
    m[10] = (1.0f - 2.0f*(xx+yy)) * sz;   m[11] = 0.0f;
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2]; m[15] = 1.0f;
}

#endif /* QS_MATH_H */
