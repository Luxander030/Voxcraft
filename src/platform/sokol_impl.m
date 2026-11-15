// sokol implementation unit for macOS (Objective-C, Metal backend). The only Objective-C in the project.
#define SOKOL_IMPL
#include "sokol/sokol_log.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_time.h"
#include "sokol/sokol_audio.h"

#import <Cocoa/Cocoa.h>
// Platform helpers used by src/app (C linkage).
int vx_platform_window_id(void) { NSWindow* w = (__bridge NSWindow*)sapp_macos_get_window(); return w ? (int)[w windowNumber] : 0; }
void vx_platform_activate(void) { [NSApp activateIgnoringOtherApps:YES]; }
