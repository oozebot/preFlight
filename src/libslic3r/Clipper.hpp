///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

// preFlight uses Clipper2 exclusively. This file provides Clipper2 integration with
// compatibility wrappers for legacy Clipper1 API where needed (move-only PolyTree, etc)

#ifndef slic3r_clipper_hpp
#define slic3r_clipper_hpp

#include <memory>
#include <vector>
#include <clipper2/clipper.h>

namespace Slic3r
{
namespace ClipperWrapper
{

// Forward declarations
class PolyTreeWrapper;
class PolyNodeWrapper;

// Type aliases to match Clipper1 naming
using cInt = int64_t;
using IntPoint = Clipper2Lib::Point64;
using Path = Clipper2Lib::Path64;
using Paths = Clipper2Lib::Paths64;

// Enum mappings for Clipper1 compatibility
using ClipType = Clipper2Lib::ClipType;
using FillRule = Clipper2Lib::FillRule;
using PolyFillType = Clipper2Lib::FillRule;
using JoinType = Clipper2Lib::JoinType;
using EndType = Clipper2Lib::EndType;

// Clipper1-style PolyType enum (not in Clipper2)
enum PolyType
{
    ptSubject,
    ptClip
};

// Clipper1-style enum constants (map to Clipper2 enum class values)
constexpr auto ctIntersection = Clipper2Lib::ClipType::Intersection;
constexpr auto ctUnion = Clipper2Lib::ClipType::Union;
constexpr auto ctDifference = Clipper2Lib::ClipType::Difference;
constexpr auto ctXor = Clipper2Lib::ClipType::Xor;

constexpr auto pftEvenOdd = Clipper2Lib::FillRule::EvenOdd;
constexpr auto pftNonZero = Clipper2Lib::FillRule::NonZero;
constexpr auto pftPositive = Clipper2Lib::FillRule::Positive;
constexpr auto pftNegative = Clipper2Lib::FillRule::Negative;

constexpr auto jtMiter = Clipper2Lib::JoinType::Miter;
constexpr auto jtRound = Clipper2Lib::JoinType::Round;
constexpr auto jtSquare = Clipper2Lib::JoinType::Square;
constexpr auto jtBevel = Clipper2Lib::JoinType::Bevel;

constexpr auto etClosedPolygon = Clipper2Lib::EndType::Polygon;
constexpr auto etClosedLine = Clipper2Lib::EndType::Joined;
constexpr auto etOpenButt = Clipper2Lib::EndType::Butt;
constexpr auto etOpenSquare = Clipper2Lib::EndType::Square;
constexpr auto etOpenRound = Clipper2Lib::EndType::Round;

// Wrapper for PolyNode/PolyPath64 - provides Clipper1-compatible interface
class PolyNodeWrapper
{
private:
    const Clipper2Lib::PolyPath64 *m_node;         // Non-owning pointer
    mutable std::shared_ptr<Path> m_contour_cache; // Cache for Contour
    mutable std::shared_ptr<std::vector<PolyNodeWrapper>> m_children_cache;

public:
    PolyNodeWrapper(const Clipper2Lib::PolyPath64 *node = nullptr) : m_node(node) {}

    // Clipper1-compatible Contour property
    const Path &Contour() const
    {
        if (!m_contour_cache)
        {
            m_contour_cache = std::make_shared<Path>(m_node ? m_node->Polygon() : Path());
        }
        return *m_contour_cache;
    }

    // Clipper1-compatible IsHole check
    bool IsHole() const { return m_node ? m_node->IsHole() : false; }

    // Clipper1-compatible ChildCount
    size_t ChildCount() const { return m_node ? m_node->Count() : 0; }

    // Clipper1-compatible Childs vector access
    const std::vector<PolyNodeWrapper> &Childs() const
    {
        if (!m_children_cache)
        {
            buildChildrenCache();
        }
        return *m_children_cache;
    }

    // Check if valid
    bool IsValid() const { return m_node != nullptr; }

private:
    void buildChildrenCache() const
    {
        m_children_cache = std::make_shared<std::vector<PolyNodeWrapper>>();
        if (m_node)
        {
            m_children_cache->reserve(m_node->Count());
            for (size_t i = 0; i < m_node->Count(); ++i)
            {
                m_children_cache->emplace_back((*m_node)[i]);
            }
        }
    }
};

// Smart wrapper that holds PolyTree64 internally but provides copyable interface
class PolyTreeWrapper
{
private:
    // Use shared_ptr for automatic memory management and copyability
    std::shared_ptr<Clipper2Lib::PolyTree64> m_tree;

    // Cache for converted children (lazy evaluation)
    mutable std::shared_ptr<std::vector<PolyNodeWrapper>> m_children_cache;

public:
    // Default constructor
    PolyTreeWrapper() : m_tree(std::make_shared<Clipper2Lib::PolyTree64>()) {}

    // Move constructor from Clipper2 PolyTree64
    PolyTreeWrapper(Clipper2Lib::PolyTree64 &&tree) : m_tree(std::make_shared<Clipper2Lib::PolyTree64>(std::move(tree)))
    {
    }

    // Copy constructor (enabled by shared_ptr)
    PolyTreeWrapper(const PolyTreeWrapper &other) = default;

    // Move constructor
    PolyTreeWrapper(PolyTreeWrapper &&other) = default;

    // Assignment operators
    PolyTreeWrapper &operator=(const PolyTreeWrapper &other) = default;
    PolyTreeWrapper &operator=(PolyTreeWrapper &&other) = default;

    // Access to underlying Clipper2 tree (for operations)
    Clipper2Lib::PolyTree64 &get() { return *m_tree; }
    const Clipper2Lib::PolyTree64 &get() const { return *m_tree; }

    // Clipper1-compatible ChildCount
    size_t ChildCount() const { return m_tree->Count(); }

    // Clipper1-compatible Childs vector access (lazy-evaluated)
    const std::vector<PolyNodeWrapper> &Childs() const
    {
        if (!m_children_cache)
        {
            buildChildrenCache();
        }
        return *m_children_cache;
    }

    // Clear the tree
    void Clear()
    {
        m_tree->Clear();
        m_children_cache.reset();
    }

    // Check if empty
    bool IsEmpty() const { return m_tree->Count() == 0; }

private:
    void buildChildrenCache() const
    {
        m_children_cache = std::make_shared<std::vector<PolyNodeWrapper>>();
        m_children_cache->reserve(m_tree->Count());
        for (size_t i = 0; i < m_tree->Count(); ++i)
        {
            m_children_cache->emplace_back((*m_tree)[i]);
        }
    }
};

// Main Clipper wrapper class that provides Clipper1 API using Clipper2
class Clipper
{
private:
    Clipper2Lib::Clipper64 m_clipper;

public:
    Clipper() = default;

    // Clipper1-compatible AddPaths
    void AddPaths(const Paths &paths, PolyType pt, bool closed)
    {
        if (pt == ptSubject)
        {
            if (closed)
                m_clipper.AddSubject(paths);
            else
                m_clipper.AddOpenSubject(paths);
        }
        else
        {
            // Clip paths are always closed in Clipper2
            m_clipper.AddClip(paths);
        }
    }

    // Single path variant
    void AddPath(const Path &path, PolyType pt, bool closed)
    {
        if (pt == ptSubject)
        {
            if (closed)
                m_clipper.AddSubject(Paths{path});
            else
                m_clipper.AddOpenSubject(Paths{path});
        }
        else
        {
            m_clipper.AddClip(Paths{path});
        }
    }

    // Clipper1-compatible Execute returning PolyTreeWrapper
    bool Execute(ClipType clipType, PolyTreeWrapper &polytree, PolyFillType subjFillType = pftEvenOdd,
                 PolyFillType clipFillType = pftEvenOdd)
    {
        // Clipper2 uses single FillRule - use subject fill type
        return m_clipper.Execute(clipType, subjFillType, polytree.get());
    }

    // Clipper1-compatible Execute returning Paths
    bool Execute(ClipType clipType, Paths &solution, PolyFillType subjFillType = pftEvenOdd,
                 PolyFillType clipFillType = pftEvenOdd)
    {
        return m_clipper.Execute(clipType, subjFillType, solution);
    }

    // Clear for reuse
    void Clear() { m_clipper.Clear(); }

    // Clipper1 compatibility methods
    bool StrictlySimple() const { return true; } // Clipper2 always produces strictly simple output
    void StrictlySimple(bool value) { /* No-op - always true in Clipper2 */ }
};

// ClipperOffset wrapper with Clipper1-compatible interface
class ClipperOffset
{
private:
    Clipper2Lib::ClipperOffset m_offset;
    double m_arc_tolerance = 0.25;
    double m_miter_limit = 2.0;

public:
    // Property-style accessors for Clipper1 compatibility
    double &ArcTolerance() { return m_arc_tolerance; }
    double &MiterLimit() { return m_miter_limit; }

    // Clipper1-compatible AddPath/AddPaths
    void AddPath(const Path &path, JoinType jt, EndType et)
    {
        // Apply stored settings
        m_offset.ArcTolerance(m_arc_tolerance);
        m_offset.MiterLimit(m_miter_limit);
        m_offset.AddPath(path, jt, et);
    }

    void AddPaths(const Paths &paths, JoinType jt, EndType et)
    {
        // Apply stored settings
        m_offset.ArcTolerance(m_arc_tolerance);
        m_offset.MiterLimit(m_miter_limit);
        m_offset.AddPaths(paths, jt, et);
    }

    // Clipper1-compatible Execute (note parameter order!)
    void Execute(Paths &solution, double delta)
    {
        m_offset.Execute(delta, solution); // Swapped for Clipper2
    }

    void Execute(PolyTreeWrapper &solution, double delta)
    {
        m_offset.Execute(delta, solution.get()); // Swapped for Clipper2
    }

    void Clear() { m_offset.Clear(); }
};

// Helper functions for compatibility

// Clipper1's SimplifyPolygons becomes union_ in Clipper2
inline void SimplifyPolygons(const Paths &input, Paths &output, PolyFillType fillType = pftEvenOdd)
{
    output = Clipper2Lib::Union(input, fillType, Clipper2Lib::FillRule::NonZero);
}

inline Paths SimplifyPolygons(const Paths &input, PolyFillType fillType = pftEvenOdd)
{
    return Clipper2Lib::Union(input, fillType, Clipper2Lib::FillRule::NonZero);
}

// Clipper1's Orientation becomes IsPositive in Clipper2
inline bool Orientation(const Path &path)
{
    return Clipper2Lib::IsPositive(path);
}

// Clipper1's Area function
inline double Area(const Path &path)
{
    return Clipper2Lib::Area(path);
}

// Clipper1's ReversePath - just use std::reverse
inline void ReversePath(Path &path)
{
    std::reverse(path.begin(), path.end());
}

// Clipper1's PointInPolygon
inline int PointInPolygon(const IntPoint &pt, const Path &path)
{
    return Clipper2Lib::PointInPolygon(pt, path);
}

// GetBounds - convert to free function
inline Clipper2Lib::Rect64 GetBounds(const Paths &paths)
{
    return Clipper2Lib::GetBounds(paths);
}

inline Clipper2Lib::Rect64 GetBounds(const Path &path)
{
    return Clipper2Lib::GetBounds(path);
}

} // namespace ClipperWrapper
} // namespace Slic3r

#endif // slic3r_clipper_hpp