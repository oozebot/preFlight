///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "DebugGeom.hpp"

#include <cstdio>

namespace Luminary
{

static void append_point(std::string &s, const Point &p)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%.4f %.4f", unscaled<double>(p.x()), unscaled<double>(p.y()));
    if (n > 0)
        s.append(buf, size_t(n));
}

// Closed ring: the first point repeated at the end, as WKT requires.
static void append_ring(std::string &s, const Points &pts)
{
    s += '(';
    for (size_t i = 0; i < pts.size(); ++i)
    {
        if (i)
            s += ',';
        append_point(s, pts[i]);
    }
    if (!pts.empty())
    {
        s += ',';
        append_point(s, pts.front());
    }
    s += ')';
}

std::string dbg_wkt(const Polygon &poly)
{
    std::string s = "POLYGON(";
    append_ring(s, poly.points);
    s += ')';
    return s;
}

std::string dbg_wkt(const Polygons &polys)
{
    if (polys.empty())
        return "MULTIPOLYGON EMPTY";
    std::string s = "MULTIPOLYGON(";
    for (size_t i = 0; i < polys.size(); ++i)
    {
        s += i ? ",(" : "(";
        append_ring(s, polys[i].points);
        s += ')';
    }
    s += ')';
    return s;
}

std::string dbg_wkt(const ExPolygon &ep)
{
    std::string s = "POLYGON(";
    append_ring(s, ep.contour.points);
    for (const Polygon &h : ep.holes)
    {
        s += ',';
        append_ring(s, h.points);
    }
    s += ')';
    return s;
}

std::string dbg_wkt(const ExPolygons &eps)
{
    if (eps.empty())
        return "MULTIPOLYGON EMPTY";
    std::string s = "MULTIPOLYGON(";
    for (size_t i = 0; i < eps.size(); ++i)
    {
        s += i ? ",(" : "(";
        append_ring(s, eps[i].contour.points);
        for (const Polygon &h : eps[i].holes)
        {
            s += ',';
            append_ring(s, h.points);
        }
        s += ')';
    }
    s += ')';
    return s;
}

static void append_open(std::string &s, const Points &pts)
{
    s += '(';
    for (size_t i = 0; i < pts.size(); ++i)
    {
        if (i)
            s += ',';
        append_point(s, pts[i]);
    }
    s += ')';
}

std::string dbg_wkt(const Polyline &pl)
{
    if (pl.points.size() < 2)
        return "LINESTRING EMPTY";
    std::string s = "LINESTRING";
    append_open(s, pl.points);
    return s;
}

std::string dbg_wkt(const Polylines &pls)
{
    std::string s = "MULTILINESTRING(";
    size_t n = 0;
    for (const Polyline &pl : pls)
    {
        if (pl.points.size() < 2)
            continue;
        if (n++)
            s += ',';
        append_open(s, pl.points);
    }
    if (n == 0)
        return "MULTILINESTRING EMPTY";
    s += ')';
    return s;
}

void dbg_geom(uint32_t cat, double z, const char *tag, const Polygons &polys, const std::string &attrs)
{
    if (dbg_geom_active(cat, z))
        dbg_geom_line(cat, z, tag, attrs, dbg_wkt(polys));
}

void dbg_geom(uint32_t cat, double z, const char *tag, const ExPolygons &eps, const std::string &attrs)
{
    if (dbg_geom_active(cat, z))
        dbg_geom_line(cat, z, tag, attrs, dbg_wkt(eps));
}

void dbg_geom(uint32_t cat, double z, const char *tag, const Polyline &pl, const std::string &attrs)
{
    if (dbg_geom_active(cat, z))
        dbg_geom_line(cat, z, tag, attrs, dbg_wkt(pl));
}

void dbg_geom(uint32_t cat, double z, const char *tag, const Polylines &pls, const std::string &attrs)
{
    if (dbg_geom_active(cat, z))
        dbg_geom_line(cat, z, tag, attrs, dbg_wkt(pls));
}

} // namespace Luminary
