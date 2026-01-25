///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef POINTGRID_HPP
#define POINTGRID_HPP

#include <libslic3r/Execution/Execution.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/BoundingBox.hpp>

namespace Slic3r
{

template<class T>
class PointGrid
{
    Vec3i m_size;
    std::vector<Vec<3, T>> m_data;
    const int XY;

public:
    explicit PointGrid(std::vector<Vec<3, T>> data, const Vec3i &size)
        : m_data(std::move(data)), m_size{size}, XY{m_size.x() * m_size.y()}
    {
    }

    const Vec<3, T> &get(size_t idx) const { return m_data[idx]; }
    const Vec<3, T> &get(const Vec3i &coord) const { return m_data[get_idx(coord)]; }

    size_t get_idx(const Vec3i &coord) const
    {
        size_t ret = coord.z() * XY + coord.y() * m_size.x() + coord.x();

        return ret;
    }

    Vec3i get_coord(size_t idx) const
    {
        int iz = idx / XY;
        int iy = (idx / m_size.x()) % m_size.y();
        int ix = idx % m_size.x();

        return {ix, iy, iz};
    }

    const std::vector<Vec<3, T>> &data() const { return m_data; }
    size_t point_count() const { return m_data.size(); }
    bool empty() const { return m_data.empty(); }
};

template<class Ex, class CoordT>
PointGrid<CoordT> point_grid(Ex policy, const BoundingBox3Base<Vec<3, CoordT>> &bounds, const Vec<3, CoordT> &stride)
{
    Vec3i numpts = Vec3i::Zero();

    for (int n = 0; n < 3; ++n)
        numpts(n) = (bounds.max(n) - bounds.min(n)) / stride(n);

    std::vector<Vec<3, CoordT>> out(numpts.x() * numpts.y() * numpts.z());

    size_t XY = numpts[X] * numpts[Y];

    execution::for_each(policy, size_t(0), out.size(),
                        [&](size_t i)
                        {
                            int iz = i / XY;
                            int iy = (i / numpts[X]) % numpts[Y];
                            int ix = i % numpts[X];

                            out[i] = Vec<3, CoordT>(ix * stride.x(), iy * stride.y(), iz * stride.z());
                        });

    return PointGrid{std::move(out), numpts};
}

} // namespace Slic3r

#endif // POINTGRID_HPP
