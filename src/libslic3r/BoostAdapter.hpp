///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef SLA_BOOSTADAPTER_HPP
#define SLA_BOOSTADAPTER_HPP

#include <libslic3r/Point.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/Polyline.hpp>

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
struct tag<Slic3r::Point>
{
    using type = point_tag;
};

template<>
struct coordinate_type<Slic3r::Point>
{
    using type = coord_t;
};

template<>
struct coordinate_system<Slic3r::Point>
{
    using type = cs::cartesian;
};

template<>
struct dimension<Slic3r::Point> : boost::mpl::int_<2>
{
};

template<std::size_t d>
struct access<Slic3r::Point, d>
{
    static inline coord_t get(Slic3r::Point const &a) { return a(d); }

    static inline void set(Slic3r::Point &a, coord_t const &value) { a(d) = value; }
};

// For Vec<N, T> ///////////////////////////////////////////////////////////////

template<int N, class T>
struct tag<Slic3r::Vec<N, T>>
{
    using type = point_tag;
};

template<int N, class T>
struct coordinate_type<Slic3r::Vec<N, T>>
{
    using type = T;
};

template<int N, class T>
struct coordinate_system<Slic3r::Vec<N, T>>
{
    using type = cs::cartesian;
};

template<int N, class T>
struct dimension<Slic3r::Vec<N, T>> : boost::mpl::int_<N>
{
};

template<int N, class T, std::size_t d>
struct access<Slic3r::Vec<N, T>, d>
{
    static inline T get(Slic3r::Vec<N, T> const &a) { return a(d); }

    static inline void set(Slic3r::Vec<N, T> &a, T const &value) { a(d) = value; }
};

/* ************************************************************************** */
/* Box concept adaptation *************************************************** */
/* ************************************************************************** */

template<>
struct tag<Slic3r::BoundingBox>
{
    using type = box_tag;
};

template<>
struct point_type<Slic3r::BoundingBox>
{
    using type = Slic3r::Point;
};

template<std::size_t d>
struct indexed_access<Slic3r::BoundingBox, 0, d>
{
    static inline coord_t get(Slic3r::BoundingBox const &box) { return box.min(d); }
    static inline void set(Slic3r::BoundingBox &box, coord_t const &coord) { box.min(d) = coord; }
};

template<std::size_t d>
struct indexed_access<Slic3r::BoundingBox, 1, d>
{
    static inline coord_t get(Slic3r::BoundingBox const &box) { return box.max(d); }
    static inline void set(Slic3r::BoundingBox &box, coord_t const &coord) { box.max(d) = coord; }
};

template<class T>
using BB3 = Slic3r::BoundingBox3Base<Slic3r::Vec<3, T>>;

template<class T>
struct tag<BB3<T>>
{
    using type = box_tag;
};

template<class T>
struct point_type<BB3<T>>
{
    using type = Slic3r::Vec<3, T>;
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
struct tag<Slic3r::Line>
{
    using type = segment_tag;
};

template<>
struct point_type<Slic3r::Line>
{
    using type = Slic3r::Point;
};

template<>
struct indexed_access<Slic3r::Line, 0, 0>
{
    static inline coord_t get(Slic3r::Line const &l) { return l.a.x(); }
    static inline void set(Slic3r::Line &l, coord_t c) { l.a.x() = c; }
};

template<>
struct indexed_access<Slic3r::Line, 0, 1>
{
    static inline coord_t get(Slic3r::Line const &l) { return l.a.y(); }
    static inline void set(Slic3r::Line &l, coord_t c) { l.a.y() = c; }
};

template<>
struct indexed_access<Slic3r::Line, 1, 0>
{
    static inline coord_t get(Slic3r::Line const &l) { return l.b.x(); }
    static inline void set(Slic3r::Line &l, coord_t c) { l.b.x() = c; }
};

template<>
struct indexed_access<Slic3r::Line, 1, 1>
{
    static inline coord_t get(Slic3r::Line const &l) { return l.b.y(); }
    static inline void set(Slic3r::Line &l, coord_t c) { l.b.y() = c; }
};

/* ************************************************************************** */
/* Polyline concept adaptation ********************************************** */
/* ************************************************************************** */

template<>
struct tag<Slic3r::Polyline>
{
    using type = linestring_tag;
};

/* ************************************************************************** */
/* Polygon concept adaptation *********************************************** */
/* ************************************************************************** */

// Ring implementation /////////////////////////////////////////////////////////

// Boost would refer to ClipperLib::Path (alias Slic3r::ExPolygon) as a ring
template<>
struct tag<Slic3r::Polygon>
{
    using type = ring_tag;
};

template<>
struct point_order<Slic3r::Polygon>
{
    static const order_selector value = counterclockwise;
};

// All our Paths should be closed for the bin packing application
template<>
struct closure<Slic3r::Polygon>
{
    static const constexpr closure_selector value = closure_selector::open;
};

// Polygon implementation //////////////////////////////////////////////////////

template<>
struct tag<Slic3r::ExPolygon>
{
    using type = polygon_tag;
};

template<>
struct exterior_ring<Slic3r::ExPolygon>
{
    static inline Slic3r::Polygon &get(Slic3r::ExPolygon &p) { return p.contour; }
    static inline Slic3r::Polygon const &get(Slic3r::ExPolygon const &p) { return p.contour; }
};

template<>
struct ring_const_type<Slic3r::ExPolygon>
{
    using type = const Slic3r::Polygon &;
};

template<>
struct ring_mutable_type<Slic3r::ExPolygon>
{
    using type = Slic3r::Polygon &;
};

template<>
struct interior_const_type<Slic3r::ExPolygon>
{
    using type = const Slic3r::Polygons &;
};

template<>
struct interior_mutable_type<Slic3r::ExPolygon>
{
    using type = Slic3r::Polygons &;
};

template<>
struct interior_rings<Slic3r::ExPolygon>
{
    static inline Slic3r::Polygons &get(Slic3r::ExPolygon &p) { return p.holes; }

    static inline const Slic3r::Polygons &get(Slic3r::ExPolygon const &p) { return p.holes; }
};

/* ************************************************************************** */
/* MultiPolygon concept adaptation ****************************************** */
/* ************************************************************************** */

template<>
struct tag<Slic3r::ExPolygons>
{
    using type = multi_polygon_tag;
};

} // namespace traits
} // namespace geometry

template<>
struct range_value<std::vector<Slic3r::Vec2d>>
{
    using type = Slic3r::Vec2d;
};

template<>
struct range_value<Slic3r::Polyline>
{
    using type = Slic3r::Point;
};

// This is an addition to the ring implementation of Polygon concept
template<>
struct range_value<Slic3r::Polygon>
{
    using type = Slic3r::Point;
};

template<>
struct range_value<Slic3r::Polygons>
{
    using type = Slic3r::Polygon;
};

template<>
struct range_value<Slic3r::ExPolygons>
{
    using type = Slic3r::ExPolygon;
};

} // namespace boost

#endif // SLABOOSTADAPTER_HPP
