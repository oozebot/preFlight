///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) OrcaSlicer 2024 Arachnid
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef libslic3r_NoiseGenerator_hpp_
#define libslic3r_NoiseGenerator_hpp_

#include <cmath>
#include <cstdint>
#include <random>
#include <thread>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::Feature::FuzzySkin
{

// Configuration struct for fuzzy skin noise parameters
struct FuzzySkinConfig
{
    FuzzySkinType type = FuzzySkinType::None;
    double thickness = 0.0;      // scaled thickness
    double point_distance = 0.0; // scaled point distance
    bool first_layer = false;
    bool on_top = true; // Apply fuzzy on top surfaces (if false, skip top-visible segments)
    FuzzySkinNoiseType noise_type = FuzzySkinNoiseType::Classic;
    FuzzySkinMode mode = FuzzySkinMode::Displacement;
    double scale = 3.0; // feature size in mm
    int octaves = 4;
    double persistence = 0.5;
    FuzzySkinPointPlacement point_placement = FuzzySkinPointPlacement::Standard;
    double visibility_detection_interval =
        2.0;                    // mm between visibility checks (1=precise, 2=standard, 4=relaxed, 8=minimal)
    int max_perimeter_idx = -1; // -1 = unlimited (all perimeters), 0 = external only, 1 = external+1, etc.

    bool operator==(const FuzzySkinConfig &other) const
    {
        return type == other.type && thickness == other.thickness && point_distance == other.point_distance &&
               first_layer == other.first_layer && on_top == other.on_top && noise_type == other.noise_type &&
               mode == other.mode && scale == other.scale && octaves == other.octaves &&
               persistence == other.persistence && point_placement == other.point_placement &&
               visibility_detection_interval == other.visibility_detection_interval &&
               max_perimeter_idx == other.max_perimeter_idx;
    }

    bool operator!=(const FuzzySkinConfig &other) const { return !(*this == other); }
};

// Hash function for FuzzySkinConfig (for use in unordered_map)
struct FuzzySkinConfigHash
{
    std::size_t operator()(const FuzzySkinConfig &cfg) const
    {
        std::size_t h = 0;
        h ^= std::hash<int>{}(static_cast<int>(cfg.type)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<double>{}(cfg.thickness) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<double>{}(cfg.point_distance) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(cfg.first_layer) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(cfg.on_top) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(cfg.noise_type)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(cfg.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<double>{}(cfg.scale) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(cfg.octaves) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<double>{}(cfg.persistence) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(cfg.point_placement)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<double>{}(cfg.visibility_detection_interval) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(cfg.max_perimeter_idx) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Base class for noise generators
class NoiseModule
{
public:
    virtual ~NoiseModule() = default;
    virtual double getValue(double x, double y, double z) const = 0;
};

// Uniform random noise (classic fuzzy skin behavior)
class UniformNoise : public NoiseModule
{
public:
    double getValue(double /*x*/, double /*y*/, double /*z*/) const override
    {
        thread_local std::random_device rd;
        thread_local std::mt19937 gen(
            rd.entropy() > 0 ? rd() : static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        thread_local std::uniform_real_distribution<double> dist(-1.0, 1.0);
        return dist(gen);
    }
};

// Perlin noise implementation
class PerlinNoise : public NoiseModule
{
public:
    PerlinNoise(double frequency = 1.0, int octaves = 4, double persistence = 0.5)
        : m_frequency(frequency), m_octaves(octaves), m_persistence(persistence)
    {
        // Initialize permutation table
        for (int i = 0; i < 256; ++i)
            p[i] = i;
        std::mt19937 rng(42); // Fixed seed for reproducibility
        for (int i = 255; i > 0; --i)
        {
            std::uniform_int_distribution<int> dist(0, i);
            std::swap(p[i], p[dist(rng)]);
        }
        for (int i = 0; i < 256; ++i)
            p[256 + i] = p[i];
    }

    double getValue(double x, double y, double z) const override
    {
        double result = 0.0;
        double amplitude = 1.0;
        double frequency = m_frequency;
        double maxValue = 0.0;

        for (int i = 0; i < m_octaves; ++i)
        {
            result += noise(x * frequency, y * frequency, z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= m_persistence;
            frequency *= 2.0;
        }

        return result / maxValue;
    }

private:
    double m_frequency;
    int m_octaves;
    double m_persistence;
    int p[512];

    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int hash, double x, double y, double z)
    {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    double noise(double x, double y, double z) const
    {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        int Z = static_cast<int>(std::floor(z)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        z -= std::floor(z);
        double u = fade(x), v = fade(y), w = fade(z);
        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
        int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
        return lerp(w,
                    lerp(v, lerp(u, grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z)),
                         lerp(u, grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z))),
                    lerp(v, lerp(u, grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1)),
                         lerp(u, grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1))));
    }
};

// Billow noise - absolute value of Perlin, creating cloud-like appearance
class BillowNoise : public NoiseModule
{
public:
    BillowNoise(double frequency = 1.0, int octaves = 4, double persistence = 0.5)
        : m_perlin(frequency, octaves, persistence)
    {
    }

    double getValue(double x, double y, double z) const override
    {
        // Billow is 2 * |perlin| - 1, scaled to [-1, 1]
        double value = m_perlin.getValue(x, y, z);
        return 2.0 * std::abs(value) - 1.0;
    }

private:
    PerlinNoise m_perlin;
};

// Ridged multifractal noise - creates sharp, jagged features
class RidgedMultiNoise : public NoiseModule
{
public:
    RidgedMultiNoise(double frequency = 1.0, int octaves = 4)
        : m_frequency(frequency), m_octaves(octaves), m_perlin(1.0, 1, 1.0)
    {
    }

    double getValue(double x, double y, double z) const override
    {
        double result = 0.0;
        double frequency = m_frequency;
        double weight = 1.0;
        const double offset = 1.0;
        const double gain = 2.0;

        for (int i = 0; i < m_octaves; ++i)
        {
            double signal = m_perlin.getValue(x * frequency, y * frequency, z * frequency);
            signal = offset - std::abs(signal);
            signal *= signal;
            signal *= weight;
            weight = std::clamp(signal * gain, 0.0, 1.0);
            result += signal * std::pow(frequency, -0.9);
            frequency *= 2.0;
        }

        // Normalize to [-1, 1]
        return result * 1.25 - 1.0;
    }

private:
    double m_frequency;
    int m_octaves;
    PerlinNoise m_perlin;
};

// Voronoi (cellular) noise - creates cell-based patchwork patterns
class VoronoiNoise : public NoiseModule
{
public:
    VoronoiNoise(double frequency = 1.0, double displacement = 1.0)
        : m_frequency(frequency), m_displacement(displacement)
    {
    }

    double getValue(double x, double y, double z) const override
    {
        x *= m_frequency;
        y *= m_frequency;
        z *= m_frequency;

        int xi = static_cast<int>(std::floor(x));
        int yi = static_cast<int>(std::floor(y));
        int zi = static_cast<int>(std::floor(z));

        double minDist = 1e10;

        // Check 3x3x3 neighborhood
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int cx = xi + dx;
                    int cy = yi + dy;
                    int cz = zi + dz;

                    // Get cell's feature point
                    double fx = cx + cellNoise(cx, cy, cz, 0);
                    double fy = cy + cellNoise(cx, cy, cz, 1);
                    double fz = cz + cellNoise(cx, cy, cz, 2);

                    double dist = (fx - x) * (fx - x) + (fy - y) * (fy - y) + (fz - z) * (fz - z);
                    if (dist < minDist)
                        minDist = dist;
                }
            }
        }

        // Return distance-based value, normalized to approximately [-1, 1]
        return std::sqrt(minDist) * m_displacement * 2.0 - 1.0;
    }

private:
    double m_frequency;
    double m_displacement;

    // Simple hash-based cell noise
    static double cellNoise(int x, int y, int z, int seed)
    {
        uint32_t n = static_cast<uint32_t>(x * 1619 + y * 31337 + z * 6971 + seed * 1013);
        n = (n >> 13) ^ n;
        n = n * (n * n * 60493 + 19990303) + 1376312589;
        return static_cast<double>(n & 0x7fffffff) / 0x7fffffff;
    }
};

// Factory function to create appropriate noise module
inline std::unique_ptr<NoiseModule> createNoiseModule(const FuzzySkinConfig &cfg)
{
    double frequency = 1.0 / cfg.scale;

    switch (cfg.noise_type)
    {
    case FuzzySkinNoiseType::Perlin:
        return std::make_unique<PerlinNoise>(frequency, cfg.octaves, cfg.persistence);
    case FuzzySkinNoiseType::Billow:
        return std::make_unique<BillowNoise>(frequency, cfg.octaves, cfg.persistence);
    case FuzzySkinNoiseType::RidgedMulti:
        return std::make_unique<RidgedMultiNoise>(frequency, cfg.octaves);
    case FuzzySkinNoiseType::Voronoi:
        return std::make_unique<VoronoiNoise>(frequency, 1.0);
    case FuzzySkinNoiseType::Classic:
    default:
        return std::make_unique<UniformNoise>();
    }
}

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_NoiseGenerator_hpp_
