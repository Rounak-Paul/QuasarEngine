#ifndef ED_THUMBNAIL_H
#define ED_THUMBNAIL_H

#include "quasar.h"

/* ================================================================
   Asset Thumbnail Service

   GPU-backed thumbnail rendering for .qsmesh and .qstex assets.

   .qsmesh thumbnails are rendered on the GPU with a simple Lambert
   shading pipeline (offscreen 128×128 colour + depth → readback).
   The file I/O and camera computation are performed on a background
   worker thread (engine job system); only the final GPU render stall
   hits the main thread, throttled to THUMB_MESH_GPU_PER_FRAME per
   frame.

   .qstex thumbnails are CPU-decoded (bilinear downsample to 128×128
   RGBA8) on a background thread and uploaded via ca_image_create on
   the main thread (fast staging copy, no render stall).

   All rendered images are cached by absolute path + mtime.  The cache
   holds at most 256 entries and evicts the least-recently-used entry
   on overflow.  In-flight entries are never evicted.

   Lifecycle
   ---------
   ed_thumbnail_init()         — call once after qs_engine_create().
   ed_thumbnail_shutdown()     — call before qs_engine_destroy().
   ed_thumbnail_begin_frame()  — call once per frame before any get().
   ed_thumbnail_get()          — call each frame from the UI sync loop.
   ed_thumbnail_flush()        — invalidates all idle cached images
                                 (useful after a project switch).
   ================================================================ */

/// One-time setup.  Compiles the mesh GPU pipeline and seeds the cache.
void ed_thumbnail_init(Qs_Engine *engine, Ca_Window *window);

/// Teardown.  Waits for in-flight jobs, then destroys all cached Ca_Images
/// and the GPU pipeline.  Must be called before qs_engine_destroy().
void ed_thumbnail_shutdown(void);

/// Resets the per-frame mesh GPU render budget.  Call once per frame,
/// before any ed_thumbnail_get() calls for that frame.
void ed_thumbnail_begin_frame(void);

/// Returns true if any cached entry has a completed background job that
/// has not yet been converted to a Ca_Image, or a mesh entry that is
/// waiting for GPU budget from a previous frame.  Used by the assets panel
/// to force a reconcile on the next frame so thumbnails appear immediately.
bool ed_thumbnail_any_ready(void);

/// Returns a Ca_Image* for @p abs_path, dispatching a background job on
/// first access and returning NULL (placeholder) until the job completes.
/// Passing the current @p mtime_sec triggers a re-render on file change.
/// Returns NULL for unsupported asset types or if rendering fails.
Ca_Image *ed_thumbnail_get(const char *abs_path, int64_t mtime_sec);

/// Destroys all idle cached Ca_Images and marks those slots for fresh
/// re-render on the next get() call.  In-flight entries are left alone.
void ed_thumbnail_flush(void);

#endif /* ED_THUMBNAIL_H */
