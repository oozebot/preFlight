///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2022 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "AABBMesh.hpp"

#include <luminary/geometry/index/AABBTreeIndirect.hpp>
#include <luminary/mesh/core/TriangleMesh.hpp>
#include <igl/Hit.h>
#include <algorithm>

#include "admesh/stl.h"
#include "luminary/geometry/primitives/Point.hpp"

namespace Luminary
{

class AABBMesh::AABBImpl
{
private:
    AABBTreeIndirect::Tree3f m_tree;
    double m_triangle_ray_epsilon;

public:
    void init(const indexed_triangle_set &its, bool calculate_epsilon)
    {
        m_triangle_ray_epsilon = 0.000001;
        if (calculate_epsilon)
        {
            // Calculate epsilon from average triangle edge length.
            double l = its_average_edge_length(its);
            if (l > 0)
                m_triangle_ray_epsilon = 0.000001 * l * l;
        }
        m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices);
    }

    void intersect_ray(const indexed_triangle_set &its, const Vec3d &s, const Vec3d &dir, igl::Hit &hit)
    {
        AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices, m_tree, s, dir, hit,
                                                  m_triangle_ray_epsilon);
    }

    void intersect_ray(const indexed_triangle_set &its, const Vec3d &s, const Vec3d &dir, std::vector<igl::Hit> &hits)
    {
        AABBTreeIndirect::intersect_ray_all_hits(its.vertices, its.indices, m_tree, s, dir, hits,
                                                 m_triangle_ray_epsilon);
    }

    double squared_distance(const indexed_triangle_set &its, const Vec3d &point, int &i,
                            Eigen::Matrix<double, 1, 3> &closest)
    {
        size_t idx_unsigned = 0;
        Vec3d closest_vec3d(closest);
        double dist = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(its.vertices, its.indices, m_tree,
                                                                                 point, idx_unsigned, closest_vec3d);
        i = int(idx_unsigned);
        closest = closest_vec3d;
        return dist;
    }
};

template<class M>
void AABBMesh::init(const M &mesh, bool calculate_epsilon)
{
    // Build the AABB accelaration tree
    m_aabb->init(*m_tm, calculate_epsilon);
}

AABBMesh::AABBMesh(const indexed_triangle_set &tmesh, bool calculate_epsilon)
    : m_tm(&tmesh), m_aabb(new AABBImpl()), m_vfidx{tmesh}, m_fnidx{its_face_neighbors(tmesh)}
{
    init(tmesh, calculate_epsilon);
}

AABBMesh::AABBMesh(const TriangleMesh &mesh, bool calculate_epsilon)
    : m_tm(&mesh.its), m_aabb(new AABBImpl()), m_vfidx{mesh.its}, m_fnidx{its_face_neighbors(mesh.its)}
{
    init(mesh, calculate_epsilon);
}

AABBMesh::~AABBMesh() {}

AABBMesh::AABBMesh(const AABBMesh &other)
    : m_tm(other.m_tm), m_aabb(new AABBImpl(*other.m_aabb)), m_vfidx{other.m_vfidx}, m_fnidx{other.m_fnidx}
{
}

AABBMesh &AABBMesh::operator=(const AABBMesh &other)
{
    m_tm = other.m_tm;
    m_aabb.reset(new AABBImpl(*other.m_aabb));
    m_vfidx = other.m_vfidx;
    m_fnidx = other.m_fnidx;

    return *this;
}

AABBMesh &AABBMesh::operator=(AABBMesh &&other) = default;

AABBMesh::AABBMesh(AABBMesh &&other) = default;

const std::vector<Vec3f> &AABBMesh::vertices() const
{
    return m_tm->vertices;
}

const std::vector<Vec3i> &AABBMesh::indices() const
{
    return m_tm->indices;
}

const Vec3f &AABBMesh::vertices(size_t idx) const
{
    return m_tm->vertices[idx];
}

const Vec3i &AABBMesh::indices(size_t idx) const
{
    return m_tm->indices[idx];
}

Vec3d AABBMesh::normal_by_face_id(int face_id) const
{
    return its_unnormalized_normal(*m_tm, face_id).cast<double>().normalized();
}

AABBMesh::hit_result AABBMesh::query_ray_hit(const Vec3d &s, const Vec3d &dir) const
{
    assert(is_approx(dir.norm(), 1.));
    igl::Hit hit{-1, -1, 0.f, 0.f, 0.f};
    hit.t = std::numeric_limits<float>::infinity();

    m_aabb->intersect_ray(*m_tm, s, dir, hit);
    hit_result ret(*this);
    ret.m_t = double(hit.t);
    ret.m_dir = dir;
    ret.m_source = s;
    if (!std::isinf(hit.t) && !std::isnan(hit.t))
    {
        ret.m_normal = this->normal_by_face_id(hit.id);
        ret.m_face_id = hit.id;
    }

    return ret;
}

std::vector<AABBMesh::hit_result> AABBMesh::query_ray_hits(const Vec3d &s, const Vec3d &dir) const
{
    std::vector<AABBMesh::hit_result> outs;
    std::vector<igl::Hit> hits;
    m_aabb->intersect_ray(*m_tm, s, dir, hits);

    // The sort is necessary, the hits are not always sorted.
    std::sort(hits.begin(), hits.end(), [](const igl::Hit &a, const igl::Hit &b) { return a.t < b.t; });

    // Remove duplicates. They sometimes appear, for example when the ray is cast
    // along an axis of a cube due to floating-point approximations in igl (?)
    hits.erase(std::unique(hits.begin(), hits.end(), [](const igl::Hit &a, const igl::Hit &b) { return a.t == b.t; }),
               hits.end());

    //  Convert the igl::Hit into hit_result
    outs.reserve(hits.size());
    for (const igl::Hit &hit : hits)
    {
        outs.emplace_back(AABBMesh::hit_result(*this));
        outs.back().m_t = double(hit.t);
        outs.back().m_dir = dir;
        outs.back().m_source = s;
        if (!std::isinf(hit.t) && !std::isnan(hit.t))
        {
            outs.back().m_normal = this->normal_by_face_id(hit.id);
            outs.back().m_face_id = hit.id;
        }
    }

    return outs;
}

double AABBMesh::squared_distance(const Vec3d &p, int &i, Vec3d &c) const
{
    double sqdst = 0;
    Eigen::Matrix<double, 1, 3> pp = p;
    Eigen::Matrix<double, 1, 3> cc;
    sqdst = m_aabb->squared_distance(*m_tm, pp, i, cc);
    c = cc;
    return sqdst;
}

} // namespace Luminary
