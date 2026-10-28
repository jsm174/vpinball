#import <Cocoa/Cocoa.h>
#include <csignal>

extern "C" char** g_argv;
extern "C" int g_argc;
extern "C" int WinMain(void*, void*, void*, int);

void OnSignalHandler(int signum);
bool OnAppTerminate();

@interface VPXAppDelegate : NSObject <NSApplicationDelegate>
- (void)makeWindowExit;

@end

@implementation VPXAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
  [self makeWindowExit];
}
- (void)makeWindowExit
{
  int status = WinMain(NULL, NULL, NULL, 0);
  exit(status);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
  return OnAppTerminate() ? NSTerminateCancel : NSTerminateNow;
}

- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename
{
  if (g_argc == 1
      && (filename && filename.length > 0 &&
          [[filename.lowercaseString pathExtension] isEqualToString:@"vpx"])) {
    char** new_argv = (char**)malloc(3 * sizeof(char*));

    new_argv[0] = g_argv[0];
    new_argv[1] = strdup("-play");
    new_argv[2] = strdup([filename UTF8String]);

    g_argc = 3;
    g_argv = new_argv;
  }
  [self makeWindowExit];
  return YES;
}

@end

int main(int argc, const char* argv[])
{
  @autoreleasepool {
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = OnSignalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, nullptr);

    g_argc = argc;
    g_argv = (char**)argv;

    NSApplication* vpxApp = [NSApplication sharedApplication];

    NSMenu* mainMenu = [[NSMenu alloc] init];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:[[NSProcessInfo processInfo] processName]]
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    [appMenuItem setSubmenu:appMenu];
    [mainMenu addItem:appMenuItem];
    [vpxApp setMainMenu:mainMenu];

    VPXAppDelegate* delegate = [[VPXAppDelegate alloc] init];
    [vpxApp setDelegate:delegate];
    [vpxApp run];
  }
  return 0;
}
