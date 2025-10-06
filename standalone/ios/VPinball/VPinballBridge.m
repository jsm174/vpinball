#import <Foundation/Foundation.h>

extern void VPinball_SetBridgeCallback(void (*callback)(void*));

extern void vpinballBridgeStartup(void* window);

__attribute__((constructor))
static void vpinball_setup_bridge(void)
{
    VPinball_SetBridgeCallback(vpinballBridgeStartup);
}
