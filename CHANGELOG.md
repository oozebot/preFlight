# preFlight Changelog

## v0.9.3

### New Features
- **Nip/Tuck Seams**: V-notch on external perimeters to hide seams
- **Seam Vertical Alignment**: Stable reference-position tracking prevents seam drift between layers; painted enforcer regions auto-center the seam at the enforcer centroid
- **Preview Clipping Plane**: Right-click any object in sliced preview to activate an interactive cross-section plane that cuts through toolpaths and shell meshes for analysis
- **Tabbed Sidebar Layout**: Tabbed sidebar as an alternative to the accordion layout, toggled via Preferences > GUI
- **Search Settings**: New search dialog with dedicated button in tab bar

### Bug Fixes
- Fix printer host type dropdown using fragile index offsets, replaced with explicit enum mapping
- Add defensive HWND validity checks in dark mode title bar and explorer theme calls

### Linux
- Fix blank Object Manipulation panel and Info panel overlap on GTK3
- Fix Wayland negative-width assertion in sidebar custom controls
- Update install paths and desktop file branding for preFlight
- AppImage now uses pre-split libraries with system GPU drivers on modern distros and bundled fallback on older systems

## v0.9.2

### New Features
- **Linux Support**: preFlight now runs natively on Linux with the full preFlight experience and single-file AppImage packaging — download and run, no install needed
- **Responsive Tab Bar**: Settings buttons auto-collapse into a single "Settings" dropdown when the tab bar is narrow (e.g., long printer name, small window)
- **Continuous Scrollable Sidebar**: Flattened sub-tabs into a single scrollable list where all setting groups are visible simultaneously

### UI/Theme Improvements
- Smoother window dragging on high-DPI displays by pausing GL canvas rendering during drag
- Custom themed menus and tab bar on Linux (GTK3), matching the Windows experience

### Bug Fixes
- Fix use-after-free crash in sidebar dead-space click handler binding
- Fix standalone RRF (Duet) machine limits retrieval for non-SBC boards

### Known Limitations
- Linux build supports dark mode only — light mode is not yet available

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
