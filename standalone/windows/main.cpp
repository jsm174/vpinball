#include <csignal>
#include <cstdio>
#include <cstdlib>

extern int g_argc;
extern const char** g_argv;
extern "C" int WinMain(void*, void*, void*, int);

static void OnSignalHandler(int signum)
{
   printf("Exiting from signal: %d\n", signum);
   exit(-9999);
}

int main(int argc, const char** argv)
{
   signal(SIGINT, OnSignalHandler);
   g_argc = argc;
   g_argv = argv;
   return WinMain(nullptr, nullptr, nullptr, 0);
}
