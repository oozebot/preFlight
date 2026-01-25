///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Lukas Matena @lukasmatena, Tomas Meszaros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "OCCTWrapper.hpp"

#include "occtwrapper_export.h"

#include <cassert>
#include <sstream>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"

#include "ShapeFix_Shape.hxx"
#include "ShapeFix_ShapeTolerance.hxx"
#include "IMeshTools_Parameters.hxx"
#include "BRepTools.hxx"
#include "Precision.hxx"
#include "BRepBuilderAPI_Sewing.hxx"
#include "BRepAdaptor_Surface.hxx"
#include "GeomAbs_SurfaceType.hxx"

#include "admesh/stl.h"
#include "libslic3r/Point.hpp"

namespace Slic3r
{

struct NamedSolid
{
    NamedSolid(const TopoDS_Shape &s, const std::string &n) : solid{s}, name{n} {}
    const TopoDS_Shape solid;
    const std::string name;
};

static void getNamedSolids(const TopLoc_Location &location, const Handle(XCAFDoc_ShapeTool) shapeTool,
                           const TDF_Label label, std::vector<NamedSolid> &namedSolids)
{
    TDF_Label referredLabel{label};
    if (shapeTool->IsReference(label))
        shapeTool->GetReferredShape(label, referredLabel);

    std::string name;
    Handle(TDataStd_Name) shapeName;
    if (referredLabel.FindAttribute(TDataStd_Name::GetID(), shapeName))
        name = TCollection_AsciiString(shapeName->Get()).ToCString();

    TopLoc_Location localLocation = location * shapeTool->GetLocation(label);
    TDF_LabelSequence components;
    if (shapeTool->GetComponents(referredLabel, components))
    {
        for (Standard_Integer compIndex = 1; compIndex <= components.Length(); ++compIndex)
        {
            getNamedSolids(localLocation, shapeTool, components.Value(compIndex), namedSolids);
        }
    }
    else
    {
        TopoDS_Shape shape;
        shapeTool->GetShape(referredLabel, shape);
        TopAbs_ShapeEnum shape_type = shape.ShapeType();
        BRepBuilderAPI_Transform transform(shape, localLocation, Standard_True);
        switch (shape_type)
        {
        case TopAbs_COMPOUND:
            namedSolids.emplace_back(TopoDS::Compound(transform.Shape()), name);
            break;
        case TopAbs_COMPSOLID:
            namedSolids.emplace_back(TopoDS::CompSolid(transform.Shape()), name);
            break;
        case TopAbs_SOLID:
            namedSolids.emplace_back(TopoDS::Solid(transform.Shape()), name);
            break;
        default:
            break;
        }
    }
}

static const char *getSurfaceTypeName(GeomAbs_SurfaceType surfType)
{
    switch (surfType)
    {
    case GeomAbs_Plane:
        return "Plane";
    case GeomAbs_Cylinder:
        return "Cylinder";
    case GeomAbs_Cone:
        return "Cone";
    case GeomAbs_Sphere:
        return "Sphere";
    case GeomAbs_Torus:
        return "Torus";
    case GeomAbs_BezierSurface:
        return "BezierSurface";
    case GeomAbs_BSplineSurface:
        return "BSplineSurface";
    case GeomAbs_SurfaceOfRevolution:
        return "SurfaceOfRevolution";
    case GeomAbs_SurfaceOfExtrusion:
        return "SurfaceOfExtrusion";
    case GeomAbs_OffsetSurface:
        return "OffsetSurface";
    default:
        return "Unknown";
    }
}

extern "C" OCCTWRAPPER_EXPORT bool load_step_internal(const char *path, OCCTResult *res,
                                                      std::optional<std::pair<double, double>> deflections)
{
    try
    {
        std::vector<NamedSolid> namedSolids;
        Handle(TDocStd_Document) document;
        Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
        application->NewDocument(path, document);
        STEPCAFControl_Reader reader;
        reader.SetNameMode(true);
        IFSelect_ReturnStatus stat = reader.ReadFile(path);
        if (stat != IFSelect_RetDone || !reader.Transfer(document))
        {
            application->Close(document);
            res->error_str = std::string{"Could not read '"} + path + "'";
            return false;
        }
        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        TDF_LabelSequence topLevelShapes;
        shapeTool->GetFreeShapes(topLevelShapes);

        Standard_Integer topShapeLength = topLevelShapes.Length() + 1;
        for (Standard_Integer iLabel = 1; iLabel < topShapeLength; ++iLabel)
        {
            getNamedSolids(TopLoc_Location{}, shapeTool, topLevelShapes.Value(iLabel), namedSolids);
        }

        const char *last_slash = strrchr(path, DIR_SEPARATOR);
        std::string obj_name((last_slash == nullptr) ? path : last_slash + 1);
        res->object_name = obj_name;

        int totalFailedFaces = 0;
        std::ostringstream warningStream;

        for (const NamedSolid &namedSolid : namedSolids)
        {
            // Fusion 360 exports have precision errors (e.g., 10^-13 coordinate mismatches)
            // Note: This doesn't fix all model issues (e.g., models with 271 open edges
            // may fail during STEP import)
            BRepBuilderAPI_Sewing sewing(1e-6);
            sewing.Add(namedSolid.solid);
            sewing.Perform();
            TopoDS_Shape sewedShape = sewing.SewedShape();

            ShapeFix_ShapeTolerance tolFixer;
            tolFixer.SetTolerance(sewedShape, 1e-5, TopAbs_SHAPE);

            ShapeFix_Shape shapeFixer(sewedShape);
            shapeFixer.SetPrecision(1e-5);
            shapeFixer.SetMaxTolerance(1e-3);
            shapeFixer.SetMinTolerance(1e-7);
            shapeFixer.Perform();
            TopoDS_Shape fixedShape = shapeFixer.Shape();

            BRepTools::Clean(fixedShape);

            double linearDeflection = deflections.has_value() ? deflections.value().first : 0.005;
            double angularDeflection = deflections.has_value() ? deflections.value().second : 0.00873;

            IMeshTools_Parameters meshParams;
            meshParams.Deflection = linearDeflection;
            meshParams.Angle = angularDeflection;
            meshParams.Relative = Standard_False;
            meshParams.InParallel = Standard_True;
            meshParams.MinSize = 1e-6;
            meshParams.InternalVerticesMode = Standard_True;
            meshParams.ControlSurfaceDeflection = Standard_True;
            meshParams.CleanModel = Standard_True;
            meshParams.AdjustMinSize = Standard_True;
            meshParams.AllowQualityDecrease = Standard_True;
            meshParams.ForceFaceDeflection = Standard_True;

            BRepMesh_IncrementalMesh mesh(fixedShape, meshParams);

            res->volumes.emplace_back();
            std::vector<Vec3f> vertices;
            std::vector<stl_facet> &facets = res->volumes.back().facets;

            int faceIndex = 0;

            for (TopExp_Explorer anExpSF(fixedShape, TopAbs_FACE); anExpSF.More(); anExpSF.Next())
            {
                faceIndex++;
                const TopoDS_Shape &aFace = anExpSF.Current();
                TopLoc_Location aLoc;
                Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);

                if (aTriangulation.IsNull())
                {
                    TopoDS_Face failedFace = TopoDS::Face(aFace);
                    BRepTools::Clean(failedFace);

                    IMeshTools_Parameters retryParams;
                    retryParams.Deflection = linearDeflection * 10.0;
                    retryParams.Angle = 0.5;
                    retryParams.Relative = Standard_False;
                    retryParams.InParallel = Standard_False;
                    retryParams.MinSize = Precision::Confusion();
                    retryParams.AdjustMinSize = Standard_True;
                    retryParams.AllowQualityDecrease = Standard_True;
                    retryParams.ForceFaceDeflection = Standard_True;

                    BRepMesh_IncrementalMesh retryMesh(failedFace, retryParams);
                    aTriangulation = BRep_Tool::Triangulation(failedFace, aLoc);

                    if (aTriangulation.IsNull() || aTriangulation->NbTriangles() == 0)
                    {
                        totalFailedFaces++;
                        BRepAdaptor_Surface surfaceAdaptor(failedFace);
                        warningStream << "  - Face #" << faceIndex << " ("
                                      << getSurfaceTypeName(surfaceAdaptor.GetType()) << ")\n";
                        continue;
                    }
                }

                if (aTriangulation->NbTriangles() == 0)
                    continue;

                const int aNodeOffset = int(vertices.size());
                gp_Trsf aTrsf = aLoc.Transformation();
                for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter)
                {
                    gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
                    aPnt.Transform(aTrsf);
                    vertices.emplace_back(Vec3f(float(aPnt.X()), float(aPnt.Y()), float(aPnt.Z())));
                }

                const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
                for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter)
                {
                    Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);
                    Standard_Integer anId[3];
                    aTri.Get(anId[0], anId[1], anId[2]);
                    if (anOrientation == TopAbs_REVERSED)
                        std::swap(anId[1], anId[2]);

                    stl_facet facet;
                    facet.vertex[0] = vertices[anId[0] + aNodeOffset - 1];
                    facet.vertex[1] = vertices[anId[1] + aNodeOffset - 1];
                    facet.vertex[2] = vertices[anId[2] + aNodeOffset - 1];
                    facet.normal =
                        (facet.vertex[1] - facet.vertex[0]).cross(facet.vertex[2] - facet.vertex[1]).normalized();
                    facet.extra[0] = 0;
                    facet.extra[1] = 0;
                    facets.emplace_back(std::move(facet));
                }
            }

            res->volumes.back().volume_name = namedSolid.name;
            if (vertices.empty())
                res->volumes.pop_back();
        }

        if (totalFailedFaces > 0)
        {
            res->warning_str = std::to_string(totalFailedFaces) +
                               " face(s) could not be triangulated and will be missing from the model.\n"
                               "This is typically caused by precision errors in the CAD export.\n"
                               "Try re-exporting the model from your CAD software.\n\n"
                               "Failed faces:\n" +
                               warningStream.str();
        }

        shapeTool.reset(nullptr);
        application->Close(document);

        if (res->volumes.empty())
            return false;
    }
    catch (const std::exception &ex)
    {
        res->error_str = ex.what();
        return false;
    }
    catch (...)
    {
        res->error_str = "An exception was thrown in load_step_internal.";
        return false;
    }
    return true;
}

}; // namespace Slic3r
