///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef ROTOPTIMIZEJOB_HPP
#define ROTOPTIMIZEJOB_HPP

#include "Job.hpp"

namespace Slic3r
{
namespace GUI
{

class Plater;

class RotoptimizeJob : public Job
{
public:
    void prepare() {}
    void process(Ctl &ctl) override {}
    RotoptimizeJob() {}
    void finalize(bool canceled, std::exception_ptr &) override {}
    static constexpr size_t get_methods_count() { return 0; }
    static std::string get_method_name(size_t) { return ""; }
    static std::string get_method_description(size_t) { return ""; }
};

} // namespace GUI
} // namespace Slic3r

#endif // ROTOPTIMIZEJOB_HPP
