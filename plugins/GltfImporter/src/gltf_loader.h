#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "quasar.h"
#include "qs_asset.h"

/// Returns the static Qs_AssetImporterExt vtable for the glTF/GLB importer.
const Qs_AssetImporterExt *gltf_importer_ext(void);

#endif
