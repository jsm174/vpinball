#pragma once

namespace rde
{
namespace StackTrace
{
#if defined (_MSC_VER)
	typedef const void*	Address;
#else
#	error "StackTrace not implemented for this platform!"
#endif

	bool InitSymbols();

	int GetCallStack(Address* callStack, int maxDepth, int entriesToSkip = 0);
	int GetCallStack(void* context, Address* callStack, int maxDepth, int entriesToSkip = 0);
	// Faster than other versions, but may be less reliable (no FPO).
	int GetCallStack_Fast(Address* callStack, int maxDepth, int entriesToSkip = 0);

	// @return	Number of chars taken by symbol info.
	int GetSymbolInfo(Address address, char* symbol, int maxSymbolLen);
	// Retrieves whole callstack (using given context), optionally with 4 first function
	// arguments, in readable form.
	// @pre	vcontext != 0
	void GetCallStack(void* vcontext, bool includeArguments, char* callStackStr, int maxBufferLen);
}

}
