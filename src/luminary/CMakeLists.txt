#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(luminary)

# The application identity header, generated where the engine can reach it. Its five macros carry
# the app name, key, version, build id and build type; consumers include it by bare name.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/core/PreFlightVersion.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/PreFlightVersion.h @ONLY)

# Sources in one block per module.
set(LUMINARY_SOURCES
    Luminary.hpp
    Luminary.cpp

    # core
    core/Prelude.hpp
    core/Exception.hpp
    core/FileParserError.hpp
    core/I18N.hpp
    core/I18N.cpp
    core/ObjectID.hpp
    core/ObjectID.cpp
    core/Raii.hpp
    core/Semver.hpp
    core/Semver.cpp
    core/enum_bitmask.hpp
    core/format.hpp
    core/algorithm/AStar.hpp
    core/containers/AnyPtr.hpp
    core/containers/Indices.hpp
    core/containers/MutablePriorityQueue.hpp
    core/containers/Sizing.hpp
    core/containers/StaticMap.hpp
    core/diagnostics/DebugCounters.hpp
    core/diagnostics/DebugOutput.hpp
    core/diagnostics/PerfTiming.hpp
    core/diagnostics/Timer.hpp
    core/diagnostics/Timer.cpp
    core/parallel/Execution.hpp
    core/parallel/ExecutionSeq.hpp
    core/parallel/ExecutionTBB.hpp
    core/text/Duration.hpp
    core/text/Duration.cpp
    core/text/Encoding.hpp
    core/text/Encoding.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/PreFlightVersion.h"

    # platform
    platform/concurrency/CpuAffinity.hpp
    platform/concurrency/CpuAffinity.cpp
    platform/concurrency/Thread.hpp
    platform/concurrency/Thread.cpp
    platform/concurrency/WorkerPolicy.hpp
    platform/files/FileIO.hpp
    platform/files/FileIO.cpp
    platform/host/BlacklistedLibraryCheck.hpp
    platform/host/BlacklistedLibraryCheck.cpp
    platform/host/Platform.hpp
    platform/host/Platform.cpp
    platform/paths/Paths.hpp
    platform/paths/Paths.cpp
    platform/process/Memory.hpp
    platform/process/Process.hpp
    platform/process/Process.cpp
    platform/signals/TryCatchSignal.hpp
    platform/signals/TryCatchSignal.cpp
    platform/time/Time.hpp
    platform/time/Time.cpp
    # platform/paths/MacUtils.mm belongs to this group and is appended under if (APPLE) below,
    # because a source row that only exists on one platform cannot carry its guard inside a set().

    # io
    io/png/PNGReadWrite.hpp
    io/png/PNGReadWrite.cpp
    io/zip/miniz_extension.hpp
    io/zip/miniz_extension.cpp

    # geometry
    geometry/primitives/BoundingBox.hpp
    geometry/primitives/BoundingBox.cpp
    geometry/primitives/ColoredLine.hpp
    geometry/primitives/Line.hpp
    geometry/primitives/Line.cpp
    geometry/primitives/Point.hpp
    geometry/primitives/Point.cpp
    geometry/contours/ExPolygon.hpp
    geometry/contours/ExPolygon.cpp
    geometry/contours/MultiPoint.hpp
    geometry/contours/MultiPoint.cpp
    geometry/contours/Polygon.hpp
    geometry/contours/Polygon.cpp
    geometry/contours/Polyline.hpp
    geometry/contours/Polyline.cpp
    geometry/clipper/ClipperUtils.hpp
    geometry/clipper/ClipperUtils.cpp
    geometry/clipper/ClipperZUtils.hpp
    geometry/transform/Geometry.hpp
    geometry/transform/Geometry.cpp
    geometry/surface/Surface.hpp
    geometry/surface/Surface.cpp
    geometry/surface/SurfaceCollection.hpp
    geometry/surface/SurfaceCollection.cpp
    geometry/diagnostics/SVG.hpp
    geometry/diagnostics/SVG.cpp
    geometry/diagnostics/DebugGeom.hpp
    geometry/diagnostics/DebugGeom.cpp
    geometry/voronoi/Voronoi.hpp
    geometry/voronoi/Voronoi.cpp
    geometry/voronoi/VoronoiUtils.hpp
    geometry/voronoi/VoronoiUtils.cpp
    geometry/medial_axis/MedialAxis.hpp
    geometry/medial_axis/MedialAxis.cpp
    geometry/medial_axis/VoronoiOffset.hpp
    geometry/medial_axis/VoronoiOffset.cpp
    geometry/index/AABBTreeIndirect.hpp
    geometry/index/AABBTreeLines.hpp
    geometry/index/EdgeGrid.hpp
    geometry/index/EdgeGrid.cpp
    geometry/index/KDTreeIndirect.hpp
    geometry/index/arachne/PolygonsPointIndex.hpp
    geometry/index/arachne/PolygonsSegmentIndex.hpp
    geometry/index/athena/PolygonsPointIndex.hpp
    geometry/index/athena/PolygonsSegmentIndex.hpp
    geometry/mutable_polygon/MutablePolygon.hpp
    geometry/mutable_polygon/MutablePolygon.cpp
    geometry/region_expansion/RegionExpansion.hpp
    geometry/region_expansion/RegionExpansion.cpp
    geometry/hull/ConvexHull.hpp
    geometry/hull/ConvexHull.cpp
    geometry/circle/Circle.hpp
    geometry/circle/Circle.cpp
    geometry/predicates/linearAlg2D.hpp
    geometry/triangulate/Tesselate.hpp
    geometry/triangulate/Tesselate.cpp
    geometry/moments/PrincipalComponents2D.hpp
    geometry/moments/PrincipalComponents2D.cpp
    geometry/bridging/BridgeDetector.hpp
    geometry/serialize/ExPolygonSerialize.hpp
    geometry/adapters/boost/BoostAdapter.hpp

    # mesh
    mesh/core/TriangleMesh.hpp
    mesh/core/TriangleMesh.cpp
    mesh/core/MeshSplitImpl.hpp
    mesh/slicer/TriangleMeshSlicer.hpp
    mesh/slicer/TriangleMeshSlicer.cpp
    mesh/decimate/NormalUtils.hpp
    mesh/decimate/NormalUtils.cpp
    mesh/decimate/ShortEdgeCollapse.hpp
    mesh/decimate/ShortEdgeCollapse.cpp
    mesh/paint/TriangleSelector.hpp
    mesh/paint/TriangleSelector.cpp
    mesh/sampling/TriangleSetSampling.hpp
    mesh/sampling/TriangleSetSampling.cpp
    # AnyPtr.hpp is listed under core above: it names no engine type and includes only <memory>,
    # <type_traits> and boost/variant.hpp, so it is a container, not mesh code.

    # config
    config/model/Config.hpp
    config/model/Config.cpp
    config/model/clonable_ptr.hpp
    config/catalog/PrintConfig.hpp
    config/catalog/PrintConfig.cpp
    config/thumbnails/Thumbnails.hpp
    config/thumbnails/Thumbnails.cpp
    config/thumbnails/ThumbnailData.hpp
    config/thumbnails/ThumbnailData.cpp
    # The four thumbnail files are listed here rather than under gcode: PrintConfig.cpp and
    # Preset.cpp include Thumbnails.hpp, so listing them under gcode would make a cycle.

    # colour
    colour/rgb/Color.hpp
    colour/rgb/Color.cpp
    colour/mixing/FilamentOptics.hpp
    colour/mixing/ColorMixer.hpp
    colour/mixing/ColorMixer.cpp
    colour/dither/ColorDithering.hpp
    colour/dither/ColorDithering.cpp
    colour/palette/MixedColorPalette.hpp
    colour/palette/MixedColorPalette.cpp
    # Colour needs no library of its own: Color.hpp reaches Eigen and the bundled locale helpers
    # through geometry's Point.hpp, both already PUBLIC above, and every other include in the module
    # is a standard header.

    # toolpath
    toolpath/extrusion/ExtrusionRole.hpp
    toolpath/extrusion/ExtrusionRole.cpp
    toolpath/extrusion/ExtrusionEntity.hpp
    toolpath/extrusion/ExtrusionEntity.cpp
    toolpath/extrusion/ExtrusionEntityCollection.hpp
    toolpath/extrusion/ExtrusionEntityCollection.cpp
    toolpath/flow/Flow.hpp
    toolpath/flow/Flow.cpp
    toolpath/ordering/ShortestPath.hpp
    toolpath/ordering/ShortestPath.cpp
    toolpath/ordering/PathSorting.hpp
    toolpath/travel/TravelOptimization.hpp
    toolpath/travel/TravelOptimization.cpp
    toolpath/arc_welder/ArcWelder.hpp
    toolpath/arc_welder/ArcWelder.cpp
    toolpath/diagnostics/DebugFingerprint.hpp

    # model
    model/custom_gcode/CustomGCode.hpp
    model/custom_gcode/CustomGCode.cpp
    model/annotations/BrimEarsPoint.hpp
    model/schema/emboss/TextConfiguration.hpp
    model/schema/emboss/EmbossShape.hpp
    model/slicing/Slicing.hpp
    model/build_volume/BuildVolume.hpp
    model/edit/ModelProcessing.hpp
    model/scene/Model.hpp
    model/scene/Model.cpp
    model/beds/MultipleBeds.hpp
    model/beds/MultipleBeds.cpp
    model/slicing/SlicingAdaptive.hpp
    model/slicing/Slicing.cpp
    model/slicing/SlicingAdaptive.cpp
    model/build_volume/BuildVolume.cpp
    model/edit/ModelProcessing.cpp
    model/cut/CutUtils.hpp
    model/cut/CutUtils.cpp

    # macro
    macro/orca_aliases/OrcaGCodeAliases.hpp
    macro/orca_aliases/OrcaGCodeAliases.cpp
    macro/placeholder/PlaceholderParser.hpp
    macro/placeholder/PlaceholderParser.cpp
    macro/custom_parameters/CustomParametersHandling.hpp
    macro/custom_parameters/CustomParametersHandling.cpp

    # presets
    presets/preset/Preset.hpp
    presets/preset/Preset.cpp
    presets/bundle/PresetBundle.hpp
    presets/bundle/PresetBundle.cpp
    presets/app_config/AppConfig.hpp
    presets/app_config/AppConfig.cpp
    presets/orca/OrcaConfigImporter.hpp
    presets/orca/OrcaConfigImporter.cpp
    presets/orca/OrcaKeyMapping.hpp
    presets/orca/OrcaKeyMapping.cpp
    # No file outside this module includes any of these headers.

    # layer
    layer/print/PrintTypes.hpp
    layer/progress/ProgressConfig.hpp
    layer/progress/ProgressConfig.cpp
    layer/wipe_tower/WipeTower.hpp
    layer/tool_ordering/ToolOrdering.hpp
    layer/region/LayerRegion.hpp
    layer/model/Layer.hpp
    layer/print/PrintBase.hpp
    layer/print/PrintBase.cpp
    layer/settings_spec/SettingsSpec.hpp
    layer/settings_spec/SettingsSpec.cpp
    layer/support_spots/SupportSpotsGenerator.hpp
    layer/print/Print.hpp
    layer/print/PrintRegion.cpp
    # These headers declare members that PrintObject.cpp, Print.cpp, Layer.cpp, LayerRegion.cpp,
    # ToolOrdering.cpp, WipeTower.cpp and SupportSpotsGenerator.cpp define, under pipeline, gcode
    # and analysis, which sort above layer.

    # gizmo
    gizmo/raycast/AABBMesh.hpp
    gizmo/raycast/AABBMesh.cpp
    gizmo/csg/CSGMesh.hpp
    gizmo/csg/CSGMeshCopy.hpp
    gizmo/csg/ModelToCSGMesh.hpp
    gizmo/csg/PerformCSGMeshBooleans.hpp
    gizmo/csg/SliceCSGMesh.hpp
    gizmo/csg/TriangleMeshAdapter.hpp
    gizmo/expolygon_index/ExPolygonsIndex.hpp
    gizmo/expolygon_index/ExPolygonsIndex.cpp
    gizmo/halfedge/SurfaceMesh.hpp
    gizmo/measure/Measure.hpp
    gizmo/measure/MeasureUtils.hpp
    gizmo/measure/Measure.cpp
    gizmo/paint/TriangleSelectorWrapper.hpp
    gizmo/paint/TriangleSelectorWrapper.cpp
    gizmo/simplify/QuadricEdgeCollapse.hpp
    gizmo/simplify/QuadricEdgeCollapse.cpp
    gizmo/emboss/Emboss.hpp
    gizmo/emboss/Emboss.cpp
    gizmo/emboss/svg/NSVGUtils.hpp
    gizmo/emboss/svg/NSVGUtils.cpp
    # Engine-side computation whose only callers are interactive tools. The one console path into it
    # is an SVG import, through the emboss, svg, mesh and expolygon_index subdirectories.
    # gizmo/emboss/surface_cut/CutSurface.{hpp,cpp}, gizmo/boolean/MeshBoolean.{hpp,cpp} and
    # gizmo/emboss/mesh/Triangulation.{hpp,cpp} are deliberately absent: luminary_cgal compiles
    # those three pairs under CGAL's own options with the compile options reset, and names each one
    # in its own source list below. The firewall still checks them, because it globs this directory
    # rather than reading this list.

    # walls
    walls/arachne/beading/BeadingStrategy.hpp
    walls/arachne/beading/BeadingStrategy.cpp
    walls/arachne/beading/BeadingStrategyFactory.hpp
    walls/arachne/beading/BeadingStrategyFactory.cpp
    walls/arachne/beading/DistributedBeadingStrategy.hpp
    walls/arachne/beading/DistributedBeadingStrategy.cpp
    walls/arachne/beading/LimitedBeadingStrategy.hpp
    walls/arachne/beading/LimitedBeadingStrategy.cpp
    walls/arachne/beading/OuterWallInsetBeadingStrategy.hpp
    walls/arachne/beading/OuterWallInsetBeadingStrategy.cpp
    walls/arachne/beading/RedistributeBeadingStrategy.hpp
    walls/arachne/beading/RedistributeBeadingStrategy.cpp
    walls/arachne/beading/WideningBeadingStrategy.hpp
    walls/arachne/beading/WideningBeadingStrategy.cpp
    walls/arachne/halfedge/HalfEdge.hpp
    walls/arachne/halfedge/HalfEdgeGraph.hpp
    walls/arachne/halfedge/HalfEdgeNode.hpp
    walls/arachne/order/PerimeterOrder.hpp
    walls/arachne/order/PerimeterOrder.cpp
    walls/arachne/paths/ExtrusionJunction.hpp
    walls/arachne/paths/ExtrusionLine.hpp
    walls/arachne/paths/ExtrusionLine.cpp
    walls/arachne/paths/PolylineStitcher.hpp
    walls/arachne/paths/PolylineStitcher.cpp
    walls/arachne/predicates/linearAlg2D.hpp
    walls/arachne/skeleton/SkeletalTrapezoidation.hpp
    walls/arachne/skeleton/SkeletalTrapezoidation.cpp
    walls/arachne/skeleton/SkeletalTrapezoidationEdge.hpp
    walls/arachne/skeleton/SkeletalTrapezoidationGraph.hpp
    walls/arachne/skeleton/SkeletalTrapezoidationGraph.cpp
    walls/arachne/skeleton/SkeletalTrapezoidationJoint.hpp
    walls/arachne/spatial/SparseGrid.hpp
    walls/arachne/spatial/SparseLineGrid.hpp
    walls/arachne/spatial/SparsePointGrid.hpp
    walls/arachne/spatial/SquareGrid.hpp
    walls/arachne/spatial/SquareGrid.cpp
    walls/arachne/toolpaths/WallToolPaths.hpp
    walls/arachne/toolpaths/WallToolPaths.cpp
    walls/athena/beading/BeadingStrategy.hpp
    walls/athena/beading/BeadingStrategy.cpp
    walls/athena/beading/BeadingStrategyFactory.hpp
    walls/athena/beading/BeadingStrategyFactory.cpp
    walls/athena/beading/CenterAbsorbBeadingStrategy.hpp
    walls/athena/beading/CenterAbsorbBeadingStrategy.cpp
    walls/athena/beading/DistributedBeadingStrategy.hpp
    walls/athena/beading/DistributedBeadingStrategy.cpp
    walls/athena/beading/LimitedBeadingStrategy.hpp
    walls/athena/beading/LimitedBeadingStrategy.cpp
    walls/athena/beading/RedistributeBeadingStrategy.hpp
    walls/athena/beading/RedistributeBeadingStrategy.cpp
    walls/athena/beading/SplitPromotionBeadingStrategy.hpp
    walls/athena/beading/SplitPromotionBeadingStrategy.cpp
    walls/athena/beading/WideningBeadingStrategy.hpp
    walls/athena/beading/WideningBeadingStrategy.cpp
    walls/athena/halfedge/HalfEdge.hpp
    walls/athena/halfedge/HalfEdgeGraph.hpp
    walls/athena/halfedge/HalfEdgeNode.hpp
    walls/athena/order/PerimeterOrder.hpp
    walls/athena/order/PerimeterOrder.cpp
    walls/athena/paths/ExtrusionJunction.hpp
    walls/athena/paths/ExtrusionLine.hpp
    walls/athena/paths/ExtrusionLine.cpp
    walls/athena/paths/PolylineStitcher.hpp
    walls/athena/paths/PolylineStitcher.cpp
    walls/athena/skeleton/SkeletalTrapezoidation.hpp
    walls/athena/skeleton/SkeletalTrapezoidation.cpp
    walls/athena/skeleton/SkeletalTrapezoidationEdge.hpp
    walls/athena/skeleton/SkeletalTrapezoidationGraph.hpp
    walls/athena/skeleton/SkeletalTrapezoidationGraph.cpp
    walls/athena/skeleton/SkeletalTrapezoidationJoint.hpp
    walls/athena/spatial/SparseGrid.hpp
    walls/athena/spatial/SparseLineGrid.hpp
    walls/athena/spatial/SparsePointGrid.hpp
    walls/athena/spatial/SquareGrid.hpp
    walls/athena/spatial/SquareGrid.cpp
    walls/athena/toolpaths/WallToolPaths.hpp
    walls/athena/toolpaths/WallToolPaths.cpp
    walls/fuzzy_skin/ExtrusionLineSplits.hpp
    walls/fuzzy_skin/FuzzySkin.hpp
    walls/fuzzy_skin/FuzzySkin.cpp
    walls/fuzzy_skin/NoiseGenerator.hpp
    walls/perimeter/PerimeterGenerator.hpp
    walls/perimeter/PerimeterGenerator.cpp
    walls/precise/PreciseWalls.hpp
    walls/precise/PreciseWalls.cpp
    walls/segmentation/LineSegmentation.hpp
    walls/segmentation/LineSegmentation.cpp
    walls/serpentine/Serpentine.hpp
    walls/serpentine/Serpentine.cpp
    # The whole module, 89 files: both wall generators, the perimeter driver that chooses between
    # them, and the three features built on that driver. The two engines are laid out under
    # identical subdirectory names so they can be diffed directory by directory, each whole and with
    # its own types.

    # fill
    fill/contract/FillBase.hpp
    fill/contract/FillBase.cpp
    fill/orchestration/Fill.cpp
    fill/rectilinear/FillRectilinear.hpp
    fill/rectilinear/FillRectilinear.cpp
    fill/adaptive/FillAdaptive.hpp
    fill/adaptive/FillAdaptive.cpp
    fill/concentric/FillConcentric.hpp
    fill/concentric/FillConcentric.cpp
    fill/ensuring/FillEnsuring.hpp
    fill/ensuring/FillEnsuring.cpp
    fill/planepath/FillPlanePath.hpp
    fill/planepath/FillPlanePath.cpp
    fill/line/FillLine.hpp
    fill/line/FillLine.cpp
    fill/patterns/procedural/Fill3DHoneycomb.hpp
    fill/patterns/procedural/Fill3DHoneycomb.cpp
    fill/patterns/procedural/FillGyroid.hpp
    fill/patterns/procedural/FillGyroid.cpp
    fill/patterns/procedural/FillHoneycomb.hpp
    fill/patterns/procedural/FillHoneycomb.cpp
    fill/patterns/lightning/FillLightning.hpp
    fill/patterns/lightning/FillLightning.cpp
    fill/lightning/DistanceField.hpp
    fill/lightning/DistanceField.cpp
    fill/lightning/Generator.hpp
    fill/lightning/Generator.cpp
    fill/lightning/Layer.hpp
    fill/lightning/Layer.cpp
    fill/lightning/TreeNode.hpp
    fill/lightning/TreeNode.cpp
    # The whole module, 31 files: the Fill contract and the registry that builds every pattern,
    # the per-surface orchestration, and eleven patterns. The two `lightning` directories are not a
    # duplication: fill/lightning is the eight-file generator, which Baobab supports reach through
    # SupportCommon.cpp, while fill/patterns/lightning is the two-file Fill subclass that
    # fill_pattern = lightning selects.

    # arrange
    arrange/sequential/seq_defs.hpp
    arrange/sequential/seq_interface.hpp
    arrange/sequential/seq_interface.cpp
    arrange/sequential/seq_preprocess.hpp
    arrange/sequential/seq_preprocess.cpp
    arrange/sequential/seq_sequential.hpp
    arrange/sequential/seq_sequential.cpp
    arrange/sequential/seq_utilities.hpp
    arrange/sequential/seq_utilities.cpp
    arrange/helper/ArrangeHelper.hpp
    arrange/helper/ArrangeHelper.cpp
    arrange/helper/MinAreaBoundingBox.hpp
    arrange/helper/MinAreaBoundingBox.cpp
    arrange/helper/Optimizer.hpp
    arrange/helper/NLoptOptimizer.hpp
    arrange/packing/ArrangeBase.hpp
    arrange/packing/ArrangeFirstFit.hpp
    arrange/packing/ArrangeItemTraits.hpp
    arrange/packing/Beds.hpp
    arrange/packing/Beds.cpp
    arrange/packing/CircularEdgeIterator.hpp
    arrange/packing/DataStoreTraits.hpp
    arrange/packing/EdgeCache.hpp
    arrange/packing/EdgeCache.cpp
    arrange/packing/GravityKernel.hpp
    arrange/packing/KernelTraits.hpp
    arrange/packing/KernelUtils.hpp
    arrange/packing/NFP.hpp
    arrange/packing/NFP.cpp
    arrange/packing/NFPArrangeItemTraits.hpp
    arrange/packing/NFPConcave_Tesselate.hpp
    arrange/packing/NFPConcave_Tesselate.cpp
    arrange/packing/PackStrategyNFP.hpp
    arrange/packing/PackingContext.hpp
    arrange/packing/RectangleOverfitKernelWrapper.hpp
    arrange/packing/RectangleOverfitPackingStrategy.hpp
    arrange/packing/SVGDebugOutputKernelWrapper.hpp
    arrange/packing/TMArrangeKernel.hpp
    arrange/scene/ArbitraryDataStore.hpp
    arrange/scene/ArrangeItem.hpp
    arrange/scene/ArrangeItem.cpp
    arrange/scene/ArrangeSettingsDb_AppCfg.hpp
    arrange/scene/ArrangeSettingsDb_AppCfg.cpp
    arrange/scene/ArrangeSettingsView.hpp
    arrange/scene/ArrangeTask.hpp
    arrange/scene/ArrangeTaskImpl.hpp
    arrange/scene/FillBedTask.hpp
    arrange/scene/FillBedTaskImpl.hpp
    arrange/scene/ModelArrange.hpp
    arrange/scene/ModelArrange.cpp
    arrange/scene/MultiplySelectionTask.hpp
    arrange/scene/MultiplySelectionTaskImpl.hpp
    arrange/scene/MutableItemTraits.hpp
    arrange/scene/Scene.hpp
    arrange/scene/Scene.cpp
    arrange/scene/SceneBuilder.hpp
    arrange/scene/SceneBuilder.cpp
    arrange/scene/SegmentedRectangleBed.hpp
    arrange/scene/SimpleArrangeItem.hpp
    arrange/scene/SimpleArrangeItem.cpp
    arrange/scene/TrafoOnlyArrangeItem.hpp
    arrange/Arrange.hpp
    arrange/ArrangeImpl.hpp
    # The whole module, 63 files in five placements: the sequential-print SMT solver over Z3
    # (sequential), the six engine-side files the rest of the module shares (helper), the
    # no-fit-polygon packer (packing), the model-facing half that turns a Model into an arrangement
    # (scene), and the two front-door headers at the module root, which are the only two a consumer
    # outside this directory ever names.

    # analysis
    analysis/extrusion_annotation/ExtrusionProcessor.hpp
    analysis/extrusion_annotation/ExtrusionProcessor.cpp
    analysis/support_spots/SupportSpotsGenerator.cpp
    # Object stability physics, filed by its consumers rather than by the word in its name: the
    # extrusion annotation half computes the overhang split and the dynamic speed and fan tables the
    # G-code path reads, and the support spots half produces the curled-line estimate the travel
    # planner avoids plus the support points the alert step and the painting gizmo consume. The
    # module's header for the second half is layer/support_spots/SupportSpotsGenerator.hpp, because
    # Print.hpp includes it.

    # slice
    slice/elephant_foot/ElephantFootCompensation.hpp
    slice/elephant_foot/ElephantFootCompensation.cpp
    slice/interlocking/InterlockingGenerator.hpp
    slice/interlocking/InterlockingGenerator.cpp
    slice/interlocking/VoxelUtils.hpp
    slice/interlocking/VoxelUtils.cpp
    slice/mm_segmentation/MultiMaterialSegmentation.hpp
    slice/mm_segmentation/MultiMaterialSegmentation.cpp
    # The front of the pipeline: first-layer elephant foot compensation, the voxel interlocking
    # generator that keys two adjacent materials together, and the painted-facet segmentation that
    # partitions each layer into per-colour regions for multi-material, fuzzy skin and colour
    # mixing. The three sub-modules share no include line at all; PrintObjectSlice.cpp calls into
    # all three and belongs to pipeline. The elephant foot pair is polygon offsetting and sits here
    # rather than with geometry because it takes a Flow, whose module sorts above geometry.

    # supports
    supports/model/SupportLayer.hpp
    supports/model/SupportParameters.hpp
    supports/model/SupportParameters.cpp
    supports/backend/SupportCommon.hpp
    supports/backend/SupportCommon.cpp
    supports/classic/SupportMaterial.hpp
    supports/classic/SupportMaterial.cpp
    supports/baobab/BaobabSupport.hpp
    supports/baobab/BaobabSupport.cpp
    supports/tree/TreeSupportCommon.hpp
    supports/tree/TreeSupportCommon.cpp
    supports/tree/TreeModelVolumes.hpp
    supports/tree/TreeModelVolumes.cpp
    supports/tree/TreeOpenings.hpp
    supports/tree/TreeOpenings.cpp
    supports/tree/TreeSupport.hpp
    supports/tree/TreeSupport.cpp
    supports/organic/OrganicSupport.hpp
    supports/organic/OrganicSupport.cpp
    # The four support engines over one shared layer model and one shared back end. model holds the
    # layer storage and the derived parameters both engines read; backend turns assembled support
    # layers into extrusions and is on the critical path of all four; classic is the grid and snug
    # generator; tree is the inherited organic tree with its collision cache and shared settings;
    # organic is the drawer that turns tree elements into polygons; baobab is preFlight's own tree
    # style, which dispatches through the tree pipeline and the organic drawer. PrintObject.cpp is
    # the module's only consumer anywhere in the tree; every other includer of these headers is a
    # file of this module.

    # gcode
    gcode/emit/GCode.hpp
    gcode/emit/GCode.cpp
    gcode/emit/LabelObjects.hpp
    gcode/emit/LabelObjects.cpp
    gcode/emit/PrintExtents.hpp
    gcode/emit/PrintExtents.cpp
    gcode/emit/order/ExtrusionOrder.hpp
    gcode/emit/order/ExtrusionOrder.cpp
    gcode/emit/path/SmoothPath.hpp
    gcode/emit/path/SmoothPath.cpp
    gcode/emit/path/Wipe.hpp
    gcode/emit/path/Wipe.cpp
    gcode/emit/travel/AvoidCrossingPerimeters.hpp
    gcode/emit/travel/AvoidCrossingPerimeters.cpp
    gcode/emit/travel/JumpPointSearch.hpp
    gcode/emit/travel/JumpPointSearch.cpp
    gcode/emit/travel/RetractWhenCrossingPerimeters.hpp
    gcode/emit/travel/RetractWhenCrossingPerimeters.cpp
    gcode/emit/travel/Travels.hpp
    gcode/emit/travel/Travels.cpp
    gcode/writer/GCodeWriter.hpp
    gcode/writer/GCodeWriter.cpp
    gcode/filters/CoolingBuffer.hpp
    gcode/filters/CoolingBuffer.cpp
    gcode/filters/FanRampEstimator.hpp
    gcode/filters/FanRampEstimator.cpp
    gcode/filters/FindReplace.hpp
    gcode/filters/FindReplace.cpp
    gcode/filters/PressureEqualizer.hpp
    gcode/filters/PressureEqualizer.cpp
    gcode/filters/SpiralVase.hpp
    gcode/filters/SpiralVase.cpp
    gcode/seams/SeamAligned.hpp
    gcode/seams/SeamAligned.cpp
    gcode/seams/SeamChoice.hpp
    gcode/seams/SeamChoice.cpp
    gcode/seams/SeamGeometry.hpp
    gcode/seams/SeamGeometry.cpp
    gcode/seams/SeamPerimeters.hpp
    gcode/seams/SeamPerimeters.cpp
    gcode/seams/SeamPlacer.hpp
    gcode/seams/SeamPlacer.cpp
    gcode/seams/SeamRandom.hpp
    gcode/seams/SeamRandom.cpp
    gcode/seams/SeamRear.hpp
    gcode/seams/SeamRear.cpp
    gcode/seams/SeamScarf.hpp
    gcode/seams/SeamScarf.cpp
    gcode/seams/SeamShells.hpp
    gcode/seams/SeamShells.cpp
    gcode/seams/model_queries/ModelVisibility.hpp
    gcode/seams/model_queries/ModelVisibility.cpp
    gcode/seams/model_queries/SeamPainting.hpp
    gcode/seams/model_queries/SeamPainting.cpp
    gcode/interpret/GCodeObject.hpp
    gcode/interpret/GCodeProcessor.hpp
    gcode/interpret/GCodeProcessor.cpp
    gcode/interpret/GCodeReader.hpp
    gcode/interpret/GCodeReader.cpp
    gcode/tools/ToolOrdering.cpp
    gcode/wipe_tower/WipeTower.cpp
    gcode/wipe_tower/WipeTowerIntegration.hpp
    gcode/wipe_tower/WipeTowerIntegration.cpp
    gcode/extruder/Extruder.hpp
    gcode/extruder/Extruder.cpp
    gcode/postprocess/PostProcessor.hpp
    gcode/postprocess/PostProcessor.cpp
    # Ordered extrusions to text, and text back to records. emit walks the layers and writes the
    # file, with order deciding what is printed when, path smoothing and wiping each extrusion and
    # travel routing the moves between them; writer formats one G-code line; filters are the passes
    # that rewrite the stream after it is written, cooling, fan ramps, spiral vase, find and replace
    # and the pressure equalizer; seams choose where each loop starts, over the model queries that
    # answer visibility and painting; interpret reads a G-code file back into records and is what
    # the preview and the time estimate are built on; tools orders the extruders of a layer and
    # wipe_tower prints the purge block between tool changes; extruder is the per-extruder state the
    # writer keeps; postprocess runs the user's scripts over the finished file. Eight of these
    # headers are named from outside the module, by Print.cpp, PrintObject.cpp, the two format
    # readers, ConflictChecker, src/CLI and src/DSKY; the other twenty-six are named only from
    # inside it.
    # gcode/tools/ToolOrdering.cpp and gcode/wipe_tower/WipeTower.cpp are listed here while their
    # headers sit under layer.

    # pipeline
    pipeline/apply/PrintApply.cpp
    pipeline/bridging/InfillAboveBridges.hpp
    pipeline/bridging/InfillAboveBridges.cpp
    pipeline/brim/Brim.hpp
    pipeline/brim/Brim.cpp
    pipeline/layer/Layer.cpp
    pipeline/object/PrintObject.cpp
    pipeline/print/Print.cpp
    pipeline/region/LayerRegion.cpp
    pipeline/slice/PrintObjectSlice.cpp
    pipeline/validation/ConflictChecker.hpp
    pipeline/validation/ConflictChecker.cpp
    # The step machine that drives every other module. print is Print::process and the skirt, wipe
    # tower and validation rules around it; object is the eight step runners and the object-side
    # invalidation table; apply is the config diff that decides what a re-slice has to redo; slice is
    # the front of the pipeline, from mesh to per-region slices; layer and region are the per-layer
    # and per-region artifact bodies; brim is the first-layer adhesion stage; bridging separates the
    # infill that sits above a bridge; validation is the conflict checker that reports two objects
    # occupying the same place at the same height.
    # pipeline/layer/Layer.cpp, pipeline/object/PrintObject.cpp, pipeline/print/Print.cpp and
    # pipeline/region/LayerRegion.cpp define members that layer/model/Layer.hpp,
    # layer/print/Print.hpp and layer/region/LayerRegion.hpp declare, so the declaration and the
    # definition are in the same archive.

    # format
    format/amf/AMF.hpp
    format/amf/AMF.cpp
    format/mesh/OBJ.hpp
    format/mesh/OBJ.cpp
    format/mesh/objparser.hpp
    format/mesh/objparser.cpp
    format/mesh/STEP.hpp
    format/mesh/STEP.cpp
    format/mesh/STL.hpp
    format/mesh/STL.cpp
    format/reader/FileReader.hpp
    format/reader/FileReader.cpp
    format/svg/SVG.hpp
    format/svg/SVG.cpp
    format/threemf/3mf.hpp
    format/threemf/3mf.cpp
    # The file readers and writers that fill the model and serialise it back. reader is the single
    # entry point every consumer calls to open a file; threemf is the project archive, read on open
    # and written on save; mesh holds the three geometry formats and the OBJ text parser; svg is the
    # outline reader the emboss shape builder consumes; amf is the legacy XML project format. What
    # decides what this module does is the file extension and not a configuration option.

    # geometry/voronoi/VoronoiUtilsCgal.{hpp,cpp} and geometry/intersections/IntersectionPoints.{hpp,cpp}
    # are deliberately absent: luminary_cgal compiles those two pairs under CGAL's own options and
    # names each .cpp in its own source list below. The firewall still checks them, because it globs
    # this directory rather than reading this list.
)

# The macOS arm of platform/paths/Paths.hpp's GetDataDir. It is the engine's only Objective-C++
# source and it compiles on Apple alone, so its row is appended under a guard rather than written
# into the # platform group above.
if (APPLE)
    list(APPEND LUMINARY_SOURCES
        platform/paths/MacUtils.mm
    )
endif ()

# The Python pre-processor is an optional feature and its binding translation unit only compiles
# when the embedded interpreter is built, so its two files are appended under the same guard that
# carries the compile definition.
if (PREFLIGHT_PYTHON_PREPROCESSOR)
    list(APPEND LUMINARY_SOURCES
        gcode/scripting/PreProcessor.hpp
        gcode/scripting/PreProcessor.cpp
    )
endif ()

# The engine's compile definitions, shared by every target that compiles engine headers. Some of
# them change a class layout inside a header (PREFLIGHT_PYTHON_PREPROCESSOR adds data members to
# Print and GCodeProcessor), so a translation unit compiled without them disagrees with the rest of
# the DLL about the size of objects both sides pass by pointer, and nothing in the output shows it.
# luminary_cgal links this target too, and src/CMakeLists.txt asserts at configure time that every
# public definition either engine target sets is listed here.
add_library(preflight_engine_flags INTERFACE)
target_compile_definitions(preflight_engine_flags INTERFACE USE_TBB TBB_USE_CAPTURED_EXCEPTION=0)
if (WIN32)
    target_compile_definitions(preflight_engine_flags INTERFACE NOMINMAX)
endif ()
if (PREFLIGHT_PYTHON_PREPROCESSOR)
    target_compile_definitions(preflight_engine_flags INTERFACE PREFLIGHT_PYTHON_PREPROCESSOR
        PREFLIGHT_PYTHON_VERSION_TAG="${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")
endif ()

add_library(luminary STATIC ${LUMINARY_SOURCES})
# src/ so every consumer spells an engine header luminary/<module>/<file>, and the binary directory
# so the generated PreFlightVersion.h is reachable by bare name from both trees.
target_include_directories(luminary PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(luminary PUBLIC cxx_std_20)
target_link_libraries(luminary PUBLIC preflight_engine_flags)
# What core's own files need: boost for
# Semver's optional and format and for Timer's log, cereal for ObjectID's serialisation, the
# bundled semver parser Semver wraps, and TBB for the parallel execution policy. PUBLIC because
# every one of them is named in a header a consumer includes.
target_link_libraries(luminary PUBLIC boost_libs libcereal semver TBB::tbb)
# What platform's own files need: TBB's malloc proxy that the worker policy sizes against. PUBLIC
# because supports/model/SupportLayer.hpp declares its layer storage over tbb::scalable_allocator.
target_link_libraries(luminary PUBLIC TBB::tbbmalloc)
# What io's own files need. miniz is PUBLIC because miniz_extension.hpp includes <miniz.h> and
# declares an mz_zip_archive member, so every consumer of that header needs miniz's include
# directory; libpng is PRIVATE because only PNGReadWrite.cpp names a libpng symbol.
target_link_libraries(luminary PUBLIC miniz)
target_link_libraries(luminary PRIVATE PNG::PNG)
# What geometry's own files need. PUBLIC where a header names the library, so every consumer
# of that header gets its include directory: Eigen for the vector types Point, Geometry, MultiPoint
# and Circle build on, clipper2 for the boolean and offset wrappers ClipperUtils.hpp declares, and
# the bundled locale helpers Point.hpp includes by bare name. PRIVATE where only a .cpp names it:
# the bundled 128 bit arithmetic Point.cpp uses for its exact predicates. The hash map
# ExPolygon.cpp and Polygon.cpp deduplicate with is the same ankerl the toolpath block links,
# PUBLIC there because a toolpath header names it.
target_link_libraries(luminary PUBLIC Eigen3::Eigen clipper2 localesutils)
target_link_libraries(luminary PRIVATE int128)
# The rest of geometry, with the leaves. admesh because Tesselate.hpp declares its triangulators
# over stl types, and libigl because AABBTreeIndirect.hpp takes an igl::Hit by reference: both are
# named from a header, so both are PUBLIC. glu-libtess is the tessellator Tesselate.cpp calls and
# no header names it, so it is PRIVATE.
target_link_libraries(luminary PUBLIC admesh libigl)
target_link_libraries(luminary PRIVATE glu-libtess)
# What mesh's own files need. qhull is the convex hull TriangleMesh.cpp builds through six
# libqhullcpp headers and one libqhull_r header; no header names it, so it is PRIVATE. Its
# other libraries are linked above: admesh from four mesh headers, ankerl and TBB's scalable
# allocator from two translation units, boost, cereal and Eigen from the container headers.
target_link_libraries(luminary PRIVATE qhull)
# What config's own files need. LibBGCode is PUBLIC because config/thumbnails/Thumbnails.hpp names
# bgcode::binarize::ThumbnailBlock in a template a consumer instantiates, so every consumer of that
# header needs its include directory; qoi and libjpeg are PRIVATE because only Thumbnails.cpp names
# an encoder symbol. Its other libraries are already linked above: miniz for the PNG encoder and the
# zip helper, localesutils for the bare LocalesUtils.hpp both Config files include, and boost,
# cereal and Eigen from the option model.
target_link_libraries(luminary PUBLIC LibBGCode::bgcode_convert)
target_link_libraries(luminary PRIVATE qoi JPEG::JPEG)
# What toolpath's own files need. ankerl is PUBLIC because the header
# toolpath/ordering/PathSorting.hpp includes ankerl/unordered_dense.h itself, so every consumer of
# that header needs its include directory. Its other libraries are linked above: Eigen from
# ArcWelder.hpp and boost from ArcWelder.cpp.
target_link_libraries(luminary PUBLIC ankerl)
# layer/print/Print.hpp includes tcbspan/span.hpp itself and declares SpanOfConstPtrs over
# tcb::span, so every consumer of that header needs its include directory: PUBLIC.
target_link_libraries(luminary PUBLIC tcbspan)
# What model's own files need. NanoSVG is PUBLIC because the header
# model/schema/emboss/EmbossShape.hpp includes nanosvg/nanosvg.h itself and holds a
# std::shared_ptr<NSVGimage>, so every consumer of that header needs its include directory. Its
# other libraries are linked above: admesh from BuildVolume.hpp, cereal and
# Eigen from the schema headers, and boost and TBB from the translation units.
target_link_libraries(luminary PUBLIC NanoSVG::nanosvg)
# What macro's and presets' own files need. The target a find_package creates is visible only in
# the directory that made the call and below, so the resolution is hoisted to the top-level
# CMakeLists.txt. PUBLIC rather than PRIVATE because the header
# presets/orca/OrcaConfigImporter.hpp includes nlohmann/json.hpp itself and holds a nlohmann::json
# member, so every consumer of that header needs its include directory; macro's use is a single
# translation unit and would be satisfied either way. The rest of both modules is
# linked above: boost for the spirit grammar, regex and nowide, miniz for the Orca bundle reader,
# localesutils for the bare LocalesUtils.hpp, and Eigen, cereal and TBB through the option model.
target_link_libraries(luminary PUBLIC nlohmann_json::nlohmann_json)
# What gizmo's own files need: the bundled header-only stb_truetype font parser, which
# gizmo/emboss/Emboss.cpp includes by bare name and whose implementation it compiles
# (STB_TRUETYPE_IMPLEMENTATION). PRIVATE, because no header names it. The target is an
# INTERFACE library carrying only an include directory and bundled_deps is added before src, so it
# needs no hoist. Everything else the module names is linked above: libigl for the igl::Hit
# the raycast and paint files take, NanoSVG for the svg pair, admesh, Eigen, boost and TBB.
target_link_libraries(luminary PRIVATE stb_truetype)
# What arrange's own files need. Z3 is the SMT solver arrange/sequential/ builds its model over;
# only the four translation units of that folder name a Z3 type and no header outside them does, so
# it is PRIVATE. It is resolved in the top-level CMakeLists.txt behind a target guard, because
# CMake processes this directory first.
# libnest2d supplies the rotating-calipers implementation arrange/helper/MinAreaBoundingBox.cpp
# calls and the geometry traits it instantiates them over; no header names it, so it is
# PRIVATE. Its own INTERFACE link names this target, which is why it can only be linked from here.
# The packer and the scene half add nothing: the only prefixed include either of them carries is
# boost, already PUBLIC above.
target_link_libraries(luminary PRIVATE z3::libz3 libnest2d)
# What supports' own files need. agg is the bundled anti-aliased rasterizer
# supports/classic/SupportMaterial.cpp rasterizes its support grid with, seven include lines under
# SUPPORT_USE_AGG_RASTERIZER; PRIVATE because no header names it. It is a header-only
# INTERFACE target created by add_subdirectory(bundled_deps), which the top-level CMakeLists.txt
# processes before src, so it needs no target guard. Everything else the module names is
# linked above: boost for its logging and containers, TBB for the parallel loops and the scalable
# allocator, admesh for the stl types the organic drawer tessellates, tcbspan for the span the
# classic generator takes, and Eigen and clipper2 through the geometry headers.
target_link_libraries(luminary PRIVATE agg)
# What gcode's own files need. fastfloat supplies the include directory for the bare fast_float.h
# that gcode/filters/CoolingBuffer.cpp, gcode/filters/PressureEqualizer.cpp and
# gcode/interpret/GCodeReader.cpp name for fast_float::from_chars; PRIVATE because no header
# names it.
target_link_libraries(luminary PRIVATE fastfloat)
# What format's own files need. expat is the XML parser format/threemf/3mf.cpp and format/amf/AMF.cpp
# drive directly; PRIVATE because no header names an expat type. The INTERFACE target is
# created in the top-level CMakeLists.txt before add_subdirectory(src), so it needs no target guard.
target_link_libraries(luminary PRIVATE libexpat)
# format/mesh/STEP.cpp declares load_step_internal extern "C" and calls it directly on non-Windows,
# so the engine archive has to link the STEP wrapper there; on Windows the symbol is fetched with
# LoadLibraryW at runtime and no link is needed. The target is created by add_subdirectory further
# down src/CMakeLists.txt than this directory, which CMake resolves at generate time. With the
# option off the wrapper target does not exist, so the definition and the link go with it and
# STEP.cpp compiles to a reader that reports a failed import.
if (PREFLIGHT_ENABLE_FORMAT_STEP)
    target_compile_definitions(luminary PRIVATE PREFLIGHT_ENABLE_FORMAT_STEP)
    if (NOT WIN32)
        target_link_libraries(luminary PUBLIC OCCTWrapper)
    endif ()
endif ()
# pybind11::embed is the embedded interpreter gcode/scripting/PreProcessor.cpp links against, under
# the same guard that decides whether that translation unit is compiled at all. PRIVATE because the
# binding surface is entirely inside the .cpp.
if (PREFLIGHT_PYTHON_PREPROCESSOR)
    target_link_libraries(luminary PRIVATE pybind11::embed)

    # Generate the preFlight.py IDE stub from the binding source: the pybind11 module's docstrings
    # (types, signatures, descriptions), the C++ enum headers (numeric values) and PrintConfig.hpp
    # (Settings keys). A public binding without a conforming docstring fails this step.
    set(_pp_config "${CMAKE_SOURCE_DIR}/src/luminary/config/catalog/PrintConfig.hpp")
    set(_pp_bindings "${CMAKE_SOURCE_DIR}/src/luminary/gcode/scripting/PreProcessor.cpp")
    set(_pp_enum_headers
        "${CMAKE_SOURCE_DIR}/src/luminary/gcode/interpret/GCodeProcessor.hpp"
        "${CMAKE_SOURCE_DIR}/src/luminary/toolpath/extrusion/ExtrusionRole.hpp"
        "${CMAKE_SOURCE_DIR}/src/luminary/model/custom_gcode/CustomGCode.hpp")
    set(_pp_enum_args "")
    foreach(_hdr IN LISTS _pp_enum_headers)
        list(APPEND _pp_enum_args --enum-header "${_hdr}")
    endforeach()
    set(_pp_script "${CMAKE_SOURCE_DIR}/build_stubs.py")
    set(_pp_output "${CMAKE_SOURCE_DIR}/resources/preprocessor/preFlight.py")
    set(_pp_output2 "${CMAKE_SOURCE_DIR}/resources/preprocessor/samples/preFlight.py")
    add_custom_command(
        OUTPUT "${_pp_output}" "${_pp_output2}"
        COMMAND ${Python3_EXECUTABLE} "${_pp_script}"
                --config "${_pp_config}"
                --bindings "${_pp_bindings}"
                ${_pp_enum_args}
                --out "${CMAKE_SOURCE_DIR}/resources/preprocessor"
                --out "${CMAKE_SOURCE_DIR}/resources/preprocessor/samples"
        DEPENDS "${_pp_config}" "${_pp_bindings}" ${_pp_enum_headers} "${_pp_script}"
        COMMENT "Generating preFlight.py type stubs from the binding source"
    )
    add_custom_target(generate_settings_stubs DEPENDS "${_pp_output}" "${_pp_output2}")
    add_dependencies(luminary generate_settings_stubs)
endif ()
# Psapi is the process-information library platform/process/Process.cpp and
# platform/host/BlacklistedLibraryCheck.cpp call for the working-set and module queries. PUBLIC
# because src/DSKY/GUI/SysInfoDialog.cpp calls it as well and takes it through this interface.
if (WIN32)
    target_link_libraries(luminary PUBLIC Psapi.lib)
endif ()
# Neither is named by an include line anywhere in src/: zlib is what miniz and libpng compress
# through, and the dynamic loader library is what a platform call needs where it is not in libc.
target_link_libraries(luminary PRIVATE ZLIB::ZLIB ${CMAKE_DL_LIBS})

encoding_check(luminary)

# The engine's CGAL half. Six engine sources compile here rather than into luminary because CGAL's
# interface carries -frounding-math, and the reset below filters it out so it reaches these six and
# nothing else; folding them into luminary would put that option in reach of every engine file.
# This target takes Luminary's include directories through a target property and links neither
# luminary nor its libraries, so each library its sources need is named here.
if (PREFLIGHT_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

# Use Boost.Multiprecision instead of GMP (CGAL 6.0+)
set(CGAL_DISABLE_GMP ON CACHE BOOL "" FORCE)

cmake_policy(PUSH)
cmake_policy(SET CMP0011 NEW)
find_package(CGAL REQUIRED)
cmake_policy(POP)

add_library(luminary_cgal STATIC
    gizmo/emboss/surface_cut/CutSurface.hpp gizmo/emboss/surface_cut/CutSurface.cpp
    geometry/voronoi/VoronoiUtilsCgal.hpp geometry/voronoi/VoronoiUtilsCgal.cpp
    geometry/intersections/IntersectionPoints.hpp geometry/intersections/IntersectionPoints.cpp
    gizmo/boolean/MeshBoolean.hpp gizmo/boolean/MeshBoolean.cpp
    platform/signals/TryCatchSignal.hpp
    gizmo/emboss/mesh/Triangulation.hpp gizmo/emboss/mesh/Triangulation.cpp
)
target_include_directories(luminary_cgal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(luminary_cgal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# This target compiles engine sources that reach the Luminary prelude, so it needs Luminary's
# include directories: src/ for the luminary/<module>/ spellings and the binary directory for the
# generated PreFlightVersion.h.
target_include_directories(luminary_cgal PRIVATE $<TARGET_PROPERTY:luminary,INTERFACE_INCLUDE_DIRECTORIES>)

# Reset compile options of luminary_cgal. Despite it being linked privately, CGAL options
# (-frounding-math) still propagate to dependent libs which is not desired.
get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${_cgal_tgt})
    set (_cgal_tgt CGAL::CGAL)
endif ()
get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
if (_opts)
    set(_opts_bad "${_opts}")
    set(_opts_good "${_opts}")
    list(FILTER _opts_bad INCLUDE REGEX frounding-math)
    list(FILTER _opts_good EXCLUDE REGEX frounding-math)
    set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
    target_compile_options(luminary_cgal PRIVATE "${_opts_bad}")
endif()

target_link_libraries(luminary_cgal PRIVATE ${_cgal_tgt} libigl)
target_link_libraries(luminary_cgal PUBLIC semver admesh localesutils)
# The engine's shared compile definitions. This target compiles six engine sources, so a definition
# the rest of the DLL has and it lacks would give the two halves different class layouts;
# src/CMakeLists.txt asserts that this link exists.
target_link_libraries(luminary_cgal PUBLIC preflight_engine_flags)
# Needs clipper2 link for include paths since it includes ClipperUtils.hpp
target_link_libraries(luminary_cgal PRIVATE clipper2)
# Same reason: CutSurface.hpp includes Emboss.hpp, which includes the Luminary schema header that
# includes nanosvg/nanosvg.h. This target takes Luminary's include directories through a target
# property and links neither luminary nor its libraries, so it needs the path named here.
target_link_libraries(luminary_cgal PRIVATE NanoSVG::nanosvg)

target_compile_definitions(luminary_cgal PRIVATE CGAL_DISABLE_GMP=1)

if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
    target_compile_definitions(luminary_cgal PRIVATE CGAL_DO_NOT_USE_MPZF)
endif ()

# The six sources above are called from luminary and nothing else calls them, so the engine archive
# carries this one; the include-directory property above is what keeps it from being a cycle.
target_link_libraries(luminary PRIVATE luminary_cgal)

if (APPLE)
    # This flag prevents the need for minimum SDK version 10.14
    target_compile_options(luminary PUBLIC "-fno-aligned-allocation")
    target_compile_options(luminary_cgal PUBLIC "-fno-aligned-allocation")
endif ()

# Dependency firewall, checked at configure time over every Luminary source. The include path
# exposes all of src/, so spellings are not trusted: every include with a directory component is
# resolved against the includer's own directory and against src/, and it passes only when the file
# it names lies inside src/luminary, or its first path segment is a third-party library named
# below. The UI trees are denied by name whatever the spelling, and an unknown prefix fails too, so
# a new dependency is added here deliberately rather than slipping in. There is no exemption.
set(_luminary_third_party
    "clipper2" "Eigen" "unsupported" "tbb" "oneapi" "boost" "catch2"
    "admesh" "cereal" "CGAL" "tcbspan" "ankerl" "agg" "igl" "LibBGCode" "libqhullcpp" "libqhull_r"
    "nanosvg" "libnest2d" "pybind11" "nlohmann" "step"
    "sys" "mach" "pthread" "IOKit" "Foundation"
)
set(_luminary_denied "DSKY" "CLI" "viewer" "wx" "imgui" "GL" "glad")
get_filename_component(_src_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE _luminary_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
foreach(_src ${_luminary_sources})
    file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_src}")
    get_filename_component(_src_dir "${_src}" DIRECTORY)
    file(STRINGS "${_src}" _includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
    foreach(_line ${_includes})
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" _hdr "${_line}")
        string(REPLACE "\\" "/" _hdr "${_hdr}")
        set(_ok FALSE)
        set(_why "")
        if (NOT _hdr MATCHES "/")
            # A bare header name is the standard library or a sibling in the same directory.
            set(_ok TRUE)
        else ()
            # Resolve against the includer's directory, then against src/; a file inside
            # src/luminary passes, a file anywhere else is judged by its first segment.
            set(_resolved "")
            foreach(_base "${_src_dir}" "${_src_root}")
                if (NOT _resolved AND EXISTS "${_base}/${_hdr}")
                    get_filename_component(_resolved "${_base}/${_hdr}" ABSOLUTE)
                endif ()
            endforeach()
            if (_resolved)
                file(RELATIVE_PATH _rel_to_lum "${CMAKE_CURRENT_SOURCE_DIR}" "${_resolved}")
                if (NOT _rel_to_lum MATCHES "^\\.\\.")
                    set(_ok TRUE)
                endif ()
            endif ()
            if (NOT _ok)
                # CMake regexes have no {n,m}; spell the relative segments out.
                string(REGEX REPLACE "^(\\./|\\.\\./)+" "" _hdr_clean "${_hdr}")
                string(REGEX REPLACE "^src/" "" _hdr_clean "${_hdr_clean}")
                string(REGEX REPLACE "/.*$" "" _first "${_hdr_clean}")
                if (_first IN_LIST _luminary_denied)
                    set(_why "the UI trees are never reachable from the engine")
                elseif (_first IN_LIST _luminary_third_party)
                    set(_ok TRUE)
                else ()
                    set(_why "unknown prefix '${_first}'; add it to _luminary_third_party deliberately if it is a library")
                endif ()
            endif ()
        endif ()
        if (NOT _ok)
            message(FATAL_ERROR "Luminary boundary violation: ${_src} includes '${_hdr}': ${_why}.")
        endif ()
    endforeach()
endforeach()
