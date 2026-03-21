#ifndef QS_PCH_H
#define QS_PCH_H

/* Standard C */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <float.h>

/* POSIX compat for MSVC */
#ifdef _MSC_VER
  #define strcasecmp _stricmp
#endif

/* Vulkan */
#include <vulkan/vulkan.h>

/* Audio */
#include <AL/al.h>
#include <AL/alc.h>

#endif
