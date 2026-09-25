///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

#include "luminary/mesh/core/TriangleMesh.hpp"
#include "luminary/gizmo/csg/CSGMesh.hpp"

namespace Luminary
{
class ModelObject;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

// Manages real-time boolean subtraction preview for objects with negative volumes.
// Computes CGAL booleans in a background thread and caches results.
class CSGPreviewManager
{
public:
    CSGPreviewManager();
    ~CSGPreviewManager();

    // Queue a boolean computation for an object that has negative volumes
    void request_update(const ModelObject &obj, int obj_idx);

    // Invalidate the cached preview for an object
    void invalidate(int obj_idx);

    // Invalidate all cached previews
    void invalidate_all();

    // Check for completed background computations. Returns true if new results available.
    bool process_completed();

    bool has_preview(int obj_idx) const;
    const TriangleMesh *get_preview_mesh(int obj_idx) const;
    bool is_computing(int obj_idx) const;

    // Returns true if a previously valid preview was cleared (object lost its negative volumes).
    // The caller should force a full scene rebuild to restore original meshes.
    bool needs_scene_rebuild() const { return m_needs_rebuild; }
    void clear_rebuild_flag() { m_needs_rebuild = false; }

private:
    struct CacheEntry
    {
        TriangleMesh result_mesh;
        bool valid{false};
        bool computing{false};
        size_t generation{0};
    };

    struct CompletedResult
    {
        int obj_idx;
        TriangleMesh mesh;
        size_t generation;
        bool failed{false};
    };

    // Shared state that background threads write to. Kept alive via shared_ptr
    // so threads can safely finish even after the manager is destroyed.
    struct SharedState
    {
        std::mutex mutex;
        std::vector<CompletedResult> completed;
    };

    std::map<int, CacheEntry> m_cache;
    size_t m_current_generation{0};
    std::shared_ptr<SharedState> m_shared;
    bool m_needs_rebuild{false};
};

} // namespace DSKY

