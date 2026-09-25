///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "GLGizmoBase.hpp"
#include "DSKY/GUI/GLModel.hpp"
#include "DSKY/GUI/MeshUtils.hpp"

namespace Luminary
{
enum class ModelVolumeType : int;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class GLGizmoFlatten : public GLGizmoBase
{
    // This gizmo does not use grabbers. The m_hover_id relates to polygon managed by the class itself.

private:
    GLModel arrow;

    struct PlaneData
    {
        std::vector<Vec3d> vertices; // should be in fact local in update_planes()
        PickingModel vbo;
        Vec3d normal;
        float area;
        int picking_id{-1};
    };

    // This holds information to decide whether recalculation is necessary:
    std::vector<Transform3d> m_volumes_matrices;
    std::vector<ModelVolumeType> m_volumes_types;
    Vec3d m_first_instance_scale;
    Vec3d m_first_instance_mirror;

    std::vector<PlaneData> m_planes;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_planes_casters;
    const ModelObject *m_old_model_object = nullptr;
    int m_old_instance_id{-1};

    void update_planes();
    bool is_plane_update_necessary() const;

public:
    GLGizmoFlatten(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id);

    void set_flattening_data(const ModelObject *model_object, int instance_id);

    /// <summary>
    /// Apply rotation on select plane
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const MouseInput &mouse) override;

    void data_changed(bool is_serializing) override;

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_render() override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
    void on_set_state() override;
    CommonGizmosDataID on_get_requirements() const override;
};

} // namespace DSKY

