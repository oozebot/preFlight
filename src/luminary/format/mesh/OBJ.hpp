///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2019 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2012 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

namespace Luminary
{

class TriangleMesh;
class Model;
class ModelObject;

// Load an OBJ file into a provided model.
extern bool load_obj(const char *path, TriangleMesh *mesh);
extern bool load_obj(const char *path, Model *model, const char *object_name = nullptr);

extern bool store_obj(const char *path, TriangleMesh *mesh);
extern bool store_obj(const char *path, ModelObject *model);
extern bool store_obj(const char *path, Model *model);

}; // namespace Luminary
