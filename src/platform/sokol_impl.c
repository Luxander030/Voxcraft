// sokol implementation unit for Windows (D3D11 backend) and other non-Apple platforms.
#define SOKOL_IMPL
#include "sokol/sokol_log.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_time.h"

int vx_platform_window_id(void) { return 0; }
void vx_platform_activate(void) {}
