///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Lukas Matena @lukasmatena, Tomas Meszaros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "STEP.hpp"
#include "step/OCCTWrapper.hpp"

#include "luminary/model/scene/Model.hpp"
#include "luminary/mesh/core/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/log/trivial.hpp>

#include <string>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#else
#include <step/OCCTWrapper.hpp>
#include <dlfcn.h>
#endif

namespace Luminary
{

#if !defined(_WIN32) && defined(PREFLIGHT_ENABLE_FORMAT_STEP)
extern "C" bool load_step_internal(const char *path, OCCTResult *res,
                                   std::optional<std::pair<double, double>> deflections /*= std::nullopt*/);
#endif

// Inside deflections pair:
// * first value is linear deflection
// * second value is angle deflection
LoadStepFn get_load_step_fn()
{
    static LoadStepFn load_step_fn = nullptr;

#ifdef _WIN32
    constexpr const char *fn_name = "load_step_internal";
#endif

    if (!load_step_fn)
    {
        auto libpath = boost::dll::program_location().parent_path();
#ifdef _WIN32
        libpath /= "OCCTWrapper.dll";
        HMODULE module = LoadLibraryW(libpath.wstring().c_str());
        if (module == NULL)
            throw Luminary::RuntimeError("Cannot load OCCTWrapper.dll");

        try
        {
            FARPROC farproc = GetProcAddress(module, fn_name);
            if (!farproc)
            {
                DWORD ec = GetLastError();
                throw Luminary::RuntimeError(std::string("Cannot load function from OCCTWrapper.dll: ") + fn_name +
                                             "\n\nError code: " + std::to_string(ec));
            }
            load_step_fn = reinterpret_cast<LoadStepFn>(farproc);
        }
        catch (const Luminary::RuntimeError &)
        {
            FreeLibrary(module);
            throw;
        }
#elif defined(PREFLIGHT_ENABLE_FORMAT_STEP)
        load_step_fn = &load_step_internal;
#endif
        // A non-Windows build without STEP support leaves the pointer null and load_step() reports
        // a failed import.
    }

    return load_step_fn;
}

bool load_step(const char *path, Model *model /*BBS:, ImportStepProgressFn proFn*/,
               std::optional<std::pair<double, double>> deflections, std::string *warning_out)
{
    OCCTResult occt_object;

    // The loader lives in a separate library; a failure to load it or a throw from the
    // conversion is reported as a warning and a failed import, not a crash.
    try
    {
        LoadStepFn load_step_fn = get_load_step_fn();
        if (!load_step_fn)
            return false;
        load_step_fn(path, &occt_object, deflections);
    }
    catch (const std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << "STEP import failed: " << e.what();
        if (warning_out)
            *warning_out = e.what();
        return false;
    }

    if (!occt_object.warning_str.empty())
    {
        BOOST_LOG_TRIVIAL(warning) << "STEP import warning: " << occt_object.warning_str;
        if (warning_out)
            *warning_out = occt_object.warning_str;
    }

    if (occt_object.volumes.empty())
        return false;

    assert(boost::algorithm::iends_with(occt_object.object_name, ".stp") ||
           boost::algorithm::iends_with(occt_object.object_name, ".step"));
    occt_object.object_name.erase(occt_object.object_name.find("."));
    assert(!occt_object.object_name.empty());

    ModelObject *new_object = model->add_object();
    new_object->input_file = path;
    if (new_object->volumes.size() == 1 && !occt_object.volumes.front().volume_name.empty())
        new_object->name = new_object->volumes.front()->name;
    else
        new_object->name = occt_object.object_name;

    for (size_t i = 0; i < occt_object.volumes.size(); ++i)
    {
        TriangleMesh triangle_mesh;
        triangle_mesh.from_facets(std::move(occt_object.volumes[i].facets));
        ModelVolume *new_volume = new_object->add_volume(std::move(triangle_mesh));

        new_volume->name = occt_object.volumes[i].volume_name.empty() ? std::string("Part") + std::to_string(i + 1)
                                                                      : occt_object.volumes[i].volume_name;
        new_volume->source.input_file = path;
        new_volume->source.object_idx = (int) model->objects.size() - 1;
        new_volume->source.volume_idx = (int) new_object->volumes.size() - 1;
    }

    return true;
}

}; // namespace Luminary
