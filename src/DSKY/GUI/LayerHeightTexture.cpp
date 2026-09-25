///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "LayerHeightTexture.hpp"

#include <algorithm>
#include <cmath>

#include "luminary/geometry/primitives/Point.hpp"

namespace DSKY
{
using namespace Luminary;

int generate_layer_height_texture(const SlicingParameters &slicing_params, const std::vector<coordf_t> &layers,
                                  void *data, int rows, int cols, bool level_of_detail_2nd_level)
{
    // https://github.com/aschn/gnuplot-colorbrewer
    std::vector<Vec3crd> palette_raw;
    palette_raw.push_back(Vec3crd(0x01A, 0x098, 0x050));
    palette_raw.push_back(Vec3crd(0x066, 0x0BD, 0x063));
    palette_raw.push_back(Vec3crd(0x0A6, 0x0D9, 0x06A));
    palette_raw.push_back(Vec3crd(0x0D9, 0x0F1, 0x0EB));
    palette_raw.push_back(Vec3crd(0x0FE, 0x0E6, 0x0EB));
    palette_raw.push_back(Vec3crd(0x0FD, 0x0AE, 0x061));
    palette_raw.push_back(Vec3crd(0x0F4, 0x06D, 0x043));
    palette_raw.push_back(Vec3crd(0x0D7, 0x030, 0x027));

    // Clear the main texture and the 2nd LOD level.
    //	memset(data, 0, rows * cols * (level_of_detail_2nd_level ? 5 : 4));
    // 2nd LOD level data start
    unsigned char *data1 = reinterpret_cast<unsigned char *>(data) + rows * cols * 4;
    int ncells = std::min((cols - 1) * rows,
                          int(ceil(16. * (slicing_params.object_print_z_height() / slicing_params.min_layer_height))));
    int ncells1 = ncells / 2;
    int cols1 = cols / 2;
    coordf_t z_to_cell = coordf_t(ncells - 1) / slicing_params.object_print_z_height();
    coordf_t cell_to_z = slicing_params.object_print_z_height() / coordf_t(ncells - 1);
    coordf_t z_to_cell1 = coordf_t(ncells1 - 1) / slicing_params.object_print_z_height();
    // for color scaling
    coordf_t hscale = 2.f * std::max(slicing_params.max_layer_height - slicing_params.layer_height,
                                     slicing_params.layer_height - slicing_params.min_layer_height);
    if (hscale == 0)
        // All layers have the same height. Provide some height scale to avoid division by zero.
        hscale = slicing_params.layer_height;
    for (size_t idx_layer = 0; idx_layer < layers.size(); idx_layer += 2)
    {
        coordf_t lo = layers[idx_layer];
        coordf_t hi = layers[idx_layer + 1];
        coordf_t mid = 0.5f * (lo + hi);
        assert(mid <= slicing_params.object_print_z_height());
        coordf_t h = hi - lo;
        hi = std::min(hi, slicing_params.object_print_z_height());
        int cell_first = std::clamp(int(ceil(lo * z_to_cell)), 0, ncells - 1);
        int cell_last = std::clamp(int(floor(hi * z_to_cell)), 0, ncells - 1);
        for (int cell = cell_first; cell <= cell_last; ++cell)
        {
            coordf_t idxf = (0.5 * hscale + (h - slicing_params.layer_height)) * coordf_t(palette_raw.size() - 1) /
                            hscale;
            int idx1 = std::clamp(int(floor(idxf)), 0, int(palette_raw.size() - 1));
            int idx2 = std::min(int(palette_raw.size() - 1), idx1 + 1);
            coordf_t t = idxf - coordf_t(idx1);
            const Vec3crd &color1 = palette_raw[idx1];
            const Vec3crd &color2 = palette_raw[idx2];
            coordf_t z = cell_to_z * coordf_t(cell);
            assert(lo - EPSILON <= z && z <= hi + EPSILON);
            // Intensity profile to visualize the layers.
            coordf_t intensity = cos(M_PI * 0.7 * (mid - z) / h);
            // Color mapping from layer height to RGB.
            Vec3d color(intensity * lerp(coordf_t(color1(0)), coordf_t(color2(0)), t),
                        intensity * lerp(coordf_t(color1(1)), coordf_t(color2(1)), t),
                        intensity * lerp(coordf_t(color1(2)), coordf_t(color2(2)), t));
            int row = cell / (cols - 1);
            int col = cell - row * (cols - 1);
            assert(row >= 0 && row < rows);
            assert(col >= 0 && col < cols);
            unsigned char *ptr = (unsigned char *) data + (row * cols + col) * 4;
            ptr[0] = (unsigned char) std::clamp(int(floor(color(0) + 0.5)), 0, 255);
            ptr[1] = (unsigned char) std::clamp(int(floor(color(1) + 0.5)), 0, 255);
            ptr[2] = (unsigned char) std::clamp(int(floor(color(2) + 0.5)), 0, 255);
            ptr[3] = 255;
            if (col == 0 && row > 0)
            {
                // Duplicate the first value in a row as a last value of the preceding row.
                ptr[-4] = ptr[0];
                ptr[-3] = ptr[1];
                ptr[-2] = ptr[2];
                ptr[-1] = ptr[3];
            }
        }
        if (level_of_detail_2nd_level)
        {
            cell_first = std::clamp(int(ceil(lo * z_to_cell1)), 0, ncells1 - 1);
            cell_last = std::clamp(int(floor(hi * z_to_cell1)), 0, ncells1 - 1);
            for (int cell = cell_first; cell <= cell_last; ++cell)
            {
                coordf_t idxf = (0.5 * hscale + (h - slicing_params.layer_height)) * coordf_t(palette_raw.size() - 1) /
                                hscale;
                int idx1 = std::clamp(int(floor(idxf)), 0, int(palette_raw.size() - 1));
                int idx2 = std::min(int(palette_raw.size() - 1), idx1 + 1);
                coordf_t t = idxf - coordf_t(idx1);
                const Vec3crd &color1 = palette_raw[idx1];
                const Vec3crd &color2 = palette_raw[idx2];
                // Color mapping from layer height to RGB.
                Vec3d color(lerp(coordf_t(color1(0)), coordf_t(color2(0)), t),
                            lerp(coordf_t(color1(1)), coordf_t(color2(1)), t),
                            lerp(coordf_t(color1(2)), coordf_t(color2(2)), t));
                int row = cell / (cols1 - 1);
                int col = cell - row * (cols1 - 1);
                assert(row >= 0 && row < rows / 2);
                assert(col >= 0 && col < cols / 2);
                unsigned char *ptr = data1 + (row * cols1 + col) * 4;
                ptr[0] = (unsigned char) std::clamp(int(floor(color(0) + 0.5)), 0, 255);
                ptr[1] = (unsigned char) std::clamp(int(floor(color(1) + 0.5)), 0, 255);
                ptr[2] = (unsigned char) std::clamp(int(floor(color(2) + 0.5)), 0, 255);
                ptr[3] = 255;
                if (col == 0 && row > 0)
                {
                    // Duplicate the first value in a row as a last value of the preceding row.
                    ptr[-4] = ptr[0];
                    ptr[-3] = ptr[1];
                    ptr[-2] = ptr[2];
                    ptr[-1] = ptr[3];
                }
            }
        }
    }

    // Returns number of cells of the 0th LOD level.
    return ncells;
}

} // namespace DSKY
