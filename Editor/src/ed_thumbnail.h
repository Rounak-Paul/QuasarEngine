#ifndef ED_THUMBNAIL_H
#define ED_THUMBNAIL_H

#include "quasar.h"

/* ================================================================
   Asset Thumbnail Service

   GPU-backed thumbnail rendering for .qsmesh and .qstex assets.

   .qsmesh thumbnails are rendered on the GPU with a simple Lambert
   shading pipeline (offscreen 128×128 colour + depth → readback).
   .qstex thumbnails are CPU-decoded from the packed binary format
   and uploaded via ca_image_create.

   All rendered images are cached by absolute path + mtime.  The
   cache holds at most THUMB_CACHE_MAX=256 entries and evicts the
   least-recently-used entry on overflow.

   Lifecycle
   ---------
   ed_thumbnail_init()     — call once after qs_engine_create().
   ed_thumbnail_shutdown() — call before qs_engine_destroy().
   ed_thumbnail_get()      — call each frame from the UI sync loop.
   ed_thumbnail_flush()    — optional; invalidates every cached image
                             (useful after a project switch).
   ================================================================ */

/// One-time setup.  Compiles the mesh GPU pipeline and seeds the cache.
void ed_thumbnail_init(Qs_Engine *engine, Ca_Window *window);

/// Teardown.  Destroys all cached Ca_Images and the GPU pipeline.
/// Must be called before qs_engine_destroy() so Vulkan resources are
/// freed while the device is still alive.
void ed_thumbnail_shutdown(void);

/// Returns a Ca_Image* for @p abs_path, rendering it on first access.
/// Passing the current @p mtime_sec triggers a re-render on file change.
/// Returns NULL for unsupported asset types or if rendering fails.
Ca_Image *ed_thumbnail_get(const char *abs_path, int64_t mtime_sec);

/// Destroys all Ca_Images in the cache and marks every slot as
/// un-tried, allowing a fresh render on the next get() call.
void ed_thumbnail_flush(void);

#endif /* ED_THUMBNAIL_H */
