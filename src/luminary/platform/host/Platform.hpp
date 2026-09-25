///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>

namespace Luminary
{

enum class Platform
{
    Uninitialized,
    Unknown,
    Windows,
    OSX,
    Linux,
    BSDUnix,
};

enum class PlatformFlavor
{
    Uninitialized,
    Unknown,
    Generic,         // For Windows and OSX, until we need to be more specific.
    GenericLinux,    // For Platform::Linux
    LinuxOnChromium, // For Platform::Linux
    WSL,             // Microsoft's Windows on Linux (Linux kernel simulated on NTFS kernel)
    WSL2,            // Microsoft's Windows on Linux, version 2 (virtual machine)
    OpenBSD,         // For Platform::BSDUnix
    GenericOSX,      // For Platform::OSX
    OSXOnX86,        // For Apple's on Intel X86 CPU
    OSXOnArm,        // For Apple's on Arm CPU
};

// To be called on program start-up.
void detect_platform();

Platform platform();
PlatformFlavor platform_flavor();

std::string platform_to_string(Platform platform);
std::string platform_flavor_to_string(PlatformFlavor pf);

} // namespace Luminary
