#ifndef CLIPPER_BACKEND_HPP
#define CLIPPER_BACKEND_HPP

#include <sstream>
#include <unordered_map>
#include <cassert>
#include <vector>
#include <iostream>

#include <libnest2d/geometry_traits.hpp>
#include <libnest2d/geometry_traits_nfp.hpp>

#include <luminary/geometry/contours/ExPolygon.hpp>
#include <luminary/geometry/clipper/ClipperUtils.hpp>

namespace Luminary {

template<class T, class En = void> struct IsVec_ : public std::false_type {};

template<class T> struct IsVec_< Vec<2, T> >: public std::true_type {};

template<class T>
static constexpr const bool IsVec = IsVec_<libnest2d::remove_cvref_t<T>>::value;

template<class T, class O> using VecOnly = std::enable_if_t<IsVec<T>, O>;

inline Point operator+(const Point& p1, const Point& p2) {
    Point ret = p1;
    ret += p2;
    return ret;
}

inline Point operator -(const Point& p ) {
    Point ret = p;
    ret.x() = -ret.x();
    ret.y() = -ret.y();
    return ret;
}

inline Point operator-(const Point& p1, const Point& p2) {
    Point ret = p1;
    ret -= p2;
    return ret;
}

inline Point& operator *=(Point& p, const Point& pa ) {
    p.x() *= pa.x();
    p.y() *= pa.y();
    return p;
}

inline Point operator*(const Point& p1, const Point& p2) {
    Point ret = p1;
    ret *= p2;
    return ret;
}

} // namespace Luminary

namespace libnest2d {

template<class T> using Vec = Luminary::Vec<2, T>;

// Aliases for convinience
using PointImpl = Luminary::Point;
using PathImpl  = Luminary::Polygon;
using HoleStore = Luminary::Polygons;
using PolygonImpl = Luminary::ExPolygon;

template<> struct ShapeTag<Luminary::Vec2crd> { using Type = PointTag; };
template<> struct ShapeTag<Luminary::Point>   { using Type = PointTag; };

template<> struct ShapeTag<std::vector<Luminary::Vec2crd>> { using Type = PathTag; };
template<> struct ShapeTag<Luminary::Polygon> { using Type = PathTag; };
template<> struct ShapeTag<Luminary::Points>  { using Type = PathTag; };
template<> struct ShapeTag<Luminary::ExPolygon> { using Type = PolygonTag; };
template<> struct ShapeTag<Luminary::ExPolygons> { using Type = MultiPolygonTag; };

// Type of coordinate units used by Clipper. Enough to specialize for point,
// the rest of the types will work (Path, Polygon)
template<> struct CoordType<Luminary::Point> {
    using Type = coord_t;
    static const constexpr coord_t MM_IN_COORDS = 1000000;
};

template<> struct CoordType<Luminary::Vec2crd> {
    using Type = coord_t;
    static const constexpr coord_t MM_IN_COORDS = 1000000;
};

// Enough to specialize for path, it will work for multishape and Polygon
template<> struct PointType<std::vector<Luminary::Vec2crd>> { using Type = Luminary::Vec2crd; };
template<> struct PointType<Luminary::Polygon> { using Type = Luminary::Point; };
template<> struct PointType<Luminary::Points> { using Type = Luminary::Point; };

// This is crucial. CountourType refers to itself by default, so we don't have
// to secialize for clipper Path. ContourType<PathImpl>::Type is PathImpl.
template<> struct ContourType<Luminary::ExPolygon> { using Type = Luminary::Polygon; };

// The holes are contained in Clipper::Paths
template<> struct HolesContainer<Luminary::ExPolygon> { using Type = Luminary::Polygons; };

template<>
struct OrientationType<Luminary::Polygon> {
    static const constexpr Orientation Value = Orientation::COUNTER_CLOCKWISE;
};

template<>
struct OrientationType<Luminary::Points> {
    static const constexpr Orientation Value = Orientation::COUNTER_CLOCKWISE;
};

template<>
struct ClosureType<Luminary::Polygon> {
    static const constexpr Closure Value = Closure::OPEN;
};

template<>
struct ClosureType<Luminary::Points> {
    static const constexpr Closure Value = Closure::OPEN;
};

template<> struct MultiShape<Luminary::ExPolygon> { using Type = Luminary::ExPolygons; };
template<> struct ContourType<Luminary::ExPolygons> { using Type = Luminary::Polygon; };

// Using the libnest2d default area implementation
#define DISABLE_BOOST_AREA

namespace shapelike {

template<>
inline void offset(Luminary::ExPolygon& sh, coord_t distance, const PolygonTag&)
{
#define DISABLE_BOOST_OFFSET
    auto res = Luminary::offset_ex(sh, distance, Luminary::JoinType::Square);
    if (!res.empty()) sh = res.front();
}

template<>
inline void offset(Luminary::Polygon& sh, coord_t distance, const PathTag&)
{
    auto res = Luminary::offset(sh, distance, Luminary::JoinType::Square);
    if (!res.empty()) sh = res.front();
}

// Tell libnest2d how to make string out of a ClipperPolygon object
template<> inline std::string toString(const Luminary::ExPolygon& sh)
{
    std::stringstream ss;

    ss << "Contour {\n";
    for(auto &p : sh.contour.points) {
        ss << "\t" << p.x() << " " << p.y() << "\n";
    }
    ss << "}\n";

    for(auto& h : sh.holes) {
        ss << "Holes {\n";
        for(auto p : h.points)  {
            ss << "\t{\n";
            ss << "\t\t" << p.x() << " " << p.y() << "\n";
            ss << "\t}\n";
        }
        ss << "}\n";
    }

    return ss.str();
}

template<>
inline Luminary::ExPolygon create(const Luminary::Polygon& path, const Luminary::Polygons& holes)
{
    Luminary::ExPolygon p;
    p.contour = path;
    p.holes = holes;

    return p;
}

template<> inline Luminary::ExPolygon create(Luminary::Polygon&& path, Luminary::Polygons&& holes) {
    Luminary::ExPolygon p;
    p.contour.points.swap(path.points);
    p.holes.swap(holes);

    return p;
}

template<>
inline const THolesContainer<PolygonImpl>& holes(const Luminary::ExPolygon& sh)
{
    return sh.holes;
}

template<> inline THolesContainer<PolygonImpl>& holes(Luminary::ExPolygon& sh)
{
    return sh.holes;
}

template<>
inline Luminary::Polygon& hole(Luminary::ExPolygon& sh, unsigned long idx)
{
    return sh.holes[idx];
}

template<>
inline const Luminary::Polygon& hole(const Luminary::ExPolygon& sh, unsigned long idx)
{
    return sh.holes[idx];
}

template<> inline size_t holeCount(const Luminary::ExPolygon& sh)
{
    return sh.holes.size();
}

template<> inline Luminary::Polygon& contour(Luminary::ExPolygon& sh)
{
    return sh.contour;
}

template<>
inline const Luminary::Polygon& contour(const Luminary::ExPolygon& sh)
{
    return sh.contour;
}

template<>
inline void reserve(Luminary::Polygon& p, size_t vertex_capacity, const PathTag&)
{
    p.points.reserve(vertex_capacity);
}

template<>
inline void addVertex(Luminary::Polygon& sh, const PathTag&, const Luminary::Point &p)
{
    sh.points.emplace_back(p);
}

#define DISABLE_BOOST_TRANSLATE
template<>
inline void translate(Luminary::ExPolygon& sh, const Luminary::Point& offs)
{
    sh.translate(offs);
}

template<>
inline void translate(Luminary::Polygon& sh, const Luminary::Point& offs)
{
    sh.translate(offs);
}

#define DISABLE_BOOST_ROTATE
template<>
inline void rotate(Luminary::ExPolygon& sh, const Radians& rads)
{
    sh.rotate(rads);
}

template<>
inline void rotate(Luminary::Polygon& sh, const Radians& rads)
{
    sh.rotate(rads);
}

} // namespace shapelike

namespace nfp {

#define DISABLE_BOOST_NFP_MERGE
template<>
inline TMultiShape<PolygonImpl> merge(const TMultiShape<PolygonImpl>& shapes)
{
    return Luminary::union_ex(shapes);
}

} // namespace nfp
} // namespace libnest2d

#define DISABLE_BOOST_CONVEX_HULL

//#define DISABLE_BOOST_SERIALIZE
//#define DISABLE_BOOST_UNSERIALIZE

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif
// All other operators and algorithms are implemented with boost
#include <libnest2d/utils/boost_alg.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // CLIPPER_BACKEND_HPP
