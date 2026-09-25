#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
#/|/ Copyright (c) 2023 Pedro Lamas @PedroLamas
#/|/ Copyright (c) 2020 Sergey Kovalev @RandoMan70
#/|/ Copyright (c) 2021 Boleslaw Ciesielski
#/|/ Copyright (c) 2019 Spencer Owen @spuder
#/|/ Copyright (c) 2019 Stephan Reichhelm @stephanr
#/|/
#/|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(dsky)

include(PrecompiledHeader)

set(PREFLIGHT_GUI_SOURCES
    pchheader.cpp
    pchheader.hpp
    GUI/AboutDialog.cpp
    GUI/AboutDialog.hpp
    GUI/ArrangeSettingsDialogImgui.hpp
    GUI/ArrangeSettingsDialogImgui.cpp
    # Removed UserAccount files (Prusa services)
    # Removed Connect WebView files (Prusa services)
    GUI/WebView.cpp
    GUI/WebView.hpp
    GUI/WebViewPlatformUtils.hpp
    GUI/SysInfoDialog.cpp
    GUI/SysInfoDialog.hpp
    GUI/KBShortcutsDialog.cpp
    GUI/KBShortcutsDialog.hpp
    GUI/BackgroundSlicingProcess.cpp
    GUI/BackgroundSlicingProcess.hpp
    GUI/BitmapCache.cpp
    GUI/BitmapCache.hpp
    GUI/ConfigSnapshotDialog.cpp
    GUI/ConfigSnapshotDialog.hpp
    GUI/3DScene.cpp
    GUI/3DScene.hpp
    GUI/format.hpp
    GUI/GLShadersManager.hpp
    GUI/GLShadersManager.cpp
    GUI/ScenePasses.hpp
    GUI/ScenePasses.cpp
    GUI/GLShader.cpp
    GUI/GLShader.hpp    
    GUI/EventTypes.hpp
    GUI/EventBridge.hpp
    GUI/EventBridge_wx.hpp
    GUI/EventBridge_wx.cpp
    GUI/InputEvents.hpp
    GUI/InputEvents_wx.hpp
    GUI/GLSurface.hpp
    GUI/GLSurface_wx.hpp
    GUI/GLSurface_wx.cpp
    GUI/ITimer.hpp
    GUI/Timer_wx.hpp
    GUI/Timer_wx.cpp
    GUI/GLCanvas3D.hpp
    GUI/GLCanvas3D.cpp
    GUI/LayerHeightTexture.hpp
    GUI/LayerHeightTexture.cpp
    GUI/Technologies.hpp
    GUI/SceneRaycaster.hpp
    GUI/SceneRaycaster.cpp	
    GUI/OpenGLManager.hpp
    GUI/OpenGLManager.cpp
    GUI/Selection.hpp
    GUI/Selection.cpp    
    GUI/Gizmos/GLGizmosManager.cpp
    GUI/Gizmos/GLGizmosManager.hpp
    GUI/Gizmos/GLGizmosCommon.cpp
    GUI/Gizmos/GLGizmosCommon.hpp
    GUI/Gizmos/GLGizmoBase.cpp
    GUI/Gizmos/GLGizmoBase.hpp
    GUI/Gizmos/GLGizmoEmboss.cpp
    GUI/Gizmos/GLGizmoEmboss.hpp
    GUI/Gizmos/GLGizmoMove.cpp
    GUI/Gizmos/GLGizmoMove.hpp
    GUI/Gizmos/GLGizmoRotate.cpp
    GUI/Gizmos/GLGizmoRotate.hpp
    GUI/Gizmos/GLGizmoScale.cpp
    GUI/Gizmos/GLGizmoScale.hpp
    GUI/Gizmos/GLGizmoFdmSupports.cpp
    GUI/Gizmos/GLGizmoFdmSupports.hpp
    GUI/Gizmos/GLGizmoFuzzySkin.cpp
    GUI/Gizmos/GLGizmoFuzzySkin.hpp
    GUI/Gizmos/GLGizmoBrimEars.cpp
    GUI/Gizmos/GLGizmoBrimEars.hpp
    GUI/Gizmos/GLGizmoFlatten.cpp
    GUI/Gizmos/GLGizmoFlatten.hpp
    GUI/Gizmos/GLGizmoCut.cpp
    GUI/Gizmos/GLGizmoCut.hpp
    GUI/Gizmos/GLGizmoPainterBase.cpp
    GUI/Gizmos/GLGizmoPainterBase.hpp
    GUI/Gizmos/GLGizmoSeam.cpp
    GUI/Gizmos/GLGizmoSeam.hpp
    GUI/Gizmos/GLGizmoSimplify.cpp
    GUI/Gizmos/GLGizmoSimplify.hpp
    GUI/Gizmos/GLGizmoSVG.cpp
    GUI/Gizmos/GLGizmoSVG.hpp
    GUI/Gizmos/TriangleSelectorMmGui.cpp
    GUI/Gizmos/TriangleSelectorMmGui.hpp
    GUI/Gizmos/GLGizmoMeasure.cpp
    GUI/Gizmos/GLGizmoMeasure.hpp
    GUI/Gizmos/GLGizmoAlign.cpp
    GUI/Gizmos/GLGizmoAlign.hpp
    GUI/Gizmos/GLGizmoRelief.cpp
    GUI/Gizmos/GLGizmoRelief.hpp
    GUI/Gizmos/GLGizmoCounterboreBridge.cpp
    GUI/Gizmos/GLGizmoCounterboreBridge.hpp
    GUI/Gizmos/GLGizmoColorMixing.cpp
    GUI/Gizmos/GLGizmoColorMixing.hpp
    GUI/CSGPreviewManager.cpp
    GUI/CSGPreviewManager.hpp
    GUI/LibVGCode/LibVGCodeWrapper.hpp
    GUI/LibVGCode/LibVGCodeWrapper.cpp
    GUI/GLSelectionRectangle.cpp
    GUI/GLSelectionRectangle.hpp
    GUI/GLModel.hpp
    GUI/GLModel.cpp
    GUI/GLTexture.hpp
    GUI/GLTexture.cpp
    GUI/GLToolbar.hpp
    GUI/GLToolbar.cpp
    GUI/GCodeViewer.hpp
    GUI/GCodeViewer.cpp
    GUI/preFlight.PreviewClipController.hpp
    GUI/preFlight.PreviewClipController.cpp
    GUI/ModernTabBar.hpp
    GUI/ModernTabBar.cpp
    GUI/PrinterWebViewPanel.hpp
    GUI/PrinterWebViewPanel.cpp
    GUI/PrinterConnectionChecker.hpp
    GUI/PrinterConnectionChecker.cpp
    GUI/Preferences.cpp
    GUI/Preferences.hpp
    GUI/PresetHints.cpp
    GUI/PresetHints.hpp
    GUI/GUI.cpp
    GUI/GUI.hpp
    GUI/GUI_Init.cpp
    GUI/GUI_Init.hpp
    GUI/GUI_Preview.cpp
    GUI/GUI_Preview.hpp
    GUI/GUI_App.cpp
    GUI/GUI_App.hpp
    GUI/DarkMode.cpp
    GUI/DarkMode.hpp
    GUI/GUI_Utils.cpp
    GUI/GUI_Utils.hpp
    GUI/Theme.cpp
    GUI/Theme.hpp
    GUI/ThemePalette.cpp
    GUI/ThemePalette.hpp
    GUI/GUI_Geometry.cpp
    GUI/GUI_Geometry.hpp
    GUI/I18N.cpp
    GUI/I18N.hpp
    GUI/IconManager.cpp
    GUI/IconManager.hpp
    GUI/MainFrame.cpp
    GUI/MainFrame.hpp
    GUI/OrcaImportDialog.cpp
    GUI/OrcaImportDialog.hpp
    GUI/UpdatesUIManager.cpp
    GUI/UpdatesUIManager.hpp
    GUI/FrequentlyChangedParameters.cpp
    GUI/FrequentlyChangedParameters.hpp
    GUI/Sidebar.cpp
    GUI/Sidebar.hpp
    GUI/Plater.cpp
    GUI/Plater.hpp
    GUI/LoadStepDialog.cpp
    GUI/LoadStepDialog.hpp
    GUI/PresetComboBoxes.hpp
    GUI/PresetComboBoxes.cpp
    GUI/BitmapComboBox.hpp
    GUI/BitmapComboBox.cpp
    GUI/EditGCodeDialog.hpp
    GUI/EditGCodeDialog.cpp
    GUI/SavePresetDialog.hpp
    GUI/SavePresetDialog.cpp
    GUI/BulkExportDialog.hpp
    GUI/BulkExportDialog.cpp
    GUI/PhysicalPrinterDialog.hpp
    GUI/PhysicalPrinterDialog.cpp
    GUI/GUI_Factories.cpp
    GUI/GUI_Factories.hpp
    GUI/GUI_ObjectList.cpp
    GUI/GUI_ObjectList.hpp
    GUI/GalleryDialog.cpp
    GUI/GalleryDialog.hpp
    GUI/GUI_ObjectManipulation.cpp
    GUI/GUI_ObjectManipulation.hpp
    GUI/GUI_ObjectSettings.cpp
    GUI/GUI_ObjectSettings.hpp
    GUI/GUI_ObjectLayers.cpp
    GUI/GUI_ObjectLayers.hpp
    GUI/MeshUtils.cpp
    GUI/MeshUtils.hpp
    GUI/Tab.cpp
    GUI/Tab.hpp
    GUI/ConfigManipulation.cpp
    GUI/ConfigManipulation.hpp
    GUI/Field.cpp
    GUI/Field.hpp
    GUI/OptionsGroup.cpp
    GUI/OptionsGroup.hpp
    GUI/OG_CustomCtrl.cpp
    GUI/OG_CustomCtrl.hpp
    GUI/BedShapeDialog.cpp
    GUI/BedShapeDialog.hpp
    GUI/ThumbnailsDialog.cpp
    GUI/ThumbnailsDialog.hpp
    GUI/2DBed.cpp
    GUI/2DBed.hpp
    GUI/3DBed.cpp
    GUI/3DBed.hpp
    GUI/CoordAxes.cpp
    GUI/CoordAxes.hpp
    GUI/Camera.cpp
    GUI/Camera.hpp
    GUI/CameraUtils.cpp
    GUI/CameraUtils.hpp
    GUI/wxExtensions.cpp
    GUI/wxExtensions.hpp
    GUI/ExtruderSequenceDialog.cpp
    GUI/ExtruderSequenceDialog.hpp
    GUI/WipeTowerDialog.cpp
    GUI/WipeTowerDialog.hpp
    GUI/RammingChart.cpp
    GUI/RammingChart.hpp
    GUI/RemovableDriveManager.cpp
    GUI/RemovableDriveManager.hpp
    # Removed SendSystemInfoDialog (Prusa services)
    GUI/SurfaceDrag.cpp
    GUI/SurfaceDrag.hpp
    GUI/TextLines.cpp
    GUI/TextLines.hpp
    GUI/BonjourDialog.cpp
    GUI/BonjourDialog.hpp
    GUI/ButtonsDescription.cpp
    GUI/ButtonsDescription.hpp
    GUI/ImGuiWrapper.hpp
    GUI/ImGuiWrapper.cpp
    GUI/ImGuiPureWrap.hpp
    GUI/ImGuiPureWrap.cpp
    GUI/NotesDialog.hpp
    GUI/NotesDialog.cpp
    Config/Snapshot.cpp
    Config/Snapshot.hpp
    Config/Version.cpp
    Config/Version.hpp    
    Utils/ASCIIFolding.cpp
    Utils/ASCIIFolding.hpp
    Utils/NvidiaProfile.cpp
    Utils/NvidiaProfile.hpp
    Utils/Serial.cpp
    Utils/Serial.hpp
    Utils/TD1SSensor.cpp
    Utils/TD1SSensor.hpp
    GUI/TD1SDialog.cpp
    GUI/TD1SDialog.hpp
    GUI/ConfigWizard.cpp
    GUI/ConfigWizard.hpp
    GUI/ConfigWizard_private.hpp
    GUI/MsgDialog.cpp
    GUI/MsgDialog.hpp
    GUI/UpdateDialogs.cpp
    GUI/UpdateDialogs.hpp
    GUI/PrintHostDialogs.cpp
    GUI/PrintHostDialogs.hpp
    GUI/Jobs/Job.hpp
    GUI/Jobs/Worker.hpp
    GUI/Jobs/BoostThreadWorker.hpp
    GUI/Jobs/BoostThreadWorker.cpp
    GUI/Jobs/UIThreadWorker.hpp
    GUI/Jobs/BusyCursorJob.hpp
    GUI/Jobs/CancellableJob.hpp
    GUI/Jobs/PlaterWorker.hpp
    GUI/Jobs/ArrangeJob2.hpp
    GUI/Jobs/ArrangeJob2.cpp
    GUI/Jobs/SeqArrangeJob.hpp
    GUI/Jobs/SeqArrangeJob.cpp
    GUI/Jobs/CreateFontNameImageJob.cpp
    GUI/Jobs/CreateFontNameImageJob.hpp
    GUI/Jobs/CreateFontStyleImagesJob.cpp
    GUI/Jobs/CreateFontStyleImagesJob.hpp
    GUI/Jobs/EmbossJob.cpp
    GUI/Jobs/EmbossJob.hpp
    GUI/Jobs/ProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.cpp
    GUI/Jobs/ThreadSafeQueue.hpp
    GUI/ProgressStatusBar.hpp
    GUI/ProgressStatusBar.cpp
    GUI/Mouse3DController.cpp
    GUI/Mouse3DController.hpp
    GUI/ImGuiDoubleSlider.cpp
    GUI/ImGuiDoubleSlider.hpp
    GUI/TickCodesManager.cpp
    GUI/TickCodesManager.hpp
    GUI/DoubleSliderForLayers.cpp
    GUI/DoubleSliderForLayers.hpp
    GUI/DoubleSliderForGcode.cpp
    GUI/DoubleSliderForGcode.hpp
    GUI/RulerForDoubleSlider.cpp
    GUI/RulerForDoubleSlider.hpp
    GUI/Notebook.cpp
    GUI/Notebook.hpp
    GUI/TopBar.cpp
    GUI/TopBar.hpp
    GUI/TopBarMenus.cpp
    GUI/TopBarMenus.hpp
    GUI/ObjectDataViewModel.cpp
    GUI/ObjectDataViewModel.hpp
    GUI/InstanceCheck.cpp
    GUI/InstanceCheck.hpp
    GUI/Search.cpp
    GUI/Search.hpp
    GUI/NotificationManager.cpp
    GUI/NotificationManager.hpp
    GUI/UnsavedChangesDialog.cpp
    GUI/UnsavedChangesDialog.hpp
    GUI/ExtraRenderers.cpp
    GUI/ExtraRenderers.hpp
    GUI/ProjectDirtyStateManager.hpp
    GUI/ProjectDirtyStateManager.cpp
    GUI/DesktopIntegrationDialog.cpp
    GUI/DesktopIntegrationDialog.hpp
    GUI/HintNotification.cpp
    GUI/HintNotification.hpp
    GUI/Widgets/BitmapToggleButton.cpp
    GUI/Widgets/BitmapToggleButton.hpp
    GUI/Widgets/Button.cpp
    GUI/Widgets/Button.hpp
    GUI/Widgets/CheckBox.cpp
    GUI/Widgets/CheckBox.hpp
    GUI/Widgets/Slider.cpp
    GUI/Widgets/Slider.hpp
    GUI/Widgets/ScrollBar.cpp
    GUI/Widgets/ScrollBar.hpp
    GUI/Widgets/ScrollablePanel.cpp
    GUI/Widgets/ScrollablePanel.hpp
    GUI/Widgets/ComboBox.cpp
    GUI/Widgets/ComboBox.hpp
    GUI/Widgets/CustomMenu.cpp
    GUI/Widgets/CustomMenu.hpp
    GUI/Widgets/CustomMenuBar.cpp
    GUI/Widgets/CustomMenuBar.hpp
    GUI/Widgets/DropDown.cpp
    GUI/Widgets/DropDown.hpp
    GUI/Widgets/StateColor.cpp
    GUI/Widgets/StateColor.hpp
    GUI/Widgets/StateHandler.cpp
    GUI/Widgets/StateHandler.hpp
    GUI/Widgets/StaticBox.cpp
    GUI/Widgets/StaticBox.hpp
    GUI/Widgets/FlatStaticBox.cpp
    GUI/Widgets/FlatStaticBox.hpp
    GUI/Widgets/CollapsibleSection.cpp
    GUI/Widgets/CollapsibleSection.hpp
    GUI/Widgets/LabeledBorderPanel.cpp
    GUI/Widgets/LabeledBorderPanel.hpp
    GUI/Widgets/SpinInput.cpp
    GUI/Widgets/SpinInput.hpp
    GUI/Widgets/SwitchButton.cpp
    GUI/Widgets/SwitchButton.hpp
    GUI/Widgets/TextInput.cpp
    GUI/Widgets/TextInput.hpp
    GUI/Widgets/ThemedTextCtrl.cpp
    GUI/Widgets/ThemedTextCtrl.hpp
    GUI/Widgets/UIColors.hpp
    GUI/FileArchiveDialog.cpp
    GUI/FileArchiveDialog.hpp
    GUI/Downloader.cpp
    GUI/Downloader.hpp
    GUI/DownloaderFileGet.cpp
    GUI/DownloaderFileGet.hpp
    # Removed LoginDialog (Prusa account login)
    GUI/PresetArchiveDatabase.cpp
    GUI/PresetArchiveDatabase.hpp
    Utils/AppUpdater.cpp
    Utils/AppUpdater.hpp
    Utils/Http.cpp
    Utils/Http.hpp
    Utils/FixModelByWin10.cpp
    Utils/FixModelByWin10.hpp
    Utils/Jwt.cpp
    Utils/Jwt.hpp
    Utils/Moonraker.cpp
    Utils/Moonraker.hpp
    Utils/OctoPrint.cpp
    Utils/OctoPrint.hpp
    Utils/Duet.cpp
    Utils/Duet.hpp
    Utils/EmbossStyleManager.cpp
    Utils/EmbossStyleManager.hpp
    Utils/FlashAir.cpp
    Utils/FlashAir.hpp
    Utils/FontConfigHelp.cpp
    Utils/FontConfigHelp.hpp
    Utils/AstroBox.cpp
    Utils/AstroBox.hpp
    Utils/Repetier.cpp
    Utils/Repetier.hpp
    Utils/PrintHost.cpp
    Utils/PrintHost.hpp
    Utils/Bonjour.cpp
    Utils/Bonjour.hpp
    Utils/PresetUpdater.cpp
    Utils/PresetUpdater.hpp
    Utils/ProfileServer.cpp
    Utils/ProfileServer.hpp
    Utils/PresetUpdaterWrapper.cpp
    Utils/PresetUpdaterWrapper.hpp
    Utils/Process.cpp
    Utils/Process.hpp
    Utils/RaycastManager.cpp
    Utils/RaycastManager.hpp
    Utils/UndoRedo.cpp
    Utils/UndoRedo.hpp
    Utils/HexFile.cpp
    Utils/HexFile.hpp
    Utils/TCPConsole.cpp
    Utils/TCPConsole.hpp
    Utils/MKS.cpp
    Utils/MKS.hpp
    Utils/WinRegistry.cpp
    Utils/WinRegistry.hpp
    Utils/WxFontUtils.cpp
    Utils/WxFontUtils.hpp
    Utils/Secrets.hpp
    Utils/Secrets.cpp
    # Removed Prusa-specific services (PrusaConnect, ServiceConfig)
)

find_package(NanoSVG REQUIRED)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(OpenSSL REQUIRED)
endif()

if (APPLE)
    list(APPEND PREFLIGHT_GUI_SOURCES
            Utils/RetinaHelperImpl.mm
            Utils/MacDarkMode.mm
            Utils/MacUtils.mm
            GUI/RemovableDriveManagerMM.mm
            GUI/RemovableDriveManagerMM.h
            GUI/Mouse3DHandlerMac.mm
            GUI/InstanceCheckMac.mm
            GUI/InstanceCheckMac.h
            GUI/WebViewPlatformUtilsMac.mm
            GUI/ObjectListMac.mm
            GUI/ObjectListMac.hpp

    )
    FIND_LIBRARY(DISKARBITRATION_LIBRARY DiskArbitration)
elseif (WIN32)
    list(APPEND PREFLIGHT_GUI_SOURCES
            GUI/WebViewPlatformUtilsWin32.cpp
    )
else() # Linux
    list(APPEND PREFLIGHT_GUI_SOURCES
            GUI/WebViewPlatformUtilsLinux.cpp
    )
endif ()

add_library(dsky STATIC ${PREFLIGHT_GUI_SOURCES})


foreach(_source IN ITEMS ${PREFLIGHT_GUI_SOURCES})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "/" "\\" _group_path "${_source_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()

encoding_check(dsky)

target_link_libraries(
    dsky
    PUBLIC
    luminary
    libcereal
    imgui
    libvgcode
    # GLEW replaced by GLAD (provided via libvgcode)
    OpenGL::GL
    hidapi
    libcurl
    ${wxWidgets_LIBRARIES}
    NanoSVG::nanosvg
    NanoSVG::nanosvgrast
    stb_dxt
    fastfloat
    boost_headeronly
)

# GUI/AboutDialog.cpp names expat for its version string, so the include directory comes with the
# consumer rather than through the engine's interface, where the link is private.
target_link_libraries(dsky PRIVATE libexpat)

if (MSVC)
    target_link_libraries(dsky PUBLIC Setupapi.lib)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(dsky PUBLIC
        ${DBus1_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
    )
elseif (APPLE)
    target_link_libraries(dsky PUBLIC ${DISKARBITRATION_LIBRARY})
endif()

if (PREFLIGHT_PCH AND NOT PREFLIGHT_SYNTAXONLY)
    add_precompiled_header(dsky pchheader.hpp FORCEINCLUDE)
endif ()

# We need to implement some hacks for wxWidgets and touch the underlying GTK
# layer and sub-libraries. This forces us to use the include locations and
# link these libraries.
if (UNIX AND NOT APPLE)
    find_package(GTK${PREFLIGHT_GTK} REQUIRED)
    find_package(PkgConfig REQUIRED)

    pkg_search_module(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.0 webkit2gtk-4.1)
    target_link_libraries(dsky PUBLIC ${GTK${PREFLIGHT_GTK}_LIBRARIES} fontconfig PkgConfig::WEBKIT2GTK)
endif ()

# Add a definition so that we can tell we are compiling the GUI module.
target_compile_definitions(dsky PRIVATE PREFLIGHT_CURRENTLY_COMPILING_GUI_MODULE)

# Keep <windows.h> from pulling in winsock.h, which collides with the winsock2.h that boost::asio
# includes. A target definition reaches every source, with or without the precompiled header.
if (WIN32)
    target_compile_definitions(dsky PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif ()
