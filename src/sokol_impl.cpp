#ifndef SOKOL_IMPL
    #define SOKOL_IMPL
#endif

#ifndef SOKOL_APP_IMPL
    #define SOKOL_APP_IMPL
#endif
#ifndef SOKOL_GFX_IMPL
    #define SOKOL_GFX_IMPL
#endif
#ifndef SOKOL_GLUE_IMPL
    #define SOKOL_GLUE_IMPL
#endif
#ifndef SOKOL_LOG_IMPL
    #define SOKOL_LOG_IMPL
#endif

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"
