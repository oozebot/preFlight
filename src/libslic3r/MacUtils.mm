///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#import "Utils/DirectoriesUtils.hpp"

#import <Foundation/Foundation.h>

// Utils/DirectoriesUtils.hpp
std::string GetDataDir()
{
	NSURL* url = [[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory
												 inDomain:NSUserDomainMask
												 appropriateForURL:nil create:NO error:nil];

	return std::string([url.path UTF8String]);
}

