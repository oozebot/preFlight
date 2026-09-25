///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
// Spelled #include rather than #import so the configure-time dependency firewall reads the line.
#include "luminary/platform/paths/Paths.hpp"

#import <Foundation/Foundation.h>

// The macOS arm of the GetDataDir declared in luminary/platform/paths/Paths.hpp.
std::string GetDataDir()
{
	NSURL* url = [[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory
												 inDomain:NSUserDomainMask
												 appropriateForURL:nil create:NO error:nil];

	return std::string([url.path UTF8String]);
}

