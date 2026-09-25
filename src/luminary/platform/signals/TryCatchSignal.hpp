///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#ifdef _MSC_VER

#include <functional>
#include <csignal>

namespace Luminary
{

using SignalT = decltype(SIGSEGV);

namespace detail
{

void try_catch_signal_seh(int sigcnt, const SignalT *sigs, std::function<void()> &&fn, std::function<void()> &&cfn);

}

template<class TryFn, class CatchFn, int N>
void try_catch_signal(const SignalT (&sigs)[N], TryFn &&fn, CatchFn &&cfn)
{
    detail::try_catch_signal_seh(N, sigs, fn, cfn);
}

} // namespace Luminary

#else

#include <csignal>

namespace Luminary
{

using SignalT = decltype(SIGSEGV);

// No signal handling off MSVC: the caller's function runs and a fault is fatal, as it was before
// this branch had a namespace.
template<class TryFn, class CatchFn, int N>
void try_catch_signal(const SignalT (& /*sigs*/)[N], TryFn &&fn, CatchFn && /*cfn*/)
{
    fn();
}

} // namespace Luminary

#endif
