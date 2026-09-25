///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GLGizmoAlign.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"
#include "DSKY/GUI/Camera.hpp"
#include "DSKY/GUI/I18N.hpp"
#include "DSKY/GUI/EventTypes.hpp"
#include "DSKY/GUI/EventBridge.hpp"
#include "DSKY/GUI/Gizmos/GLGizmosCommon.hpp"
#include "DSKY/GUI/Gizmos/GLGizmosManager.hpp"
#include "DSKY/GUI/Selection.hpp"
#include "DSKY/GUI/ImGuiPureWrap.hpp"
#include "DSKY/GUI/CSGPreviewManager.hpp"
#include "DSKY/GUI/NotificationManager.hpp"

#include "luminary/model/scene/Model.hpp"
#include "luminary/gizmo/boolean/MeshBoolean.hpp"
#include "luminary/mesh/core/TriangleMesh.hpp"

#include <unordered_map>

#include <boost/log/trivial.hpp>

#include <imgui/imgui.h>

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

namespace DSKY
{
using namespace Luminary;

// Named constants for alignment behavior
static constexpr double SURFACE_EPSILON = 0.0005;      // Half-micron overshoot to prevent coplanar z-fighting
static constexpr double EDGE_EPSILON = 0.0001;         // Micron overshoot past edges for snap alignment
static constexpr double RAY_PLANE_PARALLEL_TOL = 1e-8; // Tolerance for ray-plane parallel detection
static constexpr int FACE_SWITCH_DELAY_MS = 500;       // Debounce delay for snap point face switching
static constexpr double DRAG_SNAP_RADIUS_PX = 15.0;    // Screen-pixel radius for drag snap-to-point

GLGizmoAlign::GLGizmoAlign(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

bool GLGizmoAlign::on_init()
{
    m_shortcut_key = WXK_CONTROL_G;
    return true;
}

std::string GLGizmoAlign::on_get_name() const
{
    return _u8L("Align to face");
}

bool GLGizmoAlign::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return !selection.is_empty();
}

CommonGizmosDataID GLGizmoAlign::on_get_requirements() const
{
    return CommonGizmosDataID::SelectionInfo;
}

void GLGizmoAlign::on_set_state()
{
    if (GLGizmoBase::m_state == GLGizmoBase::On)
    {
        // Remember the selected object as the target (what we align TO)
        const Selection &selection = m_parent.get_selection();
        m_target_object_idx = selection.get_object_idx();

        // Reset popup measurement so it re-measures for the current state
        m_popup_render_count = 0;
        m_popup_height = 0.f;
    }

    if (GLGizmoBase::m_state == GLGizmoBase::Off)
    {
        // If accept_as_volume is in progress, it handles all cleanup itself
        if (m_accept_in_progress)
        {
            reset_state();
            return;
        }

        if (m_align_state == EState::Aligned && m_source.object_idx >= 0)
        {
            const Model &model = *m_parent.get_selection().get_model();

            // Validate indices before using them - objects may have been deleted while gizmo was open
            if (m_source.object_idx < static_cast<int>(model.objects.size()))
            {
                ModelObject *obj = model.objects[m_source.object_idx];
                if (obj != nullptr && m_source.instance_idx < static_cast<int>(obj->instances.size()))
                {
                    m_parent.event_poster()->postEvent(CanvasEventType::TakeSnapshot, _u8L("Align to Face"));
                    obj->invalidate_bounding_box();
                    m_parent.event_poster()->postEvent(CanvasEventType::InstanceMoved);
                }
            }
        }
        reset_state();
        m_parent.set_as_dirty();
    }
}

void GLGizmoAlign::data_changed(bool is_serializing)
{
    // Don't close when selection changes - we need to allow clicking different objects
}

void GLGizmoAlign::build_plane_axes(const Vec3d &normal, Vec3d &x_axis, Vec3d &y_axis)
{
    Vec3d z = normal.normalized();
    double z_dot = z.dot(Vec3d::UnitZ());

    if (std::abs(z_dot) > 0.9)
    {
        // Horizontal face (top or bottom): X = world right, Y = world forward/back
        x_axis = Vec3d::UnitX();
        y_axis = z.cross(x_axis).normalized();
        // For bottom face, flip Y so "up" in the UI stays consistent
        if (z_dot < 0)
            y_axis = -y_axis;
        x_axis = y_axis.cross(z).normalized();
    }
    else
    {
        // Vertical or angled face: Y points upward (toward +Z), X is horizontal
        x_axis = Vec3d::UnitZ().cross(z).normalized();
        y_axis = z.cross(x_axis).normalized();
        // Ensure Y has a positive Z component (points upward)
        if (y_axis.z() < 0)
        {
            y_axis = -y_axis;
            x_axis = -x_axis;
        }
    }
}

// Compute rotation matrix that rotates vector 'from' to vector 'to'
static Transform3d rotation_from_to(const Vec3d &from, const Vec3d &to)
{
    // Eigen's quaternion handles all edge cases (parallel, anti-parallel, arbitrary)
    Transform3d result = Transform3d::Identity();
    result.linear() = Eigen::Quaterniond().setFromTwoVectors(from.normalized(), to.normalized()).toRotationMatrix();
    return result;
}

static bool is_relief_object(const ModelObject *obj)
{
    static const std::string prefix = "[Relief] ";
    return obj && obj->name.size() >= prefix.size() && obj->name.compare(0, prefix.size(), prefix) == 0;
}

bool GLGizmoAlign::raycast_face(const Vec2d &mouse_pos, FaceData &face_out)
{
    const Camera &camera = m_parent.get_camera();
    const Selection &selection = m_parent.get_selection();
    const Model &model = *selection.get_model();

    double closest_dist_sq = std::numeric_limits<double>::max();
    bool hit_found = false;

    for (size_t obj_idx = 0; obj_idx < model.objects.size(); ++obj_idx)
    {
        const ModelObject *obj = model.objects[obj_idx];
        if (obj == nullptr)
            continue;

        for (size_t inst_idx = 0; inst_idx < obj->instances.size(); ++inst_idx)
        {
            const ModelInstance *inst = obj->instances[inst_idx];
            if (inst == nullptr || !inst->printable)
                continue;

            Transform3d inst_trafo = inst->get_matrix();

            for (size_t vol_idx = 0; vol_idx < obj->volumes.size(); ++vol_idx)
            {
                const ModelVolume *vol = obj->volumes[vol_idx];
                if (vol == nullptr)
                    continue;

                // For relief objects, ONLY raycast against the "Alignment Box" modifier
                // (skip the actual relief mesh - it has too many triangles and no flat faces).
                // For normal objects, only raycast against model parts.
                bool is_relief = is_relief_object(obj);
                if (is_relief)
                {
                    if (!vol->is_modifier() || vol->name != "Alignment Box")
                        continue;
                }
                else
                {
                    if (!vol->is_model_part())
                        continue;
                }

                Transform3d world_trafo = inst_trafo * vol->get_matrix();

                auto mesh = std::make_shared<const TriangleMesh>(vol->mesh());
                MeshRaycaster raycaster(mesh);

                // For bbox-derived hits, compute the bounding box for center snapping
                BoundingBoxf3 vol_bb;
                if (is_relief)
                    vol_bb = vol->mesh().bounding_box();

                Vec3f hit_pos;
                Vec3f hit_normal;
                size_t facet_idx;

                if (raycaster.unproject_on_mesh(mouse_pos, world_trafo, camera, hit_pos, hit_normal, nullptr,
                                                &facet_idx))
                {
                    Vec3d world_hit = world_trafo * hit_pos.cast<double>();
                    Vec3d cam_pos = camera.get_position();
                    double dist_sq = (world_hit - cam_pos).squaredNorm();

                    if (dist_sq < closest_dist_sq)
                    {
                        closest_dist_sq = dist_sq;
                        hit_found = true;

                        Matrix3d normal_matrix = world_trafo.linear().inverse().transpose();
                        Vec3d world_normal = (normal_matrix * hit_normal.cast<double>()).normalized();

                        face_out.normal = world_normal;
                        face_out.object_idx = static_cast<int>(obj_idx);
                        face_out.instance_idx = static_cast<int>(inst_idx);
                        face_out.volume_idx = static_cast<int>(vol_idx);
                        face_out.facet_idx = is_relief ? SIZE_MAX : facet_idx;

                        if (is_relief)
                        {
                            // Snap center to the geometric center of the hit bounding box face
                            Vec3d bb_center = vol_bb.center();
                            Vec3f n = hit_normal.normalized();
                            Vec3d local_center;
                            if (std::abs(n.z()) > 0.5f)
                                local_center = Vec3d(bb_center.x(), bb_center.y(),
                                                     n.z() > 0 ? vol_bb.max.z() : vol_bb.min.z());
                            else if (std::abs(n.y()) > 0.5f)
                                local_center = Vec3d(bb_center.x(), n.y() > 0 ? vol_bb.max.y() : vol_bb.min.y(),
                                                     bb_center.z());
                            else
                                local_center = Vec3d(n.x() > 0 ? vol_bb.max.x() : vol_bb.min.x(), bb_center.y(),
                                                     bb_center.z());
                            face_out.center = world_trafo * local_center;
                        }
                        else
                        {
                            face_out.center = world_hit;
                        }
                    }
                }
            }
        }
    }

    return hit_found;
}

void GLGizmoAlign::apply_alignment()
{
    if (m_source.object_idx < 0 || m_target.object_idx < 0)
        return;

    // Always align source face opposing target face. Flip is handled later
    // as a through-plane mirror, which preserves in-plane orientation for any face.
    Vec3d desired_normal = -m_target.normal;

    // Compute rotation that aligns source normal to desired direction
    Transform3d align_rot = rotation_from_to(m_source.normal, desired_normal);

    // Apply alignment rotation to the original transform's linear part directly
    Transform3d orig_trafo = m_original_transform;
    Matrix3d new_linear = align_rot.linear() * orig_trafo.linear();

    // Build rotated transform at original position
    Transform3d rotated_trafo = Transform3d::Identity();
    rotated_trafo.linear() = new_linear;
    rotated_trafo.translation() = orig_trafo.translation();

    // Target plane coordinate system
    Vec3d z_axis = m_target.normal.normalized();
    Vec3d x_axis, y_axis;
    build_plane_axes(m_target.normal, x_axis, y_axis);

    // The reference point on the source: the exact click point on the source surface
    Vec3d src_center_local = orig_trafo.inverse() * m_source.center;
    Vec3d rotated_src_center = rotated_trafo * src_center_local;

    Vec3d target_ref = m_target.center;

    // Depth slider value is used directly as a signed offset along the target normal.
    // Not flipped: slider range is negative (embedding into the target surface).
    // Flipped: slider range is positive (embedding from the other side).
    // A small epsilon prevents coplanar z-fighting at depth 0.
    double effective_depth = static_cast<double>(m_depth_offset) + SURFACE_EPSILON;

    Vec3d target_with_offset = target_ref + z_axis * effective_depth + x_axis * m_plane_offset.x() +
                               y_axis * m_plane_offset.y();

    Vec3d translation_fix = target_with_offset - rotated_src_center;

    // Build final transform
    Transform3d new_trafo = Transform3d::Identity();
    new_trafo.linear() = new_linear;
    new_trafo.translation() = orig_trafo.translation() + translation_fix;

    // Apply plane rotation around the target point
    if (std::abs(m_rotation) > 0.001)
    {
        Eigen::AngleAxisd plane_rotation(m_rotation * M_PI / 180.0, m_target.normal);
        new_trafo = Eigen::Translation3d(target_with_offset) * plane_rotation *
                    Eigen::Translation3d(-target_with_offset) * new_trafo;
    }

    // Apply mirror in the target plane's coordinate system (around the click point).
    // Flip is a through-plane (z-axis) mirror - preserves in-plane orientation on any face.
    if (m_mirror_h || m_mirror_v || m_inverted)
    {
        double mx = m_mirror_h ? -1.0 : 1.0;
        double my = m_mirror_v ? -1.0 : 1.0;
        double mz = m_inverted ? -1.0 : 1.0;

        // Construct world-space mirror: translate to target point, apply mirror in
        // the plane's local axes, translate back
        Matrix3d plane_basis;
        plane_basis.col(0) = x_axis;
        plane_basis.col(1) = y_axis;
        plane_basis.col(2) = z_axis;

        Matrix3d local_mirror = Eigen::Scaling(mx, my, mz);
        Matrix3d world_mirror = plane_basis * local_mirror * plane_basis.transpose();

        Transform3d mirror_trafo = Eigen::Translation3d(target_with_offset) * Transform3d(world_mirror) *
                                   Eigen::Translation3d(-target_with_offset);
        new_trafo = mirror_trafo * new_trafo;
    }

    // Apply per-axis scale in the target plane's coordinate system
    if (std::abs(m_scale_x - 100.f) > 0.01f || std::abs(m_scale_y - 100.f) > 0.01f ||
        std::abs(m_scale_z - 100.f) > 0.01f)
    {
        double sx = static_cast<double>(m_scale_x) / 100.0;
        double sy = static_cast<double>(m_scale_y) / 100.0;
        double sz = static_cast<double>(m_scale_z) / 100.0;

        // Build scale in the target plane's local frame:
        // X = plane horizontal, Y = plane vertical, Z = normal
        Matrix3d plane_basis;
        plane_basis.col(0) = x_axis;
        plane_basis.col(1) = y_axis;
        plane_basis.col(2) = z_axis;

        Matrix3d local_scale = Eigen::Scaling(sx, sy, sz);
        Matrix3d world_scale = plane_basis * local_scale * plane_basis.transpose();

        Transform3d scale_trafo = Eigen::Translation3d(target_with_offset) * Transform3d(world_scale) *
                                  Eigen::Translation3d(-target_with_offset);
        new_trafo = scale_trafo * new_trafo;
    }

    // Flip is handled earlier - it changes the alignment rotation direction

    // Set the raw matrix directly to avoid Euler angle decomposition precision loss
    Geometry::Transformation new_transformation;
    new_transformation.set_matrix(new_trafo);

    // Update GLVolumes (the visual)
    const GLVolumePtrs &volumes = m_parent.get_volumes().volumes;
    for (GLVolume *v : volumes)
    {
        if (v == nullptr)
            continue;
        if (v->object_idx() == m_source.object_idx && v->instance_idx() == m_source.instance_idx)
        {
            v->set_instance_transformation(new_transformation);
        }
    }

    // Update ModelInstance (the model data)
    const Model &model = *m_parent.get_selection().get_model();
    if (m_source.object_idx < static_cast<int>(model.objects.size()))
    {
        ModelObject *obj = model.objects[m_source.object_idx];
        if (obj != nullptr && m_source.instance_idx < static_cast<int>(obj->instances.size()))
            obj->instances[m_source.instance_idx]->set_transformation(new_transformation);
    }

    m_parent.event_poster()->postEvent(CanvasEventType::ManipulationDirty);
    m_parent.set_as_dirty();
}

void GLGizmoAlign::compute_original_size()
{
    m_orig_size = Vec3d::Zero();
    if (m_source.object_idx < 0)
        return;

    const Model &model = *m_parent.get_selection().get_model();
    if (m_source.object_idx >= static_cast<int>(model.objects.size()))
        return;

    const ModelObject *obj = model.objects[m_source.object_idx];
    if (obj == nullptr)
        return;

    // Compute alignment rotation (same as apply_alignment)
    Vec3d desired_normal = -m_target.normal;
    Transform3d align_rot = rotation_from_to(m_source.normal, desired_normal);

    // Target plane coordinate system
    Vec3d z_axis = m_target.normal.normalized();
    Vec3d x_axis, y_axis;
    build_plane_axes(m_target.normal, x_axis, y_axis);

    // Get the object's bounding box, transform by the alignment rotation + original transform
    Transform3d orig_trafo = m_original_transform;
    Matrix3d new_linear = align_rot.linear() * orig_trafo.linear();

    // Project the bounding box onto the target plane axes
    double min_x = std::numeric_limits<double>::max(), max_x = -std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max(), max_y = -std::numeric_limits<double>::max();
    double min_z = std::numeric_limits<double>::max(), max_z = -std::numeric_limits<double>::max();

    for (const ModelVolume *vol : obj->volumes)
    {
        if (vol == nullptr || !vol->is_model_part())
            continue;
        BoundingBoxf3 bb = vol->mesh().bounding_box();
        Matrix3d vol_linear = new_linear * vol->get_matrix().linear();

        for (int i = 0; i < 8; ++i)
        {
            Vec3d corner((i & 1) ? bb.max.x() : bb.min.x(), (i & 2) ? bb.max.y() : bb.min.y(),
                         (i & 4) ? bb.max.z() : bb.min.z());
            Vec3d world_dir = vol_linear * corner;
            double px = world_dir.dot(x_axis);
            double py = world_dir.dot(y_axis);
            double pz = world_dir.dot(z_axis);
            min_x = std::min(min_x, px);
            max_x = std::max(max_x, px);
            min_y = std::min(min_y, py);
            max_y = std::max(max_y, py);
            min_z = std::min(min_z, pz);
            max_z = std::max(max_z, pz);
        }
    }

    m_orig_size = Vec3d(max_x - min_x, max_y - min_y, max_z - min_z);
}

void GLGizmoAlign::snap_align(int align_x, int align_y)
{
    // align_x/y: -1 = min edge, 0 = center, 1 = max edge, -2 = no change
    if (m_source.object_idx < 0 || m_target.object_idx < 0)
        return;

    Vec3d z_axis = m_target.normal.normalized();
    Vec3d x_axis, y_axis;
    build_plane_axes(m_target.normal, x_axis, y_axis);

    const Model &model = *m_parent.get_selection().get_model();

    // Helper to compute an object's bounding extent projected onto the target plane
    auto compute_plane_extents = [&](int obj_idx, const Transform3d &inst_trafo, double &out_min_x, double &out_max_x,
                                     double &out_min_y, double &out_max_y)
    {
        out_min_x = out_min_y = std::numeric_limits<double>::max();
        out_max_x = out_max_y = -std::numeric_limits<double>::max();
        if (obj_idx >= static_cast<int>(model.objects.size()))
            return;
        const ModelObject *obj = model.objects[obj_idx];
        if (obj == nullptr)
            return;
        for (const ModelVolume *vol : obj->volumes)
        {
            if (vol == nullptr || !vol->is_model_part())
                continue;
            BoundingBoxf3 bb = vol->mesh().bounding_box();
            Transform3d vol_world = inst_trafo * vol->get_matrix();
            for (int i = 0; i < 8; ++i)
            {
                Vec3d corner((i & 1) ? bb.max.x() : bb.min.x(), (i & 2) ? bb.max.y() : bb.min.y(),
                             (i & 4) ? bb.max.z() : bb.min.z());
                Vec3d wc = vol_world * corner;
                out_min_x = std::min(out_min_x, wc.dot(x_axis));
                out_max_x = std::max(out_max_x, wc.dot(x_axis));
                out_min_y = std::min(out_min_y, wc.dot(y_axis));
                out_max_y = std::max(out_max_y, wc.dot(y_axis));
            }
        }
    };

    // Step 1: Reset offset to zero and apply to get the "base" aligned position.
    // Save current offset so we can preserve non-aligned axes.
    Vec2d saved_offset = m_plane_offset;
    m_plane_offset = Vec2d::Zero();
    apply_alignment();

    // Step 2: Read the source's ACTUAL current position from the GLVolumes
    // (this reflects the base alignment with offset=0)
    const Model &model2 = *m_parent.get_selection().get_model();
    Transform3d src_current = Transform3d::Identity();
    if (m_source.object_idx < static_cast<int>(model2.objects.size()))
    {
        const ModelObject *obj = model2.objects[m_source.object_idx];
        if (obj != nullptr && m_source.instance_idx < static_cast<int>(obj->instances.size()))
            src_current = obj->instances[m_source.instance_idx]->get_matrix();
    }

    // Source extents at base position (offset=0)
    double src_min_x, src_max_x, src_min_y, src_max_y;
    compute_plane_extents(m_source.object_idx, src_current, src_min_x, src_max_x, src_min_y, src_max_y);

    // Target extents
    double tgt_min_x = 0, tgt_max_x = 0, tgt_min_y = 0, tgt_max_y = 0;
    if (m_target.object_idx < static_cast<int>(model2.objects.size()))
    {
        const ModelObject *tgt_obj = model2.objects[m_target.object_idx];
        if (tgt_obj != nullptr && m_target.instance_idx < static_cast<int>(tgt_obj->instances.size()))
        {
            Transform3d tgt_trafo = tgt_obj->instances[m_target.instance_idx]->get_matrix();
            compute_plane_extents(m_target.object_idx, tgt_trafo, tgt_min_x, tgt_max_x, tgt_min_y, tgt_max_y);
        }
    }

    double src_cx = (src_min_x + src_max_x) * 0.5;
    double src_cy = (src_min_y + src_max_y) * 0.5;
    double src_w = src_max_x - src_min_x;
    double src_h = src_max_y - src_min_y;

    // For non-aligned axes (-2), preserve the saved offset
    double offset_x = (align_x == -2) ? saved_offset.x() : 0.0;
    double offset_y = (align_y == -2) ? saved_offset.y() : 0.0;

    // X alignment (with epsilon overshoot past edges to prevent coplanar boolean artifacts)
    if (align_x == -1) // Left: source left edge past target left edge
        offset_x = tgt_min_x - (src_cx - src_w * 0.5) - EDGE_EPSILON;
    else if (align_x == 0) // Center: no overshoot needed
        offset_x = (tgt_min_x + tgt_max_x) * 0.5 - src_cx;
    else if (align_x == 1) // Right: source right edge past target right edge
        offset_x = tgt_max_x - (src_cx + src_w * 0.5) + EDGE_EPSILON;

    // Y alignment (with epsilon overshoot past edges)
    if (align_y == -1) // Bottom: source bottom edge past target bottom edge
        offset_y = tgt_min_y - (src_cy - src_h * 0.5) - EDGE_EPSILON;
    else if (align_y == 0) // Center/Middle: no overshoot needed
        offset_y = (tgt_min_y + tgt_max_y) * 0.5 - src_cy;
    else if (align_y == 1) // Top: source top edge past target top edge
        offset_y = tgt_max_y - (src_cy + src_h * 0.5) + EDGE_EPSILON;

    // Step 3: Set the final offset and apply
    m_plane_offset = Vec2d(offset_x, offset_y);
    apply_alignment();
}

void GLGizmoAlign::accept_as_volume(ModelVolumeType volume_type, const std::string &snapshot_name)
{
    if (m_source.object_idx < 0 || m_target.object_idx < 0)
        return;

    if (m_source.object_idx == m_target.object_idx)
        return;

    m_parent.event_poster()->postEvent(CanvasEventType::TakeSnapshot, I18N::translate_utf8(snapshot_name));

    Model &model = *m_parent.get_model();

    if (m_source.object_idx >= static_cast<int>(model.objects.size()) ||
        m_target.object_idx >= static_cast<int>(model.objects.size()))
        return;

    wxBeginBusyCursor();

    try
    {
        ModelObject *src_obj = model.objects[m_source.object_idx];
        ModelObject *tgt_obj = model.objects[m_target.object_idx];

        if (src_obj == nullptr || tgt_obj == nullptr)
        {
            wxEndBusyCursor();
            return;
        }
        if (m_source.instance_idx >= static_cast<int>(src_obj->instances.size()) ||
            m_target.instance_idx >= static_cast<int>(tgt_obj->instances.size()))
        {
            wxEndBusyCursor();
            return;
        }

        // Strip Alignment Box modifiers from the source before boolean operations.
        // The source is deleted after accept, so this is safe.
        for (int vi = static_cast<int>(src_obj->volumes.size()) - 1; vi >= 0; --vi)
        {
            ModelVolume *v = src_obj->volumes[vi];
            if (v != nullptr && v->is_modifier() && v->name == "Alignment Box")
                src_obj->delete_volume(vi);
        }

        // If source has multiple instances, split the aligned instance into its own
        // standalone object first. This prevents modifying the shared object from
        // affecting the remaining instances (color, transform, etc.)
        bool was_multi_instance = (src_obj->instances.size() > 1);
        if (was_multi_instance)
        {
            // Clone the source object and keep only the aligned instance
            ModelObject *standalone = model.add_object(*src_obj);
            // Remove all instances except the one matching our aligned instance's transform
            Transform3d aligned_transform = src_obj->instances[m_source.instance_idx]->get_matrix();
            standalone->clear_instances();
            standalone->add_instance(Geometry::Transformation(aligned_transform));

            // Remove the aligned instance from the original object
            src_obj->delete_instance(m_source.instance_idx);
            src_obj->invalidate_bounding_box();

            // Update references to point to the new standalone object
            src_obj = standalone;
            m_source.object_idx = static_cast<int>(model.objects.size()) - 1;
            m_source.instance_idx = 0;

            // Target index hasn't changed since we only appended
        }

        ModelInstance *src_inst = src_obj->instances[m_source.instance_idx];
        ModelInstance *tgt_inst = tgt_obj->instances[m_target.instance_idx];

        // Relief objects: skip immediate CGAL boolean - the slicer handles overlapping
        // volumes (MODEL_PART for weld, NEGATIVE_VOLUME for subtract) at slice time.
        // CGAL's EPIC kernel has numerical precision issues with dense heightmap meshes.
        bool is_relief_src = is_relief_object(src_obj);

        if (volume_type == ModelVolumeType::MODEL_PART && !is_relief_src)
        {
            // True weld: boolean union of source meshes into the target's first model part.
            ModelVolume *tgt_vol = nullptr;
            for (ModelVolume *v : tgt_obj->volumes)
            {
                if (v != nullptr && v->is_model_part())
                {
                    tgt_vol = v;
                    break;
                }
            }

            if (tgt_vol == nullptr)
            {
                auto *notif = m_parent.get_notification_manager();
                notif->push_notification(NotificationType::BooleanOperationFailed,
                                         NotificationManager::NotificationLevel::WarningNotificationLevel,
                                         std::string("Target object has no solid part to weld into."));
                wxEndBusyCursor();
                return;
            }

            {
                Transform3d tgt_world = tgt_inst->get_matrix() * tgt_vol->get_matrix();
                Transform3d tgt_world_inv = tgt_world.inverse();

                for (const ModelVolume *src_vol : src_obj->volumes)
                {
                    if (src_vol == nullptr || !src_vol->is_model_part())
                        continue;

                    Transform3d src_to_tgt_local = tgt_world_inv * src_inst->get_matrix() * src_vol->get_matrix();

                    TriangleMesh src_mesh = src_vol->mesh();
                    src_mesh.transform(src_to_tgt_local, true);

                    try
                    {
                        if (MeshBoolean::cgal::does_self_intersect(src_mesh))
                            MeshBoolean::self_union(src_mesh);

                        TriangleMesh tgt_mesh = tgt_vol->mesh();
                        if (MeshBoolean::cgal::does_self_intersect(tgt_mesh))
                            MeshBoolean::self_union(tgt_mesh);

                        MeshBoolean::cgal::plus(tgt_mesh, src_mesh);

                        tgt_vol->set_mesh(std::move(tgt_mesh));
                        tgt_vol->calculate_convex_hull();
                        tgt_vol->set_new_unique_id();
                    }
                    catch (const std::exception &e)
                    {
                        BOOST_LOG_TRIVIAL(error) << "Boolean union exception: " << e.what();

                        // Fall back to adding as separate volume
                        Transform3d relative_trafo = tgt_inst->get_matrix().inverse() * src_inst->get_matrix() *
                                                     src_vol->get_matrix();
                        ModelVolume *new_vol = tgt_obj->add_volume(*src_vol, volume_type);
                        Geometry::Transformation vol_trafo;
                        vol_trafo.set_matrix(relative_trafo);
                        new_vol->set_transformation(vol_trafo);
                        new_vol->name = src_obj->name + " (welded)";

                        auto *notif = m_parent.get_notification_manager();
                        notif->push_notification(NotificationType::BooleanOperationFailed,
                                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                                                 std::string("Boolean union failed: ") + e.what());
                    }
                    catch (...)
                    {
                        BOOST_LOG_TRIVIAL(error) << "Boolean union: unknown exception";

                        Transform3d relative_trafo = tgt_inst->get_matrix().inverse() * src_inst->get_matrix() *
                                                     src_vol->get_matrix();
                        ModelVolume *new_vol = tgt_obj->add_volume(*src_vol, volume_type);
                        Geometry::Transformation vol_trafo;
                        vol_trafo.set_matrix(relative_trafo);
                        new_vol->set_transformation(vol_trafo);
                        new_vol->name = src_obj->name + " (welded)";

                        auto *notif = m_parent.get_notification_manager();
                        notif->push_notification(NotificationType::BooleanOperationFailed,
                                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                                                 std::string("Boolean union failed with unknown error."));
                    }
                }
            }
        }
        else if (volume_type == ModelVolumeType::NEGATIVE_VOLUME && !is_relief_src)
        {
            // Subtract: CGAL boolean difference baked into the target mesh (like weld uses union).
            // This produces an immediate visible result without relying on async CSG preview.
            ModelVolume *tgt_vol = nullptr;
            for (ModelVolume *v : tgt_obj->volumes)
            {
                if (v != nullptr && v->is_model_part())
                {
                    tgt_vol = v;
                    break;
                }
            }

            if (tgt_vol == nullptr)
            {
                auto *notif = m_parent.get_notification_manager();
                notif->push_notification(NotificationType::BooleanOperationFailed,
                                         NotificationManager::NotificationLevel::WarningNotificationLevel,
                                         std::string("Target object has no solid part to subtract from."));
                wxEndBusyCursor();
                return;
            }

            {
                Transform3d tgt_world = tgt_inst->get_matrix() * tgt_vol->get_matrix();
                Transform3d tgt_world_inv = tgt_world.inverse();

                for (const ModelVolume *src_vol : src_obj->volumes)
                {
                    if (src_vol == nullptr || !src_vol->is_model_part())
                        continue;

                    Transform3d src_to_tgt_local = tgt_world_inv * src_inst->get_matrix() * src_vol->get_matrix();

                    TriangleMesh src_mesh = src_vol->mesh();
                    src_mesh.transform(src_to_tgt_local, true);

                    try
                    {
                        if (MeshBoolean::cgal::does_self_intersect(src_mesh))
                            MeshBoolean::self_union(src_mesh);

                        TriangleMesh tgt_mesh = tgt_vol->mesh();
                        if (MeshBoolean::cgal::does_self_intersect(tgt_mesh))
                            MeshBoolean::self_union(tgt_mesh);

                        MeshBoolean::cgal::minus(tgt_mesh, src_mesh);

                        tgt_vol->set_mesh(std::move(tgt_mesh));
                        tgt_vol->calculate_convex_hull();
                        tgt_vol->set_new_unique_id();
                    }
                    catch (const std::exception &e)
                    {
                        // Fall back to adding as negative volume (resolved at slice time)
                        Transform3d relative_trafo = tgt_inst->get_matrix().inverse() * src_inst->get_matrix() *
                                                     src_vol->get_matrix();
                        ModelVolume *new_vol = tgt_obj->add_volume(*src_vol, ModelVolumeType::NEGATIVE_VOLUME);
                        Geometry::Transformation vol_trafo;
                        vol_trafo.set_matrix(relative_trafo);
                        new_vol->set_transformation(vol_trafo);
                        new_vol->name = src_obj->name + " (subtract)";

                        auto *notif = m_parent.get_notification_manager();
                        notif->push_notification(NotificationType::BooleanOperationFailed,
                                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                                                 std::string("Boolean subtract failed: ") + e.what());
                    }
                    catch (...)
                    {
                        Transform3d relative_trafo = tgt_inst->get_matrix().inverse() * src_inst->get_matrix() *
                                                     src_vol->get_matrix();
                        ModelVolume *new_vol = tgt_obj->add_volume(*src_vol, ModelVolumeType::NEGATIVE_VOLUME);
                        Geometry::Transformation vol_trafo;
                        vol_trafo.set_matrix(relative_trafo);
                        new_vol->set_transformation(vol_trafo);
                        new_vol->name = src_obj->name + " (subtract)";

                        auto *notif = m_parent.get_notification_manager();
                        notif->push_notification(NotificationType::BooleanOperationFailed,
                                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                                                 std::string("Boolean subtract failed with unknown error."));
                    }
                }
            }
        }
        else
        {
            // Relief objects (or CGAL-incompatible cases): add as overlapping volumes (resolved at slice time)
            for (const ModelVolume *src_vol : src_obj->volumes)
            {
                if (src_vol == nullptr || !src_vol->is_model_part())
                    continue;

                Transform3d relative_trafo = tgt_inst->get_matrix().inverse() * src_inst->get_matrix() *
                                             src_vol->get_matrix();

                ModelVolume *new_vol = tgt_obj->add_volume(*src_vol, volume_type);
                Geometry::Transformation vol_trafo;
                vol_trafo.set_matrix(relative_trafo);
                new_vol->set_transformation(vol_trafo);
                new_vol->name = src_obj->name +
                                (volume_type == ModelVolumeType::NEGATIVE_VOLUME ? " (subtract)" : " (welded)");
            }
        }

        tgt_obj->invalidate_bounding_box();

        // The source is now always a standalone single-instance object (either it was
        // originally, or we split it above). Delete the entire object.
        int src_idx = m_source.object_idx;
        reset_state();

        // Close the gizmo - the flag prevents on_set_state from doing redundant work
        m_accept_in_progress = true;
        auto &mng = m_parent.get_gizmos_manager();
        if (mng.get_current_type() == GLGizmosManager::Align)
            mng.reset_all_states();
        m_accept_in_progress = false;

        // Delete source, then invalidate CSG cache so reload_scene sees correct indices
        model.delete_object(src_idx);
        if (m_parent.get_csg_preview())
            m_parent.get_csg_preview()->invalidate_all();

        m_parent.event_poster()->postEvent(CanvasEventType::PlaterUpdate);
        m_parent.event_poster()->postEvent(CanvasEventType::ObjectListUpdateAfterUndoRedo);
        m_parent.set_as_dirty();
        wxEndBusyCursor();
    }
    catch (...)
    {
        // Safety net: ensure busy cursor is always released
        if (wxIsBusy())
            wxEndBusyCursor();
    }
}

bool GLGizmoAlign::intersect_target_plane(const Vec2d &mouse_pos, Vec3d &hit_point)
{
    const Camera &camera = m_parent.get_camera();
    Vec3d ray_origin, ray_dir;
    CameraUtils::ray_from_screen_pos(camera, mouse_pos, ray_origin, ray_dir);

    Vec3d plane_point = m_target.center;
    Vec3d plane_normal = m_target.normal;

    double denom = ray_dir.dot(plane_normal);
    if (std::abs(denom) < RAY_PLANE_PARALLEL_TOL)
        return false;

    double t = (plane_point - ray_origin).dot(plane_normal) / denom;
    if (t < 0)
        return false;

    hit_point = ray_origin + t * ray_dir;
    return true;
}

bool GLGizmoAlign::on_mouse(const MouseInput &mouse)
{
    if (m_state != On)
        return false;

    Vec2d mouse_pos(mouse.x, mouse.y);

    // When the popup panel is active, consume all mouse events that ImGui wants
    // (sliders, inputs, buttons) so they don't leak through to canvas rotation
    if (m_align_state == EState::Aligned && ImGui::GetIO().WantCaptureMouse)
        return true;

    if (mouse.type == MouseEventType::Motion &&
        (m_align_state == EState::Idle || m_align_state == EState::SourceSelected) &&
        (mouse_pos.x() != 0.0 || mouse_pos.y() != 0.0))
    {
        update_hover_snap_points(mouse_pos);
        m_parent.set_as_dirty();
        return false; // Don't consume - let canvas handle hover highlighting
    }

    if (mouse.type == MouseEventType::LeftDown)
    {
        if (m_align_state == EState::Idle)
        {
            // First click: pick SOURCE face (must be on a DIFFERENT object than the target)
            // If a snap point is hovered, use its position as the click point
            if (m_hovered_snap_idx >= 0 && m_has_hover)
            {
                FaceData snapped = m_hover_face;
                snapped.center = m_snap_points[m_hovered_snap_idx].position;

                if (snapped.object_idx == m_target_object_idx)
                    return true;

                m_source = snapped;
                const Model &model = *m_parent.get_selection().get_model();
                if (m_source.object_idx < static_cast<int>(model.objects.size()))
                {
                    ModelObject *obj = model.objects[m_source.object_idx];
                    if (obj != nullptr && m_source.instance_idx < static_cast<int>(obj->instances.size()))
                        m_original_transform = obj->instances[m_source.instance_idx]->get_matrix();
                }
                m_align_state = EState::SourceSelected;
                m_popup_render_count = 0;
                m_popup_height = 0.f;
                m_has_hover = false;
                m_snap_points.clear();
            }
            else
            {
                FaceData candidate;
                if (raycast_face(mouse_pos, candidate))
                {
                    if (candidate.object_idx == m_target_object_idx)
                        return true;

                    m_source = candidate;
                    const Model &model = *m_parent.get_selection().get_model();
                    if (m_source.object_idx < static_cast<int>(model.objects.size()))
                    {
                        ModelObject *obj = model.objects[m_source.object_idx];
                        if (obj != nullptr && m_source.instance_idx < static_cast<int>(obj->instances.size()))
                            m_original_transform = obj->instances[m_source.instance_idx]->get_matrix();
                    }
                    m_align_state = EState::SourceSelected;
                    m_popup_render_count = 0;
                    m_popup_height = 0.f;
                }
            }
            return true;
        }
        else if (m_align_state == EState::SourceSelected)
        {
            // Only accept clicks on the target object - reject everything else
            if (m_hovered_snap_idx >= 0 && m_has_hover)
            {
                FaceData snapped = m_hover_face;
                snapped.center = m_snap_points[m_hovered_snap_idx].position;

                if (snapped.object_idx != m_target_object_idx)
                    return true;

                m_target = snapped;
            }
            else
            {
                FaceData candidate;
                if (!raycast_face(mouse_pos, candidate) || candidate.object_idx != m_target_object_idx)
                    return true;

                m_target = candidate;
            }

            m_depth_offset = 0.f;
            m_rotation = 0.f;
            m_mirror_h = false;
            m_mirror_v = false;
            m_inverted = false;
            m_scale_x = 100.f;
            m_scale_y = 100.f;
            m_scale_z = 100.f;
            m_plane_offset = Vec2d::Zero();

            m_align_state = EState::Aligned;
            m_has_hover = false;
            m_snap_points.clear();
            m_popup_render_count = 0;
            m_popup_height = 0.f;
            compute_original_size();
            apply_alignment();
            return true;
        }
        else if (m_align_state == EState::Aligned)
        {
            if (mouse.shift)
            {
                Vec3d hit;
                if (intersect_target_plane(mouse_pos, hit))
                {
                    m_plane_dragging = true;
                    m_drag_last_hit = hit;

                    // Compute snap points on the target face for drag snapping
                    m_drag_snap_points.clear();
                    m_drag_snapped_idx = -1;
                    m_drag_raw_offset = m_plane_offset;
                    Vec3d face_center = compute_face_center(m_target);
                    m_drag_snap_points.push_back({face_center, m_target.normal, 0.f, false, false});
                    detect_feature_centers(m_target, m_drag_snap_points);
                }
            }
            // Consume clicks on the 3D canvas to prevent selection changes
            return true;
        }
    }

    if (mouse.dragging && mouse.left_down && m_align_state == EState::Aligned)
    {
        if (m_plane_dragging)
        {
            Vec3d hit;
            if (intersect_target_plane(mouse_pos, hit))
            {
                Vec3d delta = hit - m_drag_last_hit;

                Vec3d x_axis, y_axis;
                build_plane_axes(m_target.normal, x_axis, y_axis);

                // Accumulate on the raw (unsnapped) offset - this tracks the actual mouse
                m_drag_raw_offset.x() += delta.dot(x_axis);
                m_drag_raw_offset.y() += delta.dot(y_axis);
                m_drag_last_hit = hit;

                // Check snap: compare raw mouse position to snap points on screen
                m_drag_snapped_idx = -1;
                Vec3d raw_world = m_target.center + x_axis * m_drag_raw_offset.x() + y_axis * m_drag_raw_offset.y();
                Vec2d raw_screen = project_to_screen(raw_world);

                double best_dist = DRAG_SNAP_RADIUS_PX;
                int best_idx = -1;
                for (size_t i = 0; i < m_drag_snap_points.size(); ++i)
                {
                    Vec2d sp_screen = project_to_screen(m_drag_snap_points[i].position);
                    double dist = (sp_screen - raw_screen).norm();
                    if (dist < best_dist)
                    {
                        best_dist = dist;
                        best_idx = static_cast<int>(i);
                    }
                }

                if (best_idx >= 0)
                {
                    // Snap: display offset goes to snap point, raw offset stays with mouse
                    Vec3d snap_pos = m_drag_snap_points[best_idx].position;
                    m_plane_offset.x() = (snap_pos - m_target.center).dot(x_axis);
                    m_plane_offset.y() = (snap_pos - m_target.center).dot(y_axis);
                    m_drag_snapped_idx = best_idx;
                }
                else
                {
                    // No snap - display offset follows the raw mouse position
                    m_plane_offset = m_drag_raw_offset;
                }

                apply_alignment();
            }
            return true;
        }
        return false;
    }

    if (mouse.type == MouseEventType::LeftUp)
    {
        if (m_plane_dragging)
        {
            m_plane_dragging = false;
            m_drag_snap_points.clear();
            m_drag_snapped_idx = -1;
            return true;
        }
        // In Idle/SourceSelected, let the canvas handle LeftUp for selection
        if (m_align_state == EState::Aligned)
            return true;
        return false;
    }

    return false;
}

void GLGizmoAlign::on_render()
{
    if (m_align_state == EState::SourceSelected)
        render_face_indicator(m_source, ColorRGBA(0.2f, 0.8f, 0.2f, 0.7f));

    // Render snap point indicators during face selection
    if (m_has_hover && (m_align_state == EState::Idle || m_align_state == EState::SourceSelected))
    {
        for (size_t i = 0; i < m_snap_points.size(); ++i)
        {
            const auto &sp = m_snap_points[i];
            ColorRGBA color = sp.is_feature ? ColorRGBA(0.9f, 0.7f, 0.2f, 0.9f)  // Gold for features
                                            : ColorRGBA(1.0f, 0.6f, 0.0f, 0.9f); // Orange for face center

            if (sp.hovered)
                color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f); // White when hovered

            float size = sp.hovered ? 4.0f : 3.0f;
            render_snap_indicator(sp.position, sp.normal, color, size);

            // Draw snap ring around hovered snap points
            if (sp.hovered && m_snap_ring_model.is_initialized())
            {
                const Camera &camera = m_parent.get_camera();
                auto viewport = camera.get_viewport();
                double pixel_size;
                if (camera.get_type() == Camera::EType::Perspective)
                {
                    double cam_dist = (camera.get_position() - sp.position).norm();
                    double fov_height = 2.0 * cam_dist * std::tan(camera.get_fov() * 0.5 * M_PI / 180.0);
                    pixel_size = fov_height / viewport[3];
                }
                else
                {
                    double zoom = camera.get_zoom();
                    pixel_size = (zoom > 0.001) ? 1.0 / zoom : 0.01;
                }

                // Ring is larger than the diamond (snap zone radius)
                // Ring visibly larger than the diamond to show snap zone
                double ring_world_size = 12.0 * pixel_size;

                Vec3d z_axis = sp.normal.normalized();
                Vec3d x_axis, y_axis;
                build_plane_axes(sp.normal, x_axis, y_axis);

                Matrix3d rot;
                rot.col(0) = x_axis;
                rot.col(1) = y_axis;
                rot.col(2) = z_axis;

                Transform3d ring_trafo = Transform3d::Identity();
                ring_trafo.translation() = sp.position + sp.normal * 0.1;
                ring_trafo.linear() = rot * Eigen::Scaling(ring_world_size);

                glsafe(::glDisable(GL_DEPTH_TEST));
                glsafe(::glEnable(GL_BLEND));
                glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

                const Transform3d &view_matrix = camera.get_view_matrix();
                const Transform3d &projection_matrix = camera.get_projection_matrix();

                GLShaderProgram *shader = m_parent.get_shader("gouraud_light");
                if (shader != nullptr)
                {
                    shader->start_using();
                    const Transform3d matrix = view_matrix * ring_trafo;
                    shader->set_uniform("view_model_matrix", matrix);
                    shader->set_uniform("projection_matrix", projection_matrix);
                    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                                        ring_trafo.matrix().block(0, 0, 3, 3).inverse().transpose();
                    shader->set_uniform("view_normal_matrix", view_normal_matrix);
                    m_snap_ring_model.set_color(ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f));
                    m_snap_ring_model.render();
                    shader->stop_using();
                }

                glsafe(::glEnable(GL_DEPTH_TEST));
                glsafe(::glDisable(GL_BLEND));
            }
        }
    }

    // Render drag snap points during Shift+drag in Aligned state
    if (m_plane_dragging && !m_drag_snap_points.empty())
    {
        for (size_t i = 0; i < m_drag_snap_points.size(); ++i)
        {
            const auto &sp = m_drag_snap_points[i];
            bool snapped = (static_cast<int>(i) == m_drag_snapped_idx);

            ColorRGBA color = snapped         ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)  // White when snapped
                              : sp.is_feature ? ColorRGBA(0.9f, 0.7f, 0.2f, 0.9f)  // Gold for features
                                              : ColorRGBA(1.0f, 0.6f, 0.0f, 0.9f); // Orange for face center

            float size = snapped ? 4.0f : 3.0f;
            render_snap_indicator(sp.position, sp.normal, color, size);

            // Draw snap ring around the snapped point
            if (snapped && m_snap_ring_model.is_initialized())
            {
                const Camera &camera = m_parent.get_camera();
                auto viewport = camera.get_viewport();
                double pixel_size;
                if (camera.get_type() == Camera::EType::Perspective)
                {
                    double cam_dist = (camera.get_position() - sp.position).norm();
                    double fov_height = 2.0 * cam_dist * std::tan(camera.get_fov() * 0.5 * M_PI / 180.0);
                    pixel_size = fov_height / viewport[3];
                }
                else
                {
                    double zoom = camera.get_zoom();
                    pixel_size = (zoom > 0.001) ? 1.0 / zoom : 0.01;
                }

                double ring_world_size = 12.0 * pixel_size;

                Vec3d z_ax = sp.normal.normalized();
                Vec3d x_ax, y_ax;
                build_plane_axes(sp.normal, x_ax, y_ax);

                Matrix3d rot;
                rot.col(0) = x_ax;
                rot.col(1) = y_ax;
                rot.col(2) = z_ax;

                Transform3d ring_trafo = Transform3d::Identity();
                ring_trafo.translation() = sp.position + sp.normal * 0.1;
                ring_trafo.linear() = rot * Eigen::Scaling(ring_world_size);

                glsafe(::glDisable(GL_DEPTH_TEST));
                glsafe(::glEnable(GL_BLEND));
                glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

                const Transform3d &view_matrix = camera.get_view_matrix();
                const Transform3d &projection_matrix = camera.get_projection_matrix();

                GLShaderProgram *shader = m_parent.get_shader("gouraud_light");
                if (shader != nullptr)
                {
                    shader->start_using();
                    const Transform3d matrix = view_matrix * ring_trafo;
                    shader->set_uniform("view_model_matrix", matrix);
                    shader->set_uniform("projection_matrix", projection_matrix);
                    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                                        ring_trafo.matrix().block(0, 0, 3, 3).inverse().transpose();
                    shader->set_uniform("view_normal_matrix", view_normal_matrix);
                    m_snap_ring_model.set_color(ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f));
                    m_snap_ring_model.render();
                    shader->stop_using();
                }

                glsafe(::glEnable(GL_DEPTH_TEST));
                glsafe(::glDisable(GL_BLEND));
            }
        }
    }
}

void GLGizmoAlign::render_face_indicator(const FaceData &face, const ColorRGBA &color)
{
    if (face.object_idx < 0)
        return;

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    glsafe(::glDisable(GL_CULL_FACE));

    Vec3d z_axis = face.normal.normalized();
    Vec3d x_axis, y_axis;
    build_plane_axes(face.normal, x_axis, y_axis);

    float disc_radius = 3.0f;

    if (!m_source_indicator.is_initialized())
    {
        const int segments = 32;
        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
        init_data.reserve_vertices(segments + 1);
        init_data.reserve_indices(segments * 3);

        const Vec3f zero = Vec3f::Zero();
        const Vec3f unit_z = Vec3f::UnitZ();
        init_data.add_vertex(zero, unit_z);

        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * float(M_PI) * float(i) / float(segments);
            Vec3f pos(std::cos(angle), std::sin(angle), 0.f);
            init_data.add_vertex(pos, unit_z);
        }

        for (int i = 0; i < segments; ++i)
        {
            int next = (i + 1) % segments;
            init_data.add_triangle(0, i + 1, next + 1);
        }

        m_source_indicator.init_from(std::move(init_data));
    }

    Transform3d trafo = Transform3d::Identity();
    trafo.translation() = face.center + face.normal * 0.1;
    Matrix3d rot;
    rot.col(0) = x_axis;
    rot.col(1) = y_axis;
    rot.col(2) = z_axis;
    trafo.linear() = rot * Eigen::Scaling(static_cast<double>(disc_radius));

    const Camera &camera = m_parent.get_camera();
    const Transform3d &view_matrix = camera.get_view_matrix();
    const Transform3d &projection_matrix = camera.get_projection_matrix();

    GLShaderProgram *shader = m_parent.get_shader("gouraud_light");
    if (shader != nullptr)
    {
        shader->start_using();
        const Transform3d matrix = view_matrix * trafo;
        shader->set_uniform("view_model_matrix", matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                            trafo.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        m_source_indicator.set_color(color);
        m_source_indicator.render();
        shader->stop_using();
    }

    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));
}

Vec2d GLGizmoAlign::project_to_screen(const Vec3d &world_pos) const
{
    const Camera &camera = m_parent.get_camera();
    Point pt = CameraUtils::project(camera, world_pos);
    return Vec2d(static_cast<double>(pt.x()), static_cast<double>(pt.y()));
}

void GLGizmoAlign::update_hover_snap_points(const Vec2d &mouse_pos)
{
    FaceData hover;
    bool have_hit = raycast_face(mouse_pos, hover);

    // Filter by state
    if (have_hit)
    {
        if (m_align_state == EState::Idle && hover.object_idx == m_target_object_idx)
            have_hit = false;
        else if (m_align_state == EState::SourceSelected && hover.object_idx != m_target_object_idx)
            have_hit = false;
    }

    if (have_hit)
    {
        // Check if this is the same face we already have snap points for
        bool same_face = m_has_hover && hover.object_idx == m_hover_face.object_idx &&
                         hover.instance_idx == m_hover_face.instance_idx &&
                         hover.volume_idx == m_hover_face.volume_idx && hover.normal.dot(m_hover_face.normal) > 0.999;

        if (same_face)
        {
            // Same face - cancel any pending switch, just update hover detection
            m_face_switch_pending = false;
            update_hovered_snap_point(mouse_pos);
            return;
        }

        // Different face - delay the switch to prevent losing snap points when
        // the cursor briefly crosses a hole edge
        if (m_has_hover && !m_snap_points.empty())
        {
            if (!m_face_switch_pending)
            {
                // Start the timer for this new face
                m_face_switch_pending = true;
                m_pending_face = hover;
                m_face_switch_time = std::chrono::steady_clock::now();
                update_hovered_snap_point(mouse_pos);
                return;
            }

            // Already pending - check if cursor moved to yet another face
            bool same_pending = hover.object_idx == m_pending_face.object_idx &&
                                hover.instance_idx == m_pending_face.instance_idx &&
                                hover.volume_idx == m_pending_face.volume_idx &&
                                hover.normal.dot(m_pending_face.normal) > 0.999;
            if (!same_pending)
            {
                // Restart timer for the new face
                m_pending_face = hover;
                m_face_switch_time = std::chrono::steady_clock::now();
                update_hovered_snap_point(mouse_pos);
                return;
            }

            // Same pending face - check if the delay has elapsed
            auto elapsed = std::chrono::steady_clock::now() - m_face_switch_time;
            if (elapsed < std::chrono::milliseconds(FACE_SWITCH_DELAY_MS))
            {
                update_hovered_snap_point(mouse_pos);
                return;
            }
        }

        // Switch to the new face (either no previous face, or delay elapsed)
        m_face_switch_pending = false;
        m_hover_face = hover;
        m_has_hover = true;
        m_snap_points.clear();

        Vec3d face_center = compute_face_center(hover);
        m_snap_points.push_back({face_center, hover.normal, 15.0f, false, false});
        detect_feature_centers(hover, m_snap_points);
        update_hovered_snap_point(mouse_pos);
        return;
    }

    // Raycast missed - cancel any pending face switch and keep existing snap points
    // if cursor is still within the object's screen-space bounding box
    m_face_switch_pending = false;
    if (m_has_hover && !m_snap_points.empty())
    {
        const Model &model = *m_parent.get_selection().get_model();
        if (m_hover_face.object_idx >= 0 && m_hover_face.object_idx < static_cast<int>(model.objects.size()))
        {
            const ModelObject *obj = model.objects[m_hover_face.object_idx];
            if (obj != nullptr && m_hover_face.instance_idx < static_cast<int>(obj->instances.size()))
            {
                BoundingBoxf3 obj_bb = obj->instance_bounding_box(m_hover_face.instance_idx);
                double screen_min_x = std::numeric_limits<double>::max();
                double screen_max_x = -std::numeric_limits<double>::max();
                double screen_min_y = std::numeric_limits<double>::max();
                double screen_max_y = -std::numeric_limits<double>::max();

                for (int i = 0; i < 8; ++i)
                {
                    Vec3d corner((i & 1) ? obj_bb.max.x() : obj_bb.min.x(), (i & 2) ? obj_bb.max.y() : obj_bb.min.y(),
                                 (i & 4) ? obj_bb.max.z() : obj_bb.min.z());
                    Vec2d sp = project_to_screen(corner);
                    screen_min_x = std::min(screen_min_x, sp.x());
                    screen_max_x = std::max(screen_max_x, sp.x());
                    screen_min_y = std::min(screen_min_y, sp.y());
                    screen_max_y = std::max(screen_max_y, sp.y());
                }

                if (mouse_pos.x() >= screen_min_x && mouse_pos.x() <= screen_max_x && mouse_pos.y() >= screen_min_y &&
                    mouse_pos.y() <= screen_max_y)
                {
                    update_hovered_snap_point(mouse_pos);
                    return;
                }
            }
        }
    }

    m_snap_points.clear();
    m_hovered_snap_idx = -1;
    m_has_hover = false;
}

Vec3d GLGizmoAlign::compute_face_center(const FaceData &face)
{
    // Bbox-derived faces already have correct centers from raycast_face()
    if (face.facet_idx == SIZE_MAX)
        return face.center;

    const Model &model = *m_parent.get_selection().get_model();
    if (face.object_idx >= static_cast<int>(model.objects.size()))
        return face.center;

    const ModelObject *obj = model.objects[face.object_idx];
    if (obj == nullptr || face.instance_idx >= static_cast<int>(obj->instances.size()))
        return face.center;
    if (face.volume_idx < 0 || face.volume_idx >= static_cast<int>(obj->volumes.size()))
        return face.center;

    const ModelVolume *vol = obj->volumes[face.volume_idx];
    if (vol == nullptr)
        return face.center;

    const indexed_triangle_set &its = vol->mesh().its;
    if (face.facet_idx >= its.indices.size())
        return face.center;

    const ModelInstance *inst = obj->instances[face.instance_idx];
    Transform3d world_trafo = inst->get_matrix() * vol->get_matrix();

    // Get the normal of the hit triangle in local space
    const Vec3i &seed_tri = its.indices[face.facet_idx];
    Vec3f local_normal = (its.vertices[seed_tri[1]] - its.vertices[seed_tri[0]])
                             .cross(its.vertices[seed_tri[2]] - its.vertices[seed_tri[0]])
                             .normalized();

    // Plane distance from origin for the seed triangle (any vertex dotted with the normal)
    float seed_plane_d = its.vertices[seed_tri[0]].dot(local_normal);

    // Collect ALL triangles on the same plane (same normal AND same distance from origin).
    // This handles faces split by cutouts - both sides of the cutout are on the same plane
    // but are not connected by shared edges.
    constexpr double normal_dot_threshold = 0.999; // ~2.5 degree
    constexpr float plane_dist_threshold = 0.5f;   // 0.5mm tolerance for coplanar detection

    // Build a 2D coordinate system on the face plane for bounding box computation
    Vec3d z_axis = face.normal.normalized();
    Vec3d x_axis, y_axis;
    build_plane_axes(face.normal, x_axis, y_axis);

    double min_px = std::numeric_limits<double>::max(), max_px = -std::numeric_limits<double>::max();
    double min_py = std::numeric_limits<double>::max(), max_py = -std::numeric_limits<double>::max();
    bool found_any = false;

    for (size_t fi = 0; fi < its.indices.size(); ++fi)
    {
        const Vec3i &tri = its.indices[fi];
        Vec3f tri_normal = (its.vertices[tri[1]] - its.vertices[tri[0]])
                               .cross(its.vertices[tri[2]] - its.vertices[tri[0]])
                               .normalized();

        if (static_cast<double>(local_normal.dot(tri_normal)) < normal_dot_threshold)
            continue;

        float tri_plane_d = its.vertices[tri[0]].dot(local_normal);
        if (std::abs(tri_plane_d - seed_plane_d) > plane_dist_threshold)
            continue;

        found_any = true;
        for (int vi = 0; vi < 3; ++vi)
        {
            Vec3d wp = world_trafo * its.vertices[tri[vi]].cast<double>();
            double px = wp.dot(x_axis);
            double py = wp.dot(y_axis);
            min_px = std::min(min_px, px);
            max_px = std::max(max_px, px);
            min_py = std::min(min_py, py);
            max_py = std::max(max_py, py);
        }
    }

    if (!found_any)
        return face.center;

    // BB center in 2D plane coordinates, projected back to 3D at the hit depth
    double center_px = (min_px + max_px) * 0.5;
    double center_py = (min_py + max_py) * 0.5;
    double hit_depth = face.center.dot(z_axis);

    return x_axis * center_px + y_axis * center_py + z_axis * hit_depth;
}

void GLGizmoAlign::detect_feature_centers(const FaceData &face, std::vector<SnapPoint> &out)
{
    // Bbox-derived faces have no mesh features to detect
    if (face.facet_idx == SIZE_MAX)
        return;

    const Model &model = *m_parent.get_selection().get_model();
    if (face.object_idx >= static_cast<int>(model.objects.size()))
        return;

    const ModelObject *obj = model.objects[face.object_idx];
    if (obj == nullptr || face.instance_idx >= static_cast<int>(obj->instances.size()))
        return;

    Vec3d z_axis = face.normal.normalized();
    Transform3d inst_trafo = obj->instances[face.instance_idx]->get_matrix();

    // Negative volume feature detection
    for (const ModelVolume *vol : obj->volumes)
    {
        if (vol == nullptr || !vol->is_negative_volume())
            continue;

        Transform3d vol_world = inst_trafo * vol->get_matrix();
        BoundingBoxf3 bb = vol->mesh().bounding_box();
        Vec3d vol_center = vol_world * bb.center();

        double face_depth = face.center.dot(z_axis);
        double vol_depth = vol_center.dot(z_axis);
        if (std::abs(vol_depth - face_depth) < bb.size().norm() * 0.5)
        {
            Vec3d projected = vol_center + z_axis * (face_depth - vol_depth);
            out.push_back({projected, face.normal, 12.0f, true, false});
        }
    }

    // Mesh hole detection: find interior boundary loops on the coplanar face
    if (face.volume_idx < 0 || face.volume_idx >= static_cast<int>(obj->volumes.size()))
        return;
    const ModelVolume *vol = obj->volumes[face.volume_idx];
    if (vol == nullptr)
        return;

    const indexed_triangle_set &its = vol->mesh().its;
    if (face.facet_idx >= its.indices.size())
        return;

    Transform3d world_trafo = inst_trafo * vol->get_matrix();

    // Get the seed triangle normal and plane
    const Vec3i &seed_tri = its.indices[face.facet_idx];
    Vec3f local_normal = (its.vertices[seed_tri[1]] - its.vertices[seed_tri[0]])
                             .cross(its.vertices[seed_tri[2]] - its.vertices[seed_tri[0]])
                             .normalized();
    float seed_plane_d = its.vertices[seed_tri[0]].dot(local_normal);

    constexpr double normal_dot_threshold = 0.999;
    constexpr float plane_dist_threshold = 0.5f;

    // Mark coplanar triangles
    std::vector<bool> is_coplanar(its.indices.size(), false);
    for (size_t fi = 0; fi < its.indices.size(); ++fi)
    {
        const Vec3i &tri = its.indices[fi];
        Vec3f tri_normal = (its.vertices[tri[1]] - its.vertices[tri[0]])
                               .cross(its.vertices[tri[2]] - its.vertices[tri[0]])
                               .normalized();
        if (static_cast<double>(local_normal.dot(tri_normal)) < normal_dot_threshold)
            continue;
        float tri_plane_d = its.vertices[tri[0]].dot(local_normal);
        if (std::abs(tri_plane_d - seed_plane_d) > plane_dist_threshold)
            continue;
        is_coplanar[fi] = true;
    }

    // Find boundary edges: edges of coplanar triangles not shared with another coplanar triangle.
    // Use a map from edge -> count of coplanar triangles sharing it.
    auto edge_key = [](int a, int b) -> uint64_t
    {
        if (a > b)
            std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
    };

    std::unordered_map<uint64_t, int> edge_coplanar_count;
    for (size_t fi = 0; fi < its.indices.size(); ++fi)
    {
        if (!is_coplanar[fi])
            continue;
        const Vec3i &tri = its.indices[fi];
        for (int e = 0; e < 3; ++e)
            edge_coplanar_count[edge_key(tri[e], tri[(e + 1) % 3])]++;
    }

    // Collect boundary edges as directed pairs (for consistent loop tracing)
    // Each boundary edge appears once in the coplanar triangle with a specific winding order.
    std::unordered_multimap<int, int> directed_edges; // from -> to
    for (size_t fi = 0; fi < its.indices.size(); ++fi)
    {
        if (!is_coplanar[fi])
            continue;
        const Vec3i &tri = its.indices[fi];
        for (int e = 0; e < 3; ++e)
        {
            int v0 = tri[e];
            int v1 = tri[(e + 1) % 3];
            if (edge_coplanar_count[edge_key(v0, v1)] == 1)
                directed_edges.emplace(v0, v1);
        }
    }

    if (directed_edges.empty())
        return;

    // Trace closed loops by following directed edges, consuming them as we go
    std::vector<std::vector<int>> loops;

    while (!directed_edges.empty())
    {
        int start_v = directed_edges.begin()->first;
        std::vector<int> loop;
        int current = start_v;

        while (true)
        {
            auto it = directed_edges.find(current);
            if (it == directed_edges.end())
                break;

            int next = it->second;
            directed_edges.erase(it);
            loop.push_back(current);

            if (next == start_v)
                break; // Closed loop

            current = next;

            if (loop.size() > its.vertices.size())
                break; // Safety: prevent infinite loop on degenerate mesh
        }

        if (loop.size() >= 3)
            loops.push_back(std::move(loop));
    }

    if (loops.size() <= 1)
        return; // Only outer perimeter, no holes

    // Build a 2D coordinate system on the face plane for area computation
    Vec3d plane_x, plane_y;
    build_plane_axes(face.normal, plane_x, plane_y);

    // The outer perimeter has the largest enclosed area (not vertex count)
    size_t largest_idx = 0;
    double largest_area = 0;
    for (size_t i = 0; i < loops.size(); ++i)
    {
        // Compute 2D signed area using the shoelace formula
        double area = 0;
        size_t n = loops[i].size();
        for (size_t j = 0; j < n; ++j)
        {
            Vec3d p0 = world_trafo * its.vertices[loops[i][j]].cast<double>();
            Vec3d p1 = world_trafo * its.vertices[loops[i][(j + 1) % n]].cast<double>();
            double x0 = p0.dot(plane_x), y0 = p0.dot(plane_y);
            double x1 = p1.dot(plane_x), y1 = p1.dot(plane_y);
            area += (x0 * y1 - x1 * y0);
        }
        area = std::abs(area) * 0.5;
        if (area > largest_area)
        {
            largest_area = area;
            largest_idx = i;
        }
    }

    double hit_depth = face.center.dot(z_axis);

    for (size_t i = 0; i < loops.size(); ++i)
    {
        if (i == largest_idx)
            continue; // Skip outer perimeter

        // Skip degenerate micro-loops (mesh artifacts at corners/edges)
        {
            double a = 0;
            size_t n = loops[i].size();
            for (size_t j = 0; j < n; ++j)
            {
                Vec3d p0 = world_trafo * its.vertices[loops[i][j]].cast<double>();
                Vec3d p1 = world_trafo * its.vertices[loops[i][(j + 1) % n]].cast<double>();
                a += (p0.dot(plane_x) * p1.dot(plane_y) - p1.dot(plane_x) * p0.dot(plane_y));
            }
            if (std::abs(a) * 0.5 < 1.0) // Less than 1 mm^2
                continue;
        }

        // Compute centroid of this hole loop in world space
        Vec3d hole_center = Vec3d::Zero();
        for (int vi : loops[i])
            hole_center += world_trafo * its.vertices[vi].cast<double>();
        hole_center /= static_cast<double>(loops[i].size());

        // Project onto the face plane
        double center_depth = hole_center.dot(z_axis);
        hole_center += z_axis * (hit_depth - center_depth);

        out.push_back({hole_center, face.normal, 12.0f, true, false});
    }
}

void GLGizmoAlign::update_hovered_snap_point(const Vec2d &mouse_pos)
{
    double min_dist = std::numeric_limits<double>::max();
    m_hovered_snap_idx = -1;

    for (size_t i = 0; i < m_snap_points.size(); ++i)
    {
        Vec2d screen_pos = project_to_screen(m_snap_points[i].position);
        double dist = (screen_pos - mouse_pos).norm();

        m_snap_points[i].hovered = (dist < m_snap_points[i].screen_radius);

        if (dist < min_dist && dist < m_snap_points[i].screen_radius)
        {
            min_dist = dist;
            m_hovered_snap_idx = static_cast<int>(i);
        }
    }
}

void GLGizmoAlign::render_snap_indicator(const Vec3d &position, const Vec3d &normal, const ColorRGBA &color, float size)
{
    // Build diamond model (rotated square) if not cached
    if (!m_diamond_model.is_initialized())
    {
        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
        init_data.reserve_vertices(5);
        init_data.reserve_indices(12);

        const Vec3f unit_z = Vec3f::UnitZ();
        init_data.add_vertex(Vec3f::Zero(), unit_z);
        init_data.add_vertex(Vec3f(1.0f, 0.0f, 0.0f), unit_z);
        init_data.add_vertex(Vec3f(0.0f, 1.0f, 0.0f), unit_z);
        init_data.add_vertex(Vec3f(-1.0f, 0.0f, 0.0f), unit_z);
        init_data.add_vertex(Vec3f(0.0f, -1.0f, 0.0f), unit_z);

        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(0, 2, 3);
        init_data.add_triangle(0, 3, 4);
        init_data.add_triangle(0, 4, 1);

        m_diamond_model.init_from(std::move(init_data));
    }

    // Build snap ring model (thin circle) if not cached
    if (!m_snap_ring_model.is_initialized())
    {
        constexpr int ring_segments = 48;
        constexpr float inner_r = 0.85f;
        constexpr float outer_r = 1.0f;

        GLModel::Geometry ring_data;
        ring_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
        ring_data.reserve_vertices(ring_segments * 2);
        ring_data.reserve_indices(ring_segments * 6);

        const Vec3f unit_z = Vec3f::UnitZ();
        for (int i = 0; i < ring_segments; ++i)
        {
            float angle = 2.0f * float(M_PI) * float(i) / float(ring_segments);
            float c = std::cos(angle);
            float s = std::sin(angle);
            ring_data.add_vertex(Vec3f(inner_r * c, inner_r * s, 0.f), unit_z);
            ring_data.add_vertex(Vec3f(outer_r * c, outer_r * s, 0.f), unit_z);
        }

        for (int i = 0; i < ring_segments; ++i)
        {
            int next = (i + 1) % ring_segments;
            int i0 = i * 2;
            int i1 = i * 2 + 1;
            int n0 = next * 2;
            int n1 = next * 2 + 1;
            ring_data.add_triangle(i0, i1, n1);
            ring_data.add_triangle(i0, n1, n0);
        }

        m_snap_ring_model.init_from(std::move(ring_data));
    }

    // Compute a world-space size that appears as a fixed screen-space size
    const Camera &camera = m_parent.get_camera();
    const auto &viewport = camera.get_viewport();
    double pixel_size;
    if (camera.get_type() == Camera::EType::Perspective)
    {
        double cam_dist = (camera.get_position() - position).norm();
        double fov_height = 2.0 * cam_dist * std::tan(camera.get_fov() * 0.5 * M_PI / 180.0);
        pixel_size = fov_height / viewport[3];
    }
    else
    {
        // Ortho visible height = viewport[3] / zoom, so pixel_size = 1 / zoom
        double zoom = camera.get_zoom();
        pixel_size = (zoom > 0.001) ? 1.0 / zoom : 0.01;
    }
    double world_size = size * pixel_size;

    Vec3d z_axis = normal.normalized();
    Vec3d x_axis, y_axis;
    build_plane_axes(normal, x_axis, y_axis);

    Matrix3d rot;
    rot.col(0) = x_axis;
    rot.col(1) = y_axis;
    rot.col(2) = z_axis;

    Transform3d trafo = Transform3d::Identity();
    trafo.translation() = position + normal * 0.1;
    trafo.linear() = rot * Eigen::Scaling(world_size);

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    const Transform3d &view_matrix = camera.get_view_matrix();
    const Transform3d &projection_matrix = camera.get_projection_matrix();

    GLShaderProgram *shader = m_parent.get_shader("gouraud_light");
    if (shader != nullptr)
    {
        shader->start_using();

        // Diamond
        const Transform3d matrix = view_matrix * trafo;
        shader->set_uniform("view_model_matrix", matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                            trafo.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        m_diamond_model.set_color(color);
        m_diamond_model.render();

        shader->stop_using();
    }

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoAlign::on_render_input_window(float x, float y, float bottom_limit)
{
    draw_align_panel(x, y, bottom_limit);
}

void GLGizmoAlign::draw_align_panel(float toolbar_x, float icon_y, float toolbar_bottom)
{
    const float slider_icon_width = ImGuiPureWrap::get_slider_icon_size().x;
    const float minimal_slider_width = m_imgui->scaled(4.f);

    // Calculate width based on content like other gizmos
    const float caption_max = ImGuiPureWrap::calc_text_size(_u8L("Shift + Left mouse:")).x + m_imgui->scaled(1.f);
    const float label_width = std::max({ImGuiPureWrap::calc_text_size(_u8L("Depth:")).x,
                                        ImGuiPureWrap::calc_text_size(_u8L("Rotation:")).x,
                                        ImGuiPureWrap::calc_text_size(_u8L("Position:")).x,
                                        ImGuiPureWrap::calc_text_size(_u8L("Nudge:")).x}) +
                              m_imgui->scaled(1.5f);
    const float slider_width = minimal_slider_width + label_width;
    float window_width = slider_width + slider_icon_width;
    window_width = std::max(window_width,
                            caption_max + ImGuiPureWrap::calc_text_size(_u8L("Slide object on surface")).x);
    window_width = std::max(window_width, m_imgui->scaled(16.f)); // Minimum width

    // Position panel like other gizmos: right-aligned to toolbar, vertically centered on icon
    // In Aligned state (expanded panel), bottom-justify to toolbar bottom instead
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse;

    if (m_popup_render_count == 0 && m_popup_height <= 0.f)
    {
        // First frame: render offscreen to measure height
        set_side_flyout_pos(toolbar_x, -500.f);
    }
    else
    {
        // Always center on the gizmo icon
        float menu_y = icon_y - m_popup_height * 0.5f;
        set_side_flyout_pos(toolbar_x, menu_y);
    }

    m_popup_render_count++;

    bool measuring_frame = (m_popup_render_count == 1 && m_popup_height <= 0.f);
    if (measuring_frame)
    {
        // First frame: invisible while measuring
        window_flags |= ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
    }

    ImGuiPureWrap::begin(get_name(), window_flags);

    // Instructions header (like other gizmos)
    auto draw_instruction = [&caption_max](const std::string &key, const std::string &action)
    {
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, key);
        ImGui::SameLine(caption_max);
        ImGuiPureWrap::text(action);
    };

    switch (m_align_state)
    {
    case EState::Idle:
        draw_instruction(_u8L("Left mouse:"), _u8L("Select face to align"));
        break;

    case EState::SourceSelected:
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.f), "%s", _u8L("Source face selected").c_str());
        draw_instruction(_u8L("Left mouse:"), _u8L("Select target face"));
        ImGui::Separator();
        if (ImGui::Button(_u8L("Reset").c_str(), ImVec2(-1, 0)))
        {
            int saved_target = m_target_object_idx;
            reset_state();
            m_target_object_idx = saved_target;
        }
        break;

    case EState::Aligned:
    {
        draw_instruction(_u8L("Shift + Left mouse:"), _u8L("Slide object on surface"));
        ImGui::Separator();

        bool changed = false;

        // Flip to other side of the target plane (toggle)
        bool was_inverted = m_inverted;
        if (was_inverted)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.63f, 0.20f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.63f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }
        float tt_w = ImGui::GetFontSize() * 20.f;
        if (ImGuiPureWrap::icon_button(ImGui::FlipIcon, _u8L("Flip").c_str()))
        {
            m_inverted = !m_inverted;
            m_depth_offset = 0.f; // Reset depth - embedding direction reverses with flip
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Flip to other side of the target plane"), tt_w);
        if (was_inverted)
            ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGuiPureWrap::icon_button(ImGui::Rotate90Icon, _u8L("Rotate").c_str()))
        {
            m_rotation = std::fmod(m_rotation - 90.f + 360.f, 360.f);
            if (m_rotation > 180.f)
                m_rotation -= 360.f;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Rotate 90 degrees clockwise"), tt_w);
        ImGui::SameLine();
        if (ImGuiPureWrap::icon_button(ImGui::MirrorHIcon, _u8L("Mirror").c_str()))
        {
            m_mirror_v = !m_mirror_v;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Mirror horizontal"), tt_w);
        ImGui::SameLine();
        if (ImGuiPureWrap::icon_button(ImGui::MirrorVIcon, _u8L("Mirror").c_str()))
        {
            m_mirror_h = !m_mirror_h;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Mirror vertical"), tt_w);

        // Depth: 0 = flush with surface, slide left to embed (not flipped), right to embed (flipped)
        {
            float obj_height = static_cast<float>(m_orig_size.z());
            if (obj_height < 0.01f)
                obj_height = 0.01f;

            float depth_min = m_inverted ? 0.f : -obj_height;
            float depth_max = m_inverted ? obj_height : 0.f;

            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("Depth:"));
            ImGui::SameLine(label_width);
            ImGui::PushItemWidth(window_width - label_width - slider_icon_width);
            if (m_imgui->slider_float("##depth", &m_depth_offset, depth_min, depth_max, "%.2f mm"))
                changed = true;
            ImGui::PopItemWidth();
        }

        // Rotation
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(_u8L("Rotation:"));
        ImGui::SameLine(label_width);
        ImGui::PushItemWidth(window_width - label_width - slider_icon_width);
        if (m_imgui->slider_float("##rotation", &m_rotation, -180.f, 180.f, "%.1f"))
            changed = true;
        ImGui::PopItemWidth();

        // Position: absolute offset from alignment point (syncs with Shift+drag)
        {
            float pos_field_w = (window_width - label_width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("Position:"));
            ImGui::SameLine(label_width);
            ImGui::PushItemWidth(pos_field_w);
            float pos_x = static_cast<float>(m_plane_offset.x());
            if (ImGui::InputFloat("##pos_x", &pos_x, 0.f, 0.f, "%.2f mm"))
            {
                m_plane_offset.x() = static_cast<double>(pos_x);
                changed = true;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(pos_field_w);
            float pos_y = static_cast<float>(m_plane_offset.y());
            if (ImGui::InputFloat("##pos_y", &pos_y, 0.f, 0.f, "%.2f mm"))
            {
                m_plane_offset.y() = static_cast<double>(pos_y);
                changed = true;
            }
            ImGui::PopItemWidth();
        }

        // Nudge: relative bump (adds to position, resets to 0 after apply)
        {
            float pos_field_w = (window_width - label_width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("Nudge:"));
            ImGui::SameLine(label_width);
            ImGui::PushItemWidth(pos_field_w);
            ImGui::InputFloat("##nudge_x", &m_nudge_x, 0.f, 0.f, "%.2f mm");
            bool nudge_x_apply = ImGui::IsItemDeactivatedAfterEdit();
            if (nudge_x_apply && std::abs(m_nudge_x) > 0.001f)
            {
                m_plane_offset.x() += static_cast<double>(m_nudge_x);
                m_nudge_x = 0.f;
                changed = true;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(pos_field_w);
            ImGui::InputFloat("##nudge_y", &m_nudge_y, 0.f, 0.f, "%.2f mm");
            bool nudge_y_apply = ImGui::IsItemDeactivatedAfterEdit();
            if (nudge_y_apply && std::abs(m_nudge_y) > 0.001f)
            {
                m_plane_offset.y() += static_cast<double>(m_nudge_y);
                m_nudge_y = 0.f;
                changed = true;
            }
            ImGui::PopItemWidth();
        }

        ImGui::Separator();

        // Scale and Size section with lock icon on the left
        {
            float size_x = static_cast<float>(m_orig_size.x()) * m_scale_x / 100.f;
            float size_y = static_cast<float>(m_orig_size.y()) * m_scale_y / 100.f;
            float size_z = static_cast<float>(m_orig_size.z()) * m_scale_z / 100.f;

            // Lock icon column width
            const ImFont *font = ImGui::GetFont();
            const ImFontGlyph *lock_glyph = font->FindGlyph(ImGui::Lock);
            float lock_w = lock_glyph ? (lock_glyph->X1 - lock_glyph->X0) : m_imgui->scaled(1.f);
            float lock_col_w = lock_w + ImGui::GetStyle().FramePadding.x * 2.f + ImGui::GetStyle().ItemSpacing.x;

            float row_label_w = lock_col_w +
                                std::max(ImGuiPureWrap::calc_text_size(_u8L("Scale:")).x,
                                         ImGuiPureWrap::calc_text_size(_u8L("Size:")).x) +
                                m_imgui->scaled(0.5f);
            float suffix_w = ImGuiPureWrap::calc_text_size(_u8L("mm")).x + ImGui::GetStyle().ItemSpacing.x;
            float field_w = (window_width - row_label_w - suffix_w) / 3.f;

            // Save Y position for lock icon (centered between scale and size rows)
            float scale_row_y = ImGui::GetCursorPosY();

            // Scale row
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lock_col_w);
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("Scale:"));
            ImGui::SameLine(row_label_w);

            auto handle_scale_input = [&](const char *id, float &scale, float &other1, float &other2)
            {
                ImGui::PushItemWidth(field_w);
                ImGui::InputFloat(id, &scale, 0.f, 0.f, "%.0f");
                if (ImGui::IsItemActivated())
                {
                    m_scale_before_x = m_scale_x;
                    m_scale_before_y = m_scale_y;
                    m_scale_before_z = m_scale_z;
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    if (scale < 1.f)
                        scale = 1.f;
                    float b = (&scale == &m_scale_x)   ? m_scale_before_x
                              : (&scale == &m_scale_y) ? m_scale_before_y
                                                       : m_scale_before_z;
                    if (m_lock_scale && std::abs(b) > 0.01f)
                    {
                        float r = scale / b;
                        float b1 = (&other1 == &m_scale_x)   ? m_scale_before_x
                                   : (&other1 == &m_scale_y) ? m_scale_before_y
                                                             : m_scale_before_z;
                        float b2 = (&other2 == &m_scale_x)   ? m_scale_before_x
                                   : (&other2 == &m_scale_y) ? m_scale_before_y
                                                             : m_scale_before_z;
                        other1 = b1 * r;
                        other2 = b2 * r;
                    }
                    changed = true;
                }
                ImGui::PopItemWidth();
            };

            handle_scale_input("##sx", m_scale_x, m_scale_y, m_scale_z);
            ImGui::SameLine();
            handle_scale_input("##sy", m_scale_y, m_scale_x, m_scale_z);
            ImGui::SameLine();
            handle_scale_input("##sz", m_scale_z, m_scale_x, m_scale_y);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("%"));

            // Size row - indent to align with Scale fields (lock icon width + spacing)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lock_col_w);
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("Size:"));
            ImGui::SameLine(row_label_w);

            auto handle_size_input =
                [&](const char *id, float &size, float &scale, double orig_dim, float &other1, float &other2)
            {
                ImGui::PushItemWidth(field_w);
                ImGui::InputFloat(id, &size, 0.f, 0.f, "%.2f");
                if (ImGui::IsItemActivated())
                {
                    m_scale_before_x = m_scale_x;
                    m_scale_before_y = m_scale_y;
                    m_scale_before_z = m_scale_z;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && orig_dim > 0.001)
                {
                    float ns = size / static_cast<float>(orig_dim) * 100.f;
                    if (ns < 1.f)
                        ns = 1.f;
                    float b = (&scale == &m_scale_x)   ? m_scale_before_x
                              : (&scale == &m_scale_y) ? m_scale_before_y
                                                       : m_scale_before_z;
                    if (m_lock_scale && std::abs(b) > 0.01f)
                    {
                        float r = ns / b;
                        float b1 = (&other1 == &m_scale_x)   ? m_scale_before_x
                                   : (&other1 == &m_scale_y) ? m_scale_before_y
                                                             : m_scale_before_z;
                        float b2 = (&other2 == &m_scale_x)   ? m_scale_before_x
                                   : (&other2 == &m_scale_y) ? m_scale_before_y
                                                             : m_scale_before_z;
                        other1 = b1 * r;
                        other2 = b2 * r;
                    }
                    scale = ns;
                    changed = true;
                }
                ImGui::PopItemWidth();
            };

            handle_size_input("##szx", size_x, m_scale_x, m_orig_size.x(), m_scale_y, m_scale_z);
            ImGui::SameLine();
            handle_size_input("##szy", size_y, m_scale_y, m_orig_size.y(), m_scale_x, m_scale_z);
            ImGui::SameLine();
            handle_size_input("##szz", size_z, m_scale_z, m_orig_size.z(), m_scale_x, m_scale_y);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(_u8L("mm"));

            // Draw lock icon centered vertically between scale and size rows
            float size_row_end_y = ImGui::GetCursorPosY();

            // Use icon_button which we know handles clicks correctly
            const ImFont *btn_font = ImGui::GetFont();
            const ImFontGlyph *lock_g = btn_font->FindGlyph(ImGui::Lock);
            float lock_icon_h = lock_g ? (lock_g->Y1 - lock_g->Y0) : m_imgui->scaled(1.f);
            float lock_btn_h = lock_icon_h + ImGui::GetStyle().FramePadding.y * 2.f;
            float lock_y = scale_row_y + (size_row_end_y - scale_row_y - lock_btn_h) * 0.5f;

            ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, lock_y));
            wchar_t lock_icon = m_lock_scale ? ImGui::Lock : ImGui::UnlockHovered;
            if (ImGuiPureWrap::icon_button(lock_icon, ""))
                m_lock_scale = !m_lock_scale;

            // Restore cursor to after the size row
            ImGui::SetCursorPosY(size_row_end_y);
        }

        if (changed)
            apply_alignment();

        ImGui::Separator();

        // Alignment: label + 5 icon buttons in a row
        {
            float tt_w = ImGui::GetFontSize() * 20.f;

            // Get icon glyph height to compute button height for label centering
            const ImFont *font = ImGui::GetFont();
            const ImFontGlyph *glyph = font->FindGlyph(ImGui::AlignLeftIcon);
            float icon_h = glyph ? (glyph->Y1 - glyph->Y0) : ImGui::GetTextLineHeight();
            float btn_h = icon_h + ImGui::GetStyle().FramePadding.y * 2.f;
            float text_h = ImGui::GetTextLineHeight();
            float y_offset = (btn_h - text_h) * 0.5f;

            float save_y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(save_y + y_offset);
            ImGuiPureWrap::text(_u8L("Align object:"));
            ImGui::SameLine();
            ImGui::SetCursorPosY(save_y);
            if (ImGuiPureWrap::icon_button(ImGui::AlignLeftIcon, ""))
                snap_align(-1, -2);
            if (ImGui::IsItemHovered())
                ImGuiPureWrap::tooltip(_u8L("Align left"), tt_w);
            ImGui::SameLine();
            if (ImGuiPureWrap::icon_button(ImGui::AlignCenterIcon, ""))
                snap_align(0, 0);
            if (ImGui::IsItemHovered())
                ImGuiPureWrap::tooltip(_u8L("Align center"), tt_w);
            ImGui::SameLine();
            if (ImGuiPureWrap::icon_button(ImGui::AlignRightIcon, ""))
                snap_align(1, -2);
            if (ImGui::IsItemHovered())
                ImGuiPureWrap::tooltip(_u8L("Align right"), tt_w);
            ImGui::SameLine();
            if (ImGuiPureWrap::icon_button(ImGui::AlignTopIcon, ""))
                snap_align(-2, 1);
            if (ImGui::IsItemHovered())
                ImGuiPureWrap::tooltip(_u8L("Align top"), tt_w);
            ImGui::SameLine();
            if (ImGuiPureWrap::icon_button(ImGui::AlignBottomIcon, ""))
                snap_align(-2, -1);
            if (ImGui::IsItemHovered())
                ImGuiPureWrap::tooltip(_u8L("Align bottom"), tt_w);
        }

        ImGui::Separator();

        // Action buttons (centered)
        {
            // Measure total width of all three buttons
            const ImGuiStyle &style = ImGui::GetStyle();
            auto measure_btn = [&](const wchar_t icon, const char *label) -> float
            {
                const ImFontGlyph *g = ImGui::GetFont()->FindGlyph(icon);
                float iw = g ? (g->X1 - g->X0) : 0.f;
                ImVec2 ts = ImGui::CalcTextSize(label);
                float spacing = (iw > 0.f && ts.x > 0.f) ? style.ItemInnerSpacing.x : 0.f;
                return iw + spacing + ts.x + style.FramePadding.x * 2.f;
            };

            float w1 = measure_btn(ImGui::EmbossIcon, _u8L("Weld").c_str());
            float w2 = measure_btn(ImGui::EngraveIcon, _u8L("Subtract").c_str());
            float w3 = measure_btn(ImGui::NewAlignIcon, _u8L("New Alignment").c_str());
            float total_w = w1 + w2 + w3 + style.ItemSpacing.x * 2.f;
            float avail = ImGui::GetContentRegionAvail().x;
            float indent = (avail - total_w) * 0.5f;
            if (indent > 0.f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

            if (ImGuiPureWrap::icon_button(ImGui::EmbossIcon, _u8L("Weld").c_str()))
                accept_as_volume(ModelVolumeType::MODEL_PART, "Weld");
            ImGui::SameLine();
            if (ImGuiPureWrap::icon_button(ImGui::EngraveIcon, _u8L("Subtract").c_str()))
                accept_as_volume(ModelVolumeType::NEGATIVE_VOLUME, "Subtract");
            ImGui::SameLine();
            if (ImGuiPureWrap::icon_button(ImGui::NewAlignIcon, _u8L("New Alignment").c_str()))
            {
                int saved_target = m_target_object_idx;
                reset_state();
                m_target_object_idx = saved_target;
            }
        }
    }
    break;
    }

    // Measure popup height for positioning
    const ImVec2 size = ImGui::GetWindowSize();
    if (size.y > 0.f && m_popup_height != size.y)
    {
        m_popup_height = size.y;
        if (m_popup_render_count == 1)
            m_imgui->set_requires_extra_frame();
    }

    ImGuiPureWrap::end();

    if (measuring_frame)
        ImGui::PopStyleVar();
}

void GLGizmoAlign::reset_state()
{
    m_align_state = EState::Idle;
    m_source = FaceData();
    m_target = FaceData();
    m_depth_offset = 0.f;
    m_rotation = 0.f;
    m_mirror_h = false;
    m_mirror_v = false;
    m_inverted = false;
    m_scale_x = 100.f;
    m_scale_y = 100.f;
    m_scale_z = 100.f;
    m_lock_scale = true;
    m_orig_size = Vec3d::Zero();
    m_target_object_idx = -1;
    m_accept_in_progress = false;
    m_plane_dragging = false;
    m_plane_offset = Vec2d::Zero();
    m_drag_snap_points.clear();
    m_drag_snapped_idx = -1;
    m_drag_raw_offset = Vec2d::Zero();
    m_original_transform = Transform3d::Identity();
    m_snap_points.clear();
    m_hovered_snap_idx = -1;
    m_hover_face = FaceData();
    m_has_hover = false;
    m_face_switch_pending = false;
    m_popup_render_count = 0;
    m_popup_height = 0.f;
}

} // namespace DSKY
