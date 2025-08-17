#import <Foundation/Foundation.h>

extern void VPinball_SetSwiftCallback(void (*callback)(void*));

extern void vpinball_swift_startup(void* window);

__attribute__((constructor))
static void vpinball_setup_bridge(void)
{
    VPinball_SetSwiftCallback(vpinball_swift_startup);
}
