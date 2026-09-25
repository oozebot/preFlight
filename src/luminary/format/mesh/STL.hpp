///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2019 Vojtěch Bubník @bubnikv
///|/
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

namespace Luminary
{

class TriangleMesh;
class ModelObject;
class Model;

// Load an STL file into a provided model.
extern bool load_stl(const char *path, Model *model, const char *object_name = nullptr);

extern bool store_stl(const char *path, TriangleMesh *mesh, bool binary);
extern bool store_stl(const char *path, ModelObject *model_object, bool binary);
extern bool store_stl(const char *path, Model *model, bool binary);

}; // namespace Luminary
