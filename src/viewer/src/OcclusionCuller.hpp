///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "../include/Types.hpp"
#include "../include/Viewer.hpp" // OcclusionViewStats, OcclusionBenchStats
#include "OcclusionTest.hpp"     // SlabCandidate, ChunkDistance
#include "PrintChunks.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace libvgcode
{

// The occlusion step on the GPU. For a view (matrices, camera position,
// viewport size) it finds the sub-cells of the whole print's chunk structure whose boxes are not behind the depth of
// what was drawn for that view before:
// 1. the enabled segments of the sub-cells that passed last time (the predicted set) are drawn depth only, with the
//    same program and matrices as the pass, into a single-sample depth target of the pass's size;
// 2. a farthest-depth pyramid is built from it (build_depth_pyramid's rule);
// 3. one point per chunk box (4 taps), then one per sub-cell box of the chunks that passed (8 taps), runs
//    test_box_occlusion_faces in a vertex shader, with margins of its own that keep it from ever being stricter than
//    that rule, and writes a byte to a result target; one readback gives the bytes;
// 4. while the sub-cells that passed and were not predicted hold more enabled segments than the budget, their part
//    nearest to the camera is drawn on top (a slab that doubles each round), the pyramid rebuilt and the test run
//    again;
// 5. the enabled segments of the sub-cells that passed the last test are the draw set, written chunk by chunk nearest
//    first, which becomes the next prediction. With an occluder cap, step 1 draws only the first segments of the
//    predicted set's list, its nearest part.
// 6. when the caller asks and step 1 drew the whole predicted set, the residual is written too: the enabled segments of
//    the sub-cells that passed the last test and were drawn neither in step 1 nor in a slab. merge_depth() then writes
//    the step's depth into the caller's target, keeping the nearer depth, and the caller draws only the residual.
// The structure must have the filter applied (apply_print_chunk_filter): a sub-cell's enabled segments are its slot
// prefix, so every list is a copy of slot prefixes, and only sub-cells that hold an enabled segment are tested.
// Every occluder is an enabled segment the pass draws, so at a pixel centre of the target no occluder lies nearer than
// the nearest drawn bead there, and the sub-cell of that bead passes (OcclusionTest.hpp states why).
// The merge is exact: every occluder is an enabled bead drawn with the pass's program and matrices at the pass's
// viewport size, so its depth in the target is the depth the pass would write; each sub-cell that passed the last test
// is among the occluders or in the residual; so the merged depth with the residual holds the nearest depth of every
// enabled bead that is nearest somewhere, as drawing the whole draw set does.

// The views a result is kept for: the camera's (shared by the G-buffer and the visible pass) and the shadow pass's
static constexpr size_t OCCLUSION_VIEW_CAMERA = 0;
static constexpr size_t OCCLUSION_VIEW_SHADOW = 1;
static constexpr size_t OCCLUSION_VIEWS = 2;

// What a step reads from the viewer: the whole print's structure with the filter applied, and the generations that
// change with the structure, its boxes and the filter applied
struct OcclusionInputs
{
    const PrintChunks *chunks{nullptr};
    uint64_t structure_generation{0};
    uint64_t boxes_generation{0};
    uint64_t filter_generation{0};
};

// The view a pass draws with: its matrices and camera position as its program takes them, the pass's viewport size,
// and the viewer's frame count (within one frame a camera result is used at any viewport size)
struct OcclusionViewParams
{
    Mat4x4 view{};
    Mat4x4 projection{};
    Vec3 camera{};
    int width{0};
    int height{0};
    uint64_t frame{0};
};

// A result's draw set on the GPU (a texture buffer over a GL_R32UI buffer of vertex ids, which the culler owns), its
// segment count, and the chunks its sub-cells lie in. With depth_ready, a step in this call wrote the residual (the
// same kind of list, with the chunks its sub-cells lie in) and left its depth for merge_depth(); a step asked for the
// residual that gave none says why in residual_reason ("occluder cap", "upload failed", "list short"; empty
// otherwise).
struct OcclusionDrawList
{
    unsigned int tex_id{0};
    unsigned int buf_id{0};
    size_t count{0};
    size_t chunks{0};
    unsigned int residual_tex_id{0};
    unsigned int residual_buf_id{0};
    size_t residual_count{0};
    size_t residual_chunks{0};
    bool depth_ready{false};
    const char *residual_reason{""};
};

#ifdef PREFLIGHT_TEST_HOOKS
// The GPU test against test_box_occlusion_faces on the CPU, both on the culler's depth of one round from the camera
// view's draw set: the chunks that hold an enabled sub-cell, and the enabled sub-cells, compared; those only the GPU
// passes (harmless) and those only the CPU passes (unsafe). A sub-cell passes on the CPU when its chunk does there.
struct OcclusionCrossCheck
{
    size_t chunks{0};
    size_t chunk_gpu_only{0};
    size_t chunk_cpu_only{0};
    size_t subcells{0};
    size_t subcell_gpu_only{0};
    size_t subcell_cpu_only{0};
};
#endif // PREFLIGHT_TEST_HOOKS

class OcclusionCuller
{
public:
    // Draws a segment list (a texture buffer over a GL_R32UI buffer of vertex ids, `count` of them) depth only with the
    // segments depth program and the given view, as the passes draw: the viewer's clipping plane (none while occlusion
    // culling applies). The framebuffer, viewport and depth state are the culler's.
    using DrawDepth = std::function<void(unsigned int segments_tex_id, unsigned int segments_buf_id, size_t count,
                                         const Mat4x4 &view, const Mat4x4 &projection, const Vec3 &camera)>;

    OcclusionCuller() = default;
    OcclusionCuller(const OcclusionCuller &) = delete;
    OcclusionCuller &operator=(const OcclusionCuller &) = delete;

    void set_draw_depth(DrawDepth draw) { m_draw_depth = std::move(draw); }

    // The most segments of the predicted set drawn as first occluders (0: no cap). A changed cap makes the views'
    // stored results stale.
    void set_occluder_cap(size_t segments) { m_occluder_cap = segments; }
    size_t occluder_cap() const { return m_occluder_cap; }

    // The draw set of a view, written straight into the culler's buffer (`list`). A view whose matrices, camera
    // position, viewport size, generations and occluder cap equal its last result's (or, for the camera view, all but
    // the viewport size within the frame of that result) takes that result without GL work: `cached` true. Otherwise
    // a step runs, and with `want_residual` it also writes the residual (list.depth_ready, stats.residual_segments).
    // `stats` holds the result's figures (ms 0 when cached), and `bench` accumulates the step's or the cached use's.
    // False with stats.reason set when the pass must draw without it; every GL state the step touches is restored
    // either way and nothing throws.
    bool request(size_t view, const OcclusionInputs &inputs, const OcclusionViewParams &params, bool want_residual,
                 OcclusionDrawList &list, bool &cached, OcclusionViewStats &stats, OcclusionBenchStats &bench);

    // The whole enabled set as a view's draw set, with no step: every sub-cell's enabled segments, chunk by chunk in
    // structure order, written into the culler's buffer (`list`). Needs no program, target or readback. It holds for
    // any view, so a later call under the same structure and filter generations takes it without GL work (`cached`
    // true); request() never takes it as a stored result and request_all() never takes a step's, but a step that
    // follows predicts from it as from a step's result. `stats` holds its figures (all set, ms the write's, 0 when
    // cached), and `bench` counts the use in `all`. False with stats.reason set as for request(); the GL state is
    // restored either way and nothing throws.
    bool request_all(size_t view, const OcclusionInputs &inputs, OcclusionDrawList &list, bool &cached,
                     OcclusionViewStats &stats, OcclusionBenchStats &bench);

    // Writes the depth target of the view's step into the bound draw framebuffer over `viewport` (x, y, width, height),
    // each texel kept where it is nearer (GL_LESS), with depth writes on and color writes off; the framebuffer binding
    // stays and every other GL state it touches is restored. Only right after a request of that view that ran a step,
    // before any other request of it and before another step, a cross check or a reallocation replaces the target's
    // depth. False with `reason` ("no step depth", "viewport size", "merge program failed", "merge failed") when it did
    // not write; nothing throws.
    bool merge_depth(size_t view, const int viewport[4], std::string &reason);

    // Whether its programs failed to build (until release(true)), and the compiler or linker output of the last one
    // that failed (the merge program's failure leaves the culler working without the merge)
    bool programs_failed() const { return m_programs_failed; }
    const std::string &program_log() const { return m_program_log; }

    // The sub-cells the view's last result found visible, a byte per sub-cell (not 0: visible); empty without one
    const std::vector<uint8_t> &visible_subcells(size_t view) const;

    // Frees every GL object and forgets the views' results; the programs too, and their failure, when `programs`. On
    // the thread that owns the GL context.
    void release(bool programs);
    bool holds_gl_objects() const;
    // The bytes of its GPU objects and of its CPU tables
    size_t gpu_bytes() const;
    size_t cpu_bytes() const;

#ifdef PREFLIGHT_TEST_HOOKS
    // Runs steps 1 to 3 once for the camera view's last result, its draw set as the occluders, reads the depth back and
    // compares the GPU's bytes with the CPU rule on the pyramid of that depth. False, with `error`, when it could not
    // run. Restores every GL state it touches.
    bool cross_check(const OcclusionInputs &inputs, OcclusionCrossCheck &out, std::string &error);
#endif // PREFLIGHT_TEST_HOOKS

private:
    // A segment list on the GPU, its buffer allocated with room to grow and orphaned at each upload
    struct GpuList
    {
        unsigned int buf{0};
        unsigned int tex{0};
        size_t count{0};
        size_t capacity_bytes{0};
    };
    struct ViewState
    {
        // A result is held: the view, generations and occluder cap it was made under, and its draw set on the GPU;
        // `all` when it is the whole enabled set (request_all), which keeps no view and is never a step's stored
        // result
        bool valid{false};
        bool all{false};
        OcclusionViewParams params;
        uint64_t structure_generation{0};
        uint64_t boxes_generation{0};
        uint64_t filter_generation{0};
        size_t occluder_cap{0};
        GpuList list;
        size_t chunks{0};
        // The residual of its last step that wrote one (meaningful only to the call that ran that step)
        GpuList residual;
        OcclusionViewStats result;
        // The sub-cells that passed its last test, or for a whole enabled set every one that holds an enabled segment
        // (the prediction of the next step), the chunks that hold them in the order its list was written, and the
        // structure they index; kept through a filter change
        std::vector<uint8_t> predicted;
        std::vector<uint32_t> chunk_order;
        uint64_t predicted_structure{0};
    };
    // The wall time of a step's parts, in milliseconds
    struct StepTimes
    {
        double emit{0.0};
        double depth{0.0};
        double pyramid{0.0};
        double test{0.0};
        double readback{0.0};
        double list{0.0};
    };

    // Builds the programs on the first use; false once they failed
    bool build_programs();
    // The parts of a step, each false with `reason` on a failure; the GL state is the caller's to restore
    bool upload_static(const OcclusionInputs &inputs, std::string &reason);
    bool upload_flags(const OcclusionInputs &inputs, std::string &reason);
    bool ensure_targets(int width, int height, size_t chunks, size_t subcells, std::string &reason);
    bool upload_list(GpuList &list, const std::vector<uint32_t> &segments);
    // Writes a list of at most `count` segments: `fill` writes them from the pointer it is given and returns how many
    // it wrote. Into the buffer mapped for writing where the driver allows it; otherwise (the map or the unmap failed,
    // counted in m_map_fallbacks with m_map_fallback_reason) into m_list_scratch, uploaded by upload_list.
    bool write_list(GpuList &list, size_t count, const std::function<size_t(uint32_t *)> &fill);
    // Clears the depth target and draws the view's predicted set, at most the occluder cap of its segments, nearest
    // first (in structure order after a whole enabled set): its uploaded draw set when made under the same structure
    // and filter, else the predicted set's enabled segments written from the structure in the order of its chunks.
    // `drawn` is the segments drawn, `predicted` the predicted set's enabled segments without the cap.
    bool draw_first_occluders(const ViewState &state, const OcclusionInputs &inputs, const OcclusionViewParams &params,
                              size_t &drawn, size_t &predicted, StepTimes &times, std::string &reason);
    // Draws the first `count` segments of the list on top of the depth target
    bool draw_occluders(const GpuList &list, size_t count, const OcclusionViewParams &params, std::string &reason);
    void build_pyramid(int width, int height, int last_level);
    // The chunk and sub-cell passes, and their readbacks into m_chunk_readback and m_readback
    bool draw_tests(const OcclusionViewParams &params, const float view_proj[16], float orientation, int last_level,
                    size_t chunks, size_t subcells, std::string &reason);
    bool read_results(size_t chunks, size_t subcells, std::string &reason);
    // With `want_residual`, writes state.residual when step 1 drew the whole predicted set: `residual_chunks` the
    // chunks its sub-cells lie in, `residual_reason` why none was written (empty when one was)
    bool run_step(ViewState &state, const OcclusionInputs &inputs, const OcclusionViewParams &params,
                  bool want_residual, size_t &residual_chunks, const char *&residual_reason, OcclusionViewStats &stats,
                  StepTimes &times);
    void release_targets();
    void release_list(GpuList &list);

    DrawDepth m_draw_depth;
    std::array<ViewState, OCCLUSION_VIEWS> m_views;
    size_t m_occluder_cap{0};

    // Programs and their uniforms
    bool m_programs_failed{false};
    std::string m_program_log;
    unsigned int m_copy_program{0};
    unsigned int m_reduce_program{0};
    unsigned int m_test_program{0};
    // Built with the others; 0 when it failed, which leaves the others working
    unsigned int m_merge_program{0};
    int m_uni_merge_depth_tex{-1};
    int m_uni_merge_viewport_origin{-1};
    int m_uni_copy_depth_tex{-1};
    int m_uni_reduce_source_tex{-1};
    int m_uni_reduce_source_size{-1};
    int m_uni_reduce_target_size{-1};
    struct TestUniforms
    {
        int view_proj{-1};
        int viewport_size{-1};
        int viewport_texels{-1};
        int orientation{-1};
        int last_level{-1};
        int taps{-1};
        int stage{-1};
        int result_width{-1};
        int result_size{-1};
        int chunk_flag_offset{-1};
        int min_clip_w{-1};
        int depth_tolerance{-1};
        int face_min_area_fraction{-1};
        int face_min_area{-1};
        int window_margin{-1};
        int unbounded{-1};
        int box_faces{-1};
        int pyramid_tex{-1};
        int chunk_result_tex{-1};
        int flags_tex{-1};
    };
    TestUniforms m_uni;

    // The structure's boxes and chunk indices, uploaded for the generations they were made under, with the vertex
    // arrays that read them and the fullscreen triangle's
    bool m_static_valid{false};
    uint64_t m_static_structure{0};
    uint64_t m_static_boxes{0};
    size_t m_static_bytes{0};
    unsigned int m_subcell_box_buf{0};
    unsigned int m_subcell_chunk_buf{0};
    unsigned int m_chunk_box_buf{0};
    unsigned int m_chunk_vao{0};
    unsigned int m_subcell_vao{0};
    unsigned int m_fullscreen_buf{0};
    unsigned int m_fullscreen_vao{0};
    // The largest texture buffer, in texels
    size_t m_max_texels{0};

    // The flags of the filter applied: a byte per sub-cell (it holds an enabled segment), then a byte per chunk (it
    // holds such a sub-cell); per chunk its flagged sub-cells, and the chunks that hold one
    bool m_flags_valid{false};
    uint64_t m_flags_structure{0};
    uint64_t m_flags_filter{0};
    unsigned int m_flags_buf{0};
    unsigned int m_flags_tex{0};
    std::vector<uint8_t> m_flag_bytes;
    std::vector<uint32_t> m_chunk_flagged;
    size_t m_flagged_chunks{0};

    // The depth target and the pyramid (grown to the largest view, a view uses their lower left part), the result
    // targets (rows grown to the counts)
    int m_target_width{0};
    int m_target_height{0};
    int m_target_levels{0};
    unsigned int m_depth_tex{0};
    unsigned int m_depth_fbo{0};
    unsigned int m_pyramid_tex{0};
    unsigned int m_pyramid_fbo{0};
    int m_chunk_rows{0};
    int m_subcell_rows{0};
    unsigned int m_chunk_result_tex{0};
    unsigned int m_chunk_result_fbo{0};
    unsigned int m_subcell_result_tex{0};
    unsigned int m_subcell_result_fbo{0};
    // The view whose last step's depth the depth target holds (OCCLUSION_VIEWS: none) and that step's viewport size:
    // set when a step succeeds, cleared when a step, a cross check or a reallocation of the target starts, and by any
    // other request of that view
    size_t m_depth_owner{OCCLUSION_VIEWS};
    int m_depth_width{0};
    int m_depth_height{0};

    // The occluder list of a round
    GpuList m_occluder_list;

    // Scratch, reused from step to step: the readbacks, the sub-cells drawn as occluders once a round added some (then
    // the residual's sub-cells), the slab's candidates, the chunks of a draw set in the making and their distances, and
    // a list built in memory when its buffer could not be mapped
    std::vector<uint8_t> m_readback;
    std::vector<uint8_t> m_chunk_readback;
    std::vector<uint8_t> m_drawn;
    std::vector<SlabCandidate> m_candidates;
    std::vector<uint32_t> m_chunk_order;
    std::vector<ChunkDistance> m_chunk_distances;
    std::vector<uint32_t> m_list_scratch;
    // The step's lists that took the copy because the buffer could not be mapped, and why the last one did
    size_t m_map_fallbacks{0};
    const char *m_map_fallback_reason{""};
};

} // namespace libvgcode
