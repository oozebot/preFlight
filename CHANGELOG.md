# preFlight Changelog

## v0.9.1

### New Features
- **Printer Interface Tab**: Embedded webview showing printer's web interface with real-time connection status indicator
- **Project Notes**: Add notes to individual objects or entire project, persisted in 3MF files with undo/redo support
- **Custom Menu System**: Fully themed popup menus and menu bar
- **Accordion-Style Sidebar**: Collapsible sections with inline settings editing
- **Sidebar Visibility**: Per-option visibility checkboxes to customize sidebar
- **DPI Aware Improvements**: DPI aware improvements to all areas within the application

### UI/Theme Improvements
- Centralized UIColors system for consistent theming
- Midnight dark theme with cool blue-gray palette
- Windows 11 custom title bar colors
- Theme-aware bed/canvas, ImGui, ruler, legend, and sliders

### Bug Fixes
- Fix crash in monotonic region chaining when ant hits dependency dead-end
- Fix monotonic infill lines escaping boundary on complex multi-hole polygons
- Fix Voronoi "source index out of range" crash during slicing
- Fix physical printer selection not persisting across app restarts
- Fix brim settings crash and brim infinite loop
- Fix submenu items not responding to clicks in custom menus
- Fix config wizard broken on fresh installs
- Fix Athena thin wall width precision errors
- Fix mouse wheel gcode navigation on layer 0
- Fix double-delete crash in View3D/Preview destructors
- Disable mouse wheel on spin/combo inputs to prevent accidental changes

## v0.9.0

Initial release of preFlight, based on PrusaSlicer.
