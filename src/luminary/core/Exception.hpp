///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2021 Vojtěch Bubník @bubnikv, David Kocík @kocikdav
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stdexcept>

namespace Luminary
{

// preFlight's own exception hierarchy is derived from std::runtime_error.
// Base for Slicer's own exceptions.
class Exception : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};
#define PREFLIGHT_DERIVE_EXCEPTION(DERIVED_EXCEPTION, PARENT_EXCEPTION) \
    class DERIVED_EXCEPTION : public PARENT_EXCEPTION                   \
    {                                                                   \
        using PARENT_EXCEPTION::PARENT_EXCEPTION;                       \
    }
// Critical exception produced by Slicer, such exception shall never propagate up to the UI thread.
// If that happens, wx reports it in an unhandled exception message box.
PREFLIGHT_DERIVE_EXCEPTION(CriticalException, Exception);
PREFLIGHT_DERIVE_EXCEPTION(RuntimeError, CriticalException);
PREFLIGHT_DERIVE_EXCEPTION(LogicError, CriticalException);
PREFLIGHT_DERIVE_EXCEPTION(HardCrash, CriticalException);
PREFLIGHT_DERIVE_EXCEPTION(InvalidArgument, LogicError);
PREFLIGHT_DERIVE_EXCEPTION(OutOfRange, LogicError);
PREFLIGHT_DERIVE_EXCEPTION(IOError, CriticalException);
PREFLIGHT_DERIVE_EXCEPTION(FileIOError, IOError);
PREFLIGHT_DERIVE_EXCEPTION(HostNetworkError, IOError);
PREFLIGHT_DERIVE_EXCEPTION(ExportError, CriticalException);
PREFLIGHT_DERIVE_EXCEPTION(PlaceholderParserError, RuntimeError);
// Runtime exception produced by Slicer. Such exception cancels the slicing process and it shall be shown in notifications.
PREFLIGHT_DERIVE_EXCEPTION(SlicingError, Exception);
#undef PREFLIGHT_DERIVE_EXCEPTION

} // namespace Luminary
