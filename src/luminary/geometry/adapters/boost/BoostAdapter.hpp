///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"

#include <boost/geometry.hpp>

namespace boost
{
namespace geometry
{
namespace traits
{

/* ************************************************************************** */
/* Point concept adaptation ************************************************* */
/* ************************************************************************** */

template<>
struct tag<Luminary::Point>
{
    using type = point_tag;
};

template<>
struct coordinate_type<Luminary::Point>
{
    using type = coord_t;
};

template<>
struct coordinate_system<Luminary::Point>
{
    using type = cs::cartesian;
};

template<>
struct dimension<Luminary::Point> : boost::mpl::int_<2>
{
};

template<std::size_t d>
struct access<Luminary::Point, d>
{
    static inline coord_t get(Luminary::Point const &a) { return a(d); }

    static inline void set(Luminary::Point &a, coord_t const &value) { a(d) = value; }
};

// For Vec<N, T> ///////////////////////////////////////////////////////////////

template<int N, class T>
struct tag<Luminary::Vec<N, T>>
{
    using type = point_tag;
};

template<int N, class T>
struct coordinate_type<Luminary::Vec<N, T>>
{
    using type = T;
};

template<int N, class T>
struct coordinate_system<Luminary::Vec<N, T>>
{
    using type = cs::cartesian;
};

template<int N, class T>
struct dimension<Luminary::Vec<N, T>> : boost::mpl::int_<N>
{
};

template<int N, class T, std::size_t d>
struct access<Luminary::Vec<N, T>, d>
{
    static inline T get(Luminary::Vec<N, T> const &a) { return a(d); }

    static inline void set(Luminary::Vec<N, T> &a, T const &value) { a(d) = value; }
};

/* ************************************************************************** */
/* Box concept adaptation *************************************************** */
/* ************************************************************************** */

template<>
struct tag<Luminary::BoundingBox>
{
    using type = box_tag;
};

template<>
struct point_type<Luminary::BoundingBox>
{
    using type = Luminary::Point;
};

template<std::size_t d>
struct indexed_access<Luminary::BoundingBox, 0, d>
{
    static inline coord_t get(Luminary::BoundingBox const &box) { return box.min(d); }
    static inline void set(Luminary::BoundingBox &box, coord_t const &coord) { box.min(d) = coord; }
};

template<std::size_t d>
struct indexed_access<Luminary::BoundingBox, 1, d>
{
    static inline coord_t get(Luminary::BoundingBox const &box) { return box.max(d); }
    static inline void set(Luminary::BoundingBox &box, coord_t const &coord) { box.max(d) = coord; }
};

template<class T>
using BB3 = Luminary::BoundingBox3Base<Luminary::Vec<3, T>>;

template<class T>
struct tag<BB3<T>>
{
    using type = box_tag;
};

template<class T>
struct point_type<BB3<T>>
{
    using type = Luminary::Vec<3, T>;
};

template<class T, std::size_t d>
struct indexed_access<BB3<T>, 0, d>
{
    static inline coord_t get(BB3<T> const &box) { return box.min(d); }
    static inline void set(BB3<T> &box, coord_t const &coord) { box.min(d) = coord; }
};

template<class T, std::size_t d>
struct indexed_access<BB3<T>, 1, d>
{
    static inline coord_t get(BB3<T> const &box) { return box.max(d); }
    static inline void set(BB3<T> &box, coord_t const &coord) { box.max(d) = coord; }
};

/* ************************************************************************** */
/* Segment concept adaptaion ************************************************ */
/* ************************************************************************** */

template<>
struct tag<Luminary::Line>
{
    using type = segment_tag;
};

template<>
struct point_type<Luminary::Line>
{
    using type = Luminary::Point;
};

template<>
struct indexed_access<Luminary::Line, 0, 0>
{
    static inline coord_t get(Luminary::Line const &l) { return l.a.x(); }
    static inline void set(Luminary::Line &l, coord_t c) { l.a.x() = c; }
};

template<>
struct indexed_access<Luminary::Line, 0, 1>
{
    static inline coord_t get(Luminary::Line const &l) { return l.a.y(); }
    static inline void set(Luminary::Line &l, coord_t c) { l.a.y() = c; }
};

template<>
struct indexed_access<Luminary::Line, 1, 0>
{
    static inline coord_t get(Luminary::Line const &l) { return l.b.x(); }
    static inline void set(Luminary::Line &l, coord_t c) { l.b.x() = c; }
};

template<>
struct indexed_access<Luminary::Line, 1, 1>
{
    static inline coord_t get(Luminary::Line const &l) { return l.b.y(); }
    static inline void set(Luminary::Line &l, coord_t c) { l.b.y() = c; }
};

/* ************************************************************************** */
/* Polyline concept adaptation ********************************************** */
/* ************************************************************************** */

template<>
struct tag<Luminary::Polyline>
{
    using type = linestring_tag;
};

/* ************************************************************************** */
/* Polygon concept adaptation *********************************************** */
/* ************************************************************************** */

// Ring implementation /////////////////////////////////////////////////////////

// Boost would refer to ClipperLib::Path (alias Luminary::ExPolygon) as a ring
template<>
struct tag<Luminary::Polygon>
{
    using type = ring_tag;
};

template<>
struct point_order<Luminary::Polygon>
{
    static const order_selector value = counterclockwise;
};

// All our Paths should be closed for the bin packing application
template<>
struct closure<Luminary::Polygon>
{
    static const constexpr closure_selector value = closure_selector::open;
};

// Polygon implementation //////////////////////////////////////////////////////

template<>
struct tag<Luminary::ExPolygon>
{
    using type = polygon_tag;
};

template<>
struct exterior_ring<Luminary::ExPolygon>
{
    static inline Luminary::Polygon &get(Luminary::ExPolygon &p) { return p.contour; }
    static inline Luminary::Polygon const &get(Luminary::ExPolygon const &p) { return p.contour; }
};

template<>
struct ring_const_type<Luminary::ExPolygon>
{
    using type = const Luminary::Polygon &;
};

template<>
struct ring_mutable_type<Luminary::ExPolygon>
{
    using type = Luminary::Polygon &;
};

template<>
struct interior_const_type<Luminary::ExPolygon>
{
    using type = const Luminary::Polygons &;
};

template<>
struct interior_mutable_type<Luminary::ExPolygon>
{
    using type = Luminary::Polygons &;
};

template<>
struct interior_rings<Luminary::ExPolygon>
{
    static inline Luminary::Polygons &get(Luminary::ExPolygon &p) { return p.holes; }

    static inline const Luminary::Polygons &get(Luminary::ExPolygon const &p) { return p.holes; }
};

/* ************************************************************************** */
/* MultiPolygon concept adaptation ****************************************** */
/* ************************************************************************** */

template<>
struct tag<Luminary::ExPolygons>
{
    using type = multi_polygon_tag;
};

} // namespace traits
} // namespace geometry

template<>
struct range_value<std::vector<Luminary::Vec2d>>
{
    using type = Luminary::Vec2d;
};

template<>
struct range_value<Luminary::Polyline>
{
    using type = Luminary::Point;
};

// This is an addition to the ring implementation of Polygon concept
template<>
struct range_value<Luminary::Polygon>
{
    using type = Luminary::Point;
};

template<>
struct range_value<Luminary::Polygons>
{
    using type = Luminary::Polygon;
};

template<>
struct range_value<Luminary::ExPolygons>
{
    using type = Luminary::ExPolygon;
};

} // namespace boost
