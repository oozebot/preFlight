///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstdio>
#include <functional>
#include <type_traits>
#include <utility>

namespace Luminary
{

#if defined __GNUC__ && __GNUC__ < 5 && !defined __clang__
// Older GCCs don't have std::is_trivially_copyable
// cf. https://gcc.gnu.org/onlinedocs/gcc-4.9.4/libstdc++/manual/manual/status.html#status.iso.2011
// #warning "GCC version < 5, faking std::is_trivially_copyable"
template<typename T>
struct IsTriviallyCopyable
{
    static constexpr bool value = true;
};
#else
template<typename T>
struct IsTriviallyCopyable : public std::is_trivially_copyable<T>
{
};
#endif

// A very lightweight ROII wrapper around C FILE.
// The old C file API is much faster than C++ streams, thus they are recommended for processing large / huge files.
struct FilePtr
{
    FilePtr(FILE *f) : f(f) {}
    ~FilePtr() { this->close(); }
    void close()
    {
        if (this->f)
        {
            ::fclose(this->f);
            this->f = nullptr;
        }
    }
    FILE *f = nullptr;
};

class ScopeGuard
{
public:
    typedef std::function<void()> Closure;
    Closure closure;

public:
    ScopeGuard() {}
    ScopeGuard(Closure closure) : closure(std::move(closure)) {}
    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard(ScopeGuard &&other) : closure(std::move(other.closure)) {}

    ~ScopeGuard()
    {
        if (closure)
        {
            closure();
        }
    }

    ScopeGuard &operator=(const ScopeGuard &) = delete;
    ScopeGuard &operator=(ScopeGuard &&other)
    {
        closure = std::move(other.closure);
        return *this;
    }

    void reset() { closure = Closure(); }
};

} // namespace Luminary
