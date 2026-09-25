///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 - 2025 Pavel Surynek
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
/*================================================================*/
// Object preprocessing for the sequential printing SMT model.
/*================================================================*/

#pragma once

/*----------------------------------------------------------------*/

#include "seq_sequential.hpp"

/*----------------------------------------------------------------*/

namespace Sequential
{

/*----------------------------------------------------------------*/

const coord_t SEQ_SLICER_SCALE_FACTOR = 100000;
const double SEQ_POLYGON_DECIMATION_GROW_FACTOR = 1.005;

/*----------------------------------------------------------------*/

struct ObjectToPrint;

/*----------------------------------------------------------------*/

extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S;

extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_MK3S;
extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S;
extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S;

extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK4;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK4;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK4;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK4;

extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_MK4;
extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK4;
extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK4;

extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_XL;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_XL;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_XL;
extern const std::vector<Luminary::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_XL;

extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_XL;
extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_XL;
extern const std::vector<std::vector<Luminary::Polygon>> SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_XL;

/*----------------------------------------------------------------*/

Rational scaleDown_CoordinateForSequentialSolver(coord_t x);

void scaleDown_PolygonForSequentialSolver(const Luminary::Polygon &polygon, Luminary::Polygon &scaled_polygon);

void scaleDown_PolygonForSequentialSolver(coord_t scale_factor, const Luminary::Polygon &polygon,
                                          Luminary::Polygon &scaled_polygon);

Luminary::Polygon scaleDown_PolygonForSequentialSolver(coord_t scale_factor, const Luminary::Polygon &polygon);

void scaleUp_PositionForSlicer(const Rational &position_X, const Rational &position_Y, coord_t &scaled_position_X,
                               coord_t &scaled_position_Y);

void scaleUp_PositionForSlicer(coord_t scale_factor, const Rational &position_X, const Rational &position_Y,
                               coord_t &scaled_position_X, coord_t &scaled_position_Y);

void scaleUp_PositionForSlicer(double position_X, double position_Y, coord_t &scaled_position_X,
                               coord_t &scaled_position_Y);

void scaleUp_PositionForSlicer(coord_t scale_factor, double position_X, double position_Y, coord_t &scaled_position_X,
                               coord_t &scaled_position_Y);

Luminary::Polygon scaleUp_PolygonForSlicer(const Luminary::Polygon &polygon);
Luminary::Polygon scaleUp_PolygonForSlicer(coord_t scale_factor, const Luminary::Polygon &polygon);

Luminary::Polygon scaleUp_PolygonForSlicer(const Luminary::Polygon &polygon, double x_pos, double y_pos);
Luminary::Polygon scaleUp_PolygonForSlicer(coord_t scale_factor, const Luminary::Polygon &polygon, double x_pos,
                                           double y_pos);

Luminary::Polygon truncate_PolygonAsSeenBySequentialSolver(coord_t scale_factor, const Luminary::Polygon &polygon);

void ground_PolygonByBoundingBox(Luminary::Polygon &polygon);
void ground_PolygonByFirstPoint(Luminary::Polygon &polygon);

void shift_Polygon(Luminary::Polygon &polygon, coord_t x_offset, coord_t y_offset);
void shift_Polygon(Luminary::Polygon &polygon, const Luminary::Point &offset);

/*----------------------------------------------------------------*/

Luminary::Polygon transform_UpsideDown(const SolverConfiguration &solver_configuration,
                                       const Luminary::Polygon &polygon);
Luminary::Polygon transform_UpsideDown(const SolverConfiguration &solver_configuration, coord_t scale_factor,
                                       const Luminary::Polygon &polygon);

void transform_UpsideDown(const SolverConfiguration &solver_configuration, const coord_t &scaled_x_pos,
                          const coord_t &scaled_y_pos, coord_t &transformed_x_pos, coord_t &transformed_y_pos);

void transform_UpsideDown(const SolverConfiguration &solver_configuration, coord_t scale_factor,
                          const coord_t &scaled_x_pos, const coord_t &scaled_y_pos, coord_t &transformed_x_pos,
                          coord_t &transformed_y_pos);

/*----------------------------------------------------------------*/

void grow_PolygonForContainedness(coord_t center_x, coord_t center_y, Luminary::Polygon &polygon);

void decimate_PolygonForSequentialSolver(const SolverConfiguration &solver_configuration,
                                         const Luminary::Polygon &polygon, Luminary::Polygon &scale_down_polygon,
                                         bool extra_safety);

void decimate_PolygonForSequentialSolver(double DP_tolerance, const Luminary::Polygon &polygon,
                                         Luminary::Polygon &decimated_polygon, bool extra_safety);

void extend_PolygonConvexUnreachableZone(const SolverConfiguration &solver_configuration,
                                         const Luminary::Polygon &polygon,
                                         const std::vector<Luminary::Polygon> &extruder_polygons,
                                         std::vector<Luminary::Polygon> &unreachable_polygons);

void extend_PolygonBoxUnreachableZone(const SolverConfiguration &solver_configuration, const Luminary::Polygon &polygon,
                                      const std::vector<Luminary::Polygon> &extruder_polygons,
                                      std::vector<Luminary::Polygon> &unreachable_polygons);

void extend_PolygonBoxUnreachableZone(const SolverConfiguration &solver_configuration, const Luminary::Polygon &polygon,
                                      const std::vector<Luminary::Polygon> &extruder_polygons,
                                      std::vector<Luminary::Polygon> &unreachable_polygons);

void prepare_ExtruderPolygons(const SolverConfiguration &solver_configuration, const PrinterGeometry &printer_geometry,
                              const ObjectToPrint &object_to_print,
                              std::vector<Luminary::Polygon> &convex_level_polygons,
                              std::vector<Luminary::Polygon> &box_level_polygons,
                              std::vector<std::vector<Luminary::Polygon>> &extruder_convex_level_polygons,
                              std::vector<std::vector<Luminary::Polygon>> &extruder_box_level_polygons,
                              bool extra_safety);

void prepare_ObjectPolygons(const SolverConfiguration &solver_configuration,
                            const std::vector<Luminary::Polygon> &convex_level_polygons,
                            const std::vector<Luminary::Polygon> &box_level_polygons,
                            const std::vector<std::vector<Luminary::Polygon>> &extruder_convex_level_polygons,
                            const std::vector<std::vector<Luminary::Polygon>> &extruder_box_level_polygons,
                            Luminary::Polygon &object_polygon, std::vector<Luminary::Polygon> &unreachable_polygons);

void prepare_UnreachableZonePolygons(const SolverConfiguration &solver_configuration, const Luminary::Polygon &polygon,
                                     const std::vector<std::vector<Luminary::Polygon>> &extruder_convex_level_polygons,
                                     const std::vector<std::vector<Luminary::Polygon>> &extruder_box_level_polygons,
                                     std::vector<Luminary::Polygon> &unreachable_polygons);

void prepare_UnreachableZonePolygons(const SolverConfiguration &solver_configuration,
                                     const std::vector<Luminary::Polygon> &convex_level_polygons,
                                     const std::vector<Luminary::Polygon> &box_level_polygons,
                                     const std::vector<std::vector<Luminary::Polygon>> &extruder_convex_level_polygons,
                                     const std::vector<std::vector<Luminary::Polygon>> &extruder_box_level_polygons,
                                     std::vector<Luminary::Polygon> &unreachable_polygons);

bool check_PolygonSizeFitToPlate(const SolverConfiguration &solver_configuration, const Luminary::Polygon &polygon);
bool check_PolygonPositionWithinPlate(const SolverConfiguration &solver_configuration, coord_t x, coord_t y,
                                      const Luminary::Polygon &polygon);

bool check_PolygonSizeFitToPlate(const SolverConfiguration &solver_configuration, coord_t scale_factor,
                                 const Luminary::Polygon &polygon);
bool check_PolygonPositionWithinPlate(const SolverConfiguration &solver_configuration, coord_t scale_factor, coord_t x,
                                      coord_t y, const Luminary::Polygon &polygon);

/*----------------------------------------------------------------*/

bool check_PolygonConsumation(const std::vector<Luminary::Polygon> &polygons,
                              const std::vector<Luminary::Polygon> &consumer_polygons);
std::vector<std::vector<Luminary::Polygon>> simplify_UnreachableZonePolygons(
    const std::vector<std::vector<Luminary::Polygon>> &unreachable_polygons);

void glue_LowObjects(std::vector<SolvableObject> &solvable_ojects);

/*----------------------------------------------------------------*/

double calc_PolygonArea(const Luminary::Polygon &polygon);

double calc_PolygonUnreachableZoneArea(const std::vector<Luminary::Polygon> &unreachable_polygons);
double calc_PolygonUnreachableZoneArea(const Luminary::Polygon &polygon,
                                       const std::vector<Luminary::Polygon> &unreachable_polygons);

double calc_PolygonArea(const std::vector<Luminary::Polygon> &polygons);
double calc_PolygonArea(const std::vector<int> &fixed, const std::vector<int> &undecided,
                        const std::vector<Luminary::Polygon> &polygons);

double calc_PolygonUnreachableZoneArea(const std::vector<Luminary::Polygon> &polygons,
                                       const std::vector<std::vector<Luminary::Polygon>> &unreachable_polygons);

/*----------------------------------------------------------------*/

} // namespace Sequential

/*----------------------------------------------------------------*/
