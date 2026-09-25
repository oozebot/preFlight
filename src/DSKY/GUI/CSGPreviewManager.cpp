///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "CSGPreviewManager.hpp"
#include "NotificationManager.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

#include "luminary/model/scene/Model.hpp"
#include "luminary/gizmo/csg/ModelToCSGMesh.hpp"
#include "luminary/gizmo/csg/PerformCSGMeshBooleans.hpp"
#include "luminary/gizmo/boolean/MeshBoolean.hpp"

namespace DSKY
{
using namespace Luminary;

CSGPreviewManager::CSGPreviewManager() : m_shared(std::make_shared<SharedState>()) {}

CSGPreviewManager::~CSGPreviewManager()
{
    // SharedState is ref-counted - background threads that are still running
    // hold their own shared_ptr, so they won't write to freed memory.
}

void CSGPreviewManager::request_update(const ModelObject &obj, int obj_idx)
{
    bool has_negative = false;
    for (const ModelVolume *v : obj.volumes)
    {
        if (v != nullptr && v->is_negative_volume())
        {
            has_negative = true;
            break;
        }
    }

    if (!has_negative)
    {
        auto it = m_cache.find(obj_idx);
        if (it != m_cache.end())
        {
            if (it->second.valid && !it->second.result_mesh.empty())
                m_needs_rebuild = true;
            m_cache.erase(it);
        }
        return;
    }

    auto &entry = m_cache[obj_idx];

    if (entry.computing || entry.valid)
        return;

    entry.computing = true;
    entry.generation = ++m_current_generation;

    // Build CSG mesh data on the main thread (copies triangle data)
    auto csg_parts = std::make_shared<std::vector<csg::CSGPart>>();
    csg_parts->reserve(2 * obj.volumes.size());
    csg::model_to_csgmesh(obj, Transform3d::Identity(), std::back_inserter(*csg_parts),
                          csg::mpartsPositive | csg::mpartsNegative | csg::mpartsDoSplits);

    size_t gen = entry.generation;

    // Capture shared_ptr to SharedState (not 'this') so the thread is safe
    // even if CSGPreviewManager is destroyed before the thread finishes
    std::shared_ptr<SharedState> shared = m_shared;

    std::thread(
        [shared, csg_parts, obj_idx, gen]()
        {
            TriangleMesh result;

            try
            {
                auto csgrange = range(*csg_parts);

                if (csg::is_all_positive(csgrange))
                {
                    result = TriangleMesh{csg::csgmesh_merge_positive_parts(csgrange)};
                }
                else
                {
                    // Use the igl boolean path (EPECK + winding numbers) instead of
                    // CGAL corefinement, which fails on dense meshes intersecting surfaces.
                    TriangleMesh merged;
                    for (auto &part : *csg_parts)
                    {
                        auto op = csg::get_operation(part);
                        const indexed_triangle_set *its = csg::get_mesh(part);
                        if (!its || its->indices.empty())
                            continue;

                        indexed_triangle_set transformed = *its;
                        its_transform(transformed, csg::get_transform(part), true);
                        TriangleMesh part_mesh(std::move(transformed));

                        if (merged.empty())
                        {
                            merged = std::move(part_mesh);
                        }
                        else if (op == csg::CSGType::Union)
                        {
                            MeshBoolean::plus(merged, part_mesh);
                        }
                        else if (op == csg::CSGType::Difference)
                        {
                            MeshBoolean::minus(merged, part_mesh);
                        }
                        else if (op == csg::CSGType::Intersection)
                        {
                            MeshBoolean::intersect(merged, part_mesh);
                        }
                    }
                    result = std::move(merged);
                }
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->completed.push_back({obj_idx, std::move(result), gen, true});
                // Wake the main thread so render() picks up the result
                wxTheApp->CallAfter([]() { wxGetApp().plater()->canvas3D()->set_as_dirty(); });
                return;
            }

            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->completed.push_back({obj_idx, std::move(result), gen, false});
            }
            // Wake the main thread so render() picks up the result
            wxTheApp->CallAfter([]() { wxGetApp().plater()->canvas3D()->set_as_dirty(); });
        })
        .detach();
}

void CSGPreviewManager::invalidate(int obj_idx)
{
    m_cache.erase(obj_idx);
    ++m_current_generation;
}

void CSGPreviewManager::invalidate_all()
{
    ++m_current_generation;
    m_cache.clear();
}

bool CSGPreviewManager::process_completed()
{
    std::vector<CompletedResult> results;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        results.swap(m_shared->completed);
    }

    bool any_new = false;
    bool any_failed = false;
    for (auto &result : results)
    {
        auto &entry = m_cache[result.obj_idx];

        if (result.generation != entry.generation)
            continue;

        entry.result_mesh = std::move(result.mesh);
        // Mark as valid even when empty to prevent infinite re-computation of failed booleans
        entry.valid = true;
        entry.computing = false;
        any_new = true;
        if (result.failed)
            any_failed = true;
    }

    if (any_failed)
    {
        auto *notif = wxGetApp().plater()->get_notification_manager();
        notif->push_notification(NotificationType::BooleanOperationFailed,
                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                                 _u8L("Boolean subtraction preview failed for one or more objects."));
    }

    return any_new;
}

bool CSGPreviewManager::has_preview(int obj_idx) const
{
    auto it = m_cache.find(obj_idx);
    return it != m_cache.end() && it->second.valid;
}

const TriangleMesh *CSGPreviewManager::get_preview_mesh(int obj_idx) const
{
    auto it = m_cache.find(obj_idx);
    if (it != m_cache.end() && it->second.valid)
        return &it->second.result_mesh;
    return nullptr;
}

bool CSGPreviewManager::is_computing(int obj_idx) const
{
    auto it = m_cache.find(obj_idx);
    return it != m_cache.end() && it->second.computing;
}

} // namespace DSKY
