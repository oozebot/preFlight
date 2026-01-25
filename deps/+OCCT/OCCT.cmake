add_cmake_project(OCCT
	# Previous versions had chamfer triangulation bug (SPE-2257).
	# Fix applied in OCCTWrapper.cpp: Angular deflection changed from 1.0 rad (57 deg)
	# to FreeCAD's formula (~0.03 rad / 1.7 deg) which resolves the issue.
	# See: ToDo/occt-upgrade-and-triangulation-fix.md for full analysis
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_9_3.zip
	URL_HASH SHA256=566a236b5a22e778dbe1c3dce6560ba12db4504bbdae0354c51dd2f4728756ee

    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/occt_toolkit.cmake ./adm/cmake/
    CMAKE_ARGS
        -DINSTALL_DIR_LAYOUT=Unix
        -DBUILD_LIBRARY_TYPE=Static
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
        -DUSE_FREETYPE=OFF
        -DUSE_FFMPEG=OFF
        -DUSE_VTK=OFF
        # Modules needed for STEP import (OCCTWrapper.cpp):
        # - FoundationClasses: Core types (TDF_*, TCollection_*)
        # - ModelingData: Shape types (TopoDS_*, TopExp_*, BRep_Tool)
        # - ModelingAlgorithms: Mesh/shape ops (BRepMesh_*, ShapeFix_*, BRepBuilderAPI_*)
        # - ApplicationFramework: XCAF (XCAFDoc_*, XCAFApp_*)
        # - DataExchange: STEP reader (STEPCAFControl_Reader)
        -DBUILD_MODULE_Draw=OFF
        -DBUILD_MODULE_Visualization=OFF
)
