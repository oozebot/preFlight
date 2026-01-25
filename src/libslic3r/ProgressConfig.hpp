///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_ProgressConfig_hpp_
#define slic3r_ProgressConfig_hpp_

//
// This configuration allows easy adjustment of progress bar granularity.
// Each weight represents the RELATIVE time spent in that operation.
// Higher weight = more progress bar movement for that step.
// Actual percentages are auto-calculated based on relative weights.
//
// USAGE:
// 1. Adjust weights below to match observed performance
// 2. Weights automatically distribute across 0-50% range
// 3. Use ProgressTracker class to report progress with weights
//
// TUNING GUIDE:
// - Start with estimate based on typical print complexity
// - Test with various models (simple, complex, with supports, etc.)
// - Adjust weights where progress feels stuck or jumps too fast
// - Total weight doesn't matter - it's the RATIO that counts
//

namespace Slic3r
{

struct ProgressConfig
{
    // Phase 1: Slicing and Preparation (0-33% of total progress)
    // These weights are relative to each other within the 0-33% range
    // Weights based on actual timing analysis of large, complex models

    struct SlicingPhase
    {
        // Initial mesh processing (posSlice step) - ~instant, keep minimal
        float prepare_layers = 0.01f;         // Creating layer structure (instant)
        float slice_volumes = 3.6f;           // Actual mesh slicing (3.63% of base time)
        float process_sliced_regions = 0.01f; // Region processing (instant)
        float process_geometry = 0.02f;       // Bounding box updates (instant)
        float build_layer_graph = 0.9f;       // Z-graph linking (0.90% of base time)

        // Per-object processing steps - TUNED from actual timing data
        float perimeters = 27.2f;     // Generating perimeters (27.19% of base time)
        float prepare_infill = 33.1f; // Preparing infill regions (33.12% of base time - LARGEST!)
        float making_infill = 28.3f;  // Generating infill patterns (28.31% of base time)

        // Optional/conditional steps (only contribute if enabled) - TUNED from actual timing
        float support_spots = 18.8f;     // Searching support spots (18.82% when enabled)
        float support_material = 12.1f;  // Generating supports (12.05% when enabled)
        float curled_extrusions = 0.2f;  // Estimating curling (0.18% when enabled)
        float overhanging_perims = 0.2f; // Calculating overhangs (0.18% when enabled)

        // Final print-level steps - TUNED from actual timing
        float skirt_brim = 9.9f;     // Generating skirt/brim (9.90% of base time)
        float supports_alert = 0.0f; // Checking if supports needed (instant)

        // Calculate total weight (for base steps always present)
        float total_base() const
        {
            return prepare_layers + slice_volumes + process_sliced_regions + process_geometry + build_layer_graph +
                   perimeters + prepare_infill + making_infill + skirt_brim;
        }

        // Calculate total including conditional steps (call this at runtime)
        float total_with_conditionals(bool has_support_spots, bool has_support_material, bool has_curled_extrusions,
                                      bool has_overhanging_perims, bool has_supports_alert) const
        {
            float total = total_base();
            if (has_support_spots)
                total += support_spots;
            if (has_support_material)
                total += support_material;
            if (has_curled_extrusions)
                total += curled_extrusions;
            if (has_overhanging_perims)
                total += overhanging_perims;
            if (has_supports_alert)
                total += supports_alert;
            return total;
        }
    } slicing;

    // Phase 2: GCode Text Generation (33-50% of total progress)
    // This is the _do_export() function generating actual GCode text
    // Takes ~17% of total time based on actual measurements
    struct GCodeGenerationPhase
    {
        // Layer-by-layer GCode generation
        // Each layer gets equal weight within this 17% range
        // Progress calculated as: 33 + ((current_layer / total_layers) * 17)

        // Progress updates happen during layer processing in _do_export()
        // The GCodeGenerator already has layer counting
    } gcode_generation;

    // Phase 3: Processor Finalization (50-85% of total progress)
    // This is m_processor.finalize() parsing GCode and building preview data
    // Takes ~35% of total time based on actual measurements
    struct ProcessorFinalizePhase
    {
        // The processor.finalize() processes moves and calculates print times
        // Progress hooks are in preFlight.GCodeProcessor.cpp finalize()
        float finalize = 35.0f; // 35% of total (50-85%)
    } processor_finalize;

    // Phase 4: Data Conversion (85-100% of total progress)
    // Converting GCode data to GPU-renderable format
    // Takes ~15% of total time based on actual measurements
    struct DataConversionPhase
    {
        // Convert from preFlight format to libvgcode format
        // Progress reporting in preFlight.GCodeViewer.cpp load_as_gcode()
        float conversion = 15.0f; // 15% of total (85-100%)
    } data_conversion;
};

// Progress tracker helper class
class ProgressTracker
{
private:
    float m_accumulated_weight;
    float m_total_weight;
    class Print *m_print;

public:
    ProgressTracker(class Print *print, float total_weight)
        : m_accumulated_weight(0.0f), m_total_weight(total_weight), m_print(print)
    {
    }

    // Report progress by adding a weight increment
    void report(float weight, const std::string &message);

    // Get current percentage (0-33 range for slicing phase)
    int current_percent() const { return static_cast<int>((m_accumulated_weight / m_total_weight) * 33.0f); }

    // Reset for new tracking session
    void reset(float new_total_weight)
    {
        m_accumulated_weight = 0.0f;
        m_total_weight = new_total_weight;
    }

    // Add weight without reporting (for conditional steps)
    void add_weight(float weight) { m_accumulated_weight += weight; }
};

// Global configuration instance
// This can be adjusted at runtime if needed for different scenarios
extern ProgressConfig g_progress_config;

} // namespace Slic3r

#endif // slic3r_ProgressConfig_hpp_
