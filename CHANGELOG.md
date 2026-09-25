# preFlight Changelog

## v1.4.0

### RIP Slic3r! Long live Luminary!
One of our initial goals with preFlight was to retire libslic3r, the 15-year-old engine at its core. With the next release, we're doing exactly that with the introduction of Luminary as its replacement. This isn't just a rename, it's an entire ground-up reimagining of the engine we've been working towards for the past 18 months, with additional modernization still ahead that will land in future releases. And while we were at it, the UI formerly known as slic3r becomes DSKY.

For anyone curious about why these names were chosen, look up Apollo 13. When an oxygen tank blew on the way to the Moon, the crew powered down the Command Module to save it for reentry and used the lunar lander, Aquarius, as a lifeboat. The software that flew Aquarius home was Luminary, and the crew ran those critical burns by hand through the DSKY, the display and keyboard unit. Neither Luminary nor the DSKY was meant to save a mission, but together they brought the crew back alive. That's the pedigree we want for the engine everything else depends on.

And for the record, we owe a real debt to everyone who built Slic3r and PrusaSlicer over the years - the remaining attributions carry forward, as it should.

If we did it right, you won't notice any of this: your profiles, projects and G-code are unchanged. Post-processing scripts now also receive `PREFLIGHT_PP_HOST` and `PREFLIGHT_PP_OUTPUT_NAME`, and the old `SLIC3R_PP_*` names are still set, so existing scripts keep working.

### Sidebar (Re)Slicing
- Sidebar processing replaces background processing
- The Sidebar is now available in the Preview via the "hamburger" icon beside the Legend near the top. Clicking it swaps the Legend for the Sidebar
- In the Preview, any change made within the Sidebar automatically reslices
  - Changes made anywhere else, including the Settings tabs, return the button to Slice
  - Changes made in Prepare never trigger a slice
- Objects can be selected in the Preview by clicking them, with the same selection box as in Prepare

### Sidebar Overrides
- The object settings panel has been rebuilt. Click the new Overrides column at the far right of the Object list to open it for any object or part
  - Every setting the item can override is listed with a checkbox to enable it and a lock showing whether it matches the project value
  - Changes prompt the same way the main settings do, such as enabling Serpentine or setting 100% infill with a pattern that can't be solid
- The old right-click "Add settings" flow has been retired
- The object info panel under the Object list has been removed, giving the list more room

### Rendering & Camera
- Alt+Middle click centers the view and the orbit point on whatever is under the cursor, toolpaths included
- New "Full (shadows + AO)" lighting quality with shadows, ambient occlusion and physically based shading in Prepare and Preview. Toolpaths cast and receive shadows
- Supersampling (Off, 1.5x, 2x) removes aliasing in dense previews and applies without a restart
- Preferences > Camera offers mouse navigation schemes matching Blender, Fusion 360, SolidWorks and Tinkercad
- Changing MSAA now prompts for the restart it needs

### Settings
- The categories inside the Print, Filament and Printer panels of the Sidebar now collapse to assist with navigation, and stay the way you left them
- Disabled settings now say why, such as "Available when <setting> is enabled"
- New setting: Small perimeter diameter (default 13 mm, previously a fixed 6.5 mm radius)
- New setting: Support alerts (on by default). Turn it off to skip stability analysis on objects without automatic supports
- New per-object setting: Minimum wall length for Athena and Arachne (#277)
  - Single thin walls shorter than this are skipped, avoiding strings of tiny stubs
  - The default (50%) matches previous behavior
- First layer speed, First layer solid infill speed and the over-raft speed are now limits, so features already set slower keep their own speed on the first layer. 0 disables the limit

### Supports
- Organic, Baobab and Snug supports now reach sloped overhangs, and painted supports ignore the threshold angle
- New per-object "Minimum opening" for Organic and Baobab (default 8 mm): branches no longer route through holes or slots narrower than this. Set it to 0 to allow them
- "Support on build plate only" can now be set whenever supports are generated, including painted Organic supports. "Plant trunks on object" had the same problem (#285)
- Tree and Organic support roof lines follow the interface angle and alternate each layer instead of always printing at 45 degrees
- Baobab: new "Trunk consolidation" setting (default 4 mm). Higher values give fewer, larger trunks
- Baobab: redundant trunks are removed, the base under each interface covers the whole interface, and canopies keep their clearance from the model
- Baobab: moving or rotating an object no longer produces a different tree
- The print stability alert now separates unsupported geometry from bed adhesion and part strength issues
- Fixed thick Organic branches near walls being cut flat
- Fixed Organic support branches printing through the interface under some overhangs
- Projects saved with the old "tree" support style now load as Organic

### Seams
- Painted seams follow the center of the paint stroke, so a slightly wobbly stroke should generate a straight seam, in every seam mode
- Nip/Tuck notches span their full configured width and no longer collapse on tapered walls
- Seam notch width can now go down to 0.5x the external perimeter width (#237)

### Serpentine
- Fixed islands Serpentine can't fully cover printing about half a bead oversized. The uncovered parts now print as normal walls, with one notification per object
- Fixed thin walls alternating between Serpentine and normal walls from layer to layer
- Fixed small solid islands such as bolt heads being left hollow in the center
- The Serpentine extrusion width is now validated like every other width
- Several of these fixes were inspired by an experimental Serpentine build shared by @LeoMoz (thank you!)

### Printers
- New option "Combine Z into first travel" (Printer Settings > General > Advanced): the first move to the print is one diagonal move, so a nozzle parked high doesn't drop to layer height and skim across the bed (#262)
- Klipper physical printers now have an optional separate web interface address, for setups where Mainsail or Fluidd is served apart from Moonraker (#169)
- New preference (Preferences > GUI): "Open the printer web interface in the system browser"
- Downloaded updates only run when signed by oozeBot

### Preprocessing
- Time estimates, filament statistics and M73 progress now reflect the changes scripts make
- Fan and temperature overrides apply only to the moves a script changes instead of carrying over for whole layers
- Scripts have a time limit and can be interrupted with Cancel
- The console asks before running scripts embedded in a project
- API: layers can now search their own G-code, `layer.z` ignores z-hops, and invalid line numbers raise `ValueError`

### Performance
- Slicing on Windows is 10 to 17 percent faster. Memory allocation now goes through tbbmalloc instead of the Windows heap (not on Windows ARM64)
  - If Windows blocks tbbmalloc, for example with Arbitrary Code Guard turned on in Exploit Protection, preFlight falls back to the Windows heap. Set `TBB_MALLOC_DISABLE_REPLACEMENT=1` to turn tbbmalloc off
- Ironing no longer stalls on narrow top surfaces (one test case went from 30 seconds to less than 5)
- Projects using Interlocking Perimeters or an over-bridge speed slice up to twice as fast
- A settings change only reruns the steps it affects, so many changes reslice in a fraction of the time
- Large Baobab supports slice much faster
- Travel planning is much faster with Avoid crossing curled overhangs enabled

### Bug Fixes / Other tweaks
- Fixed the Linux AppImage printer tab crashing on newer systems such as Ubuntu 26.04 and never loading on systems older than 24.04
  - If the page fails, preFlight now opens it in your browser (#281)
- Fixed Linux desktop integration asking to update itself at every launch when a packaged preFlight.desktop already exists in a system directory
- Fixed print time estimates on curved parts reading up to 3x too long with junction deviation or Klipper, which also made preview layer times disagree with auto-cooling (#274)
- Fixed a crash when enabling "Use surface" in the Emboss gizmo (#278)
- Switching to a printer with fewer extruders and back now restores each slot's filament and color (#94)
- Fixed a Sidebar UI lag while a Settings tab is open, and when loading a project that switches to a printer with more extruders
- Fixed the printer tab keeping the previous printer's name after switching physical printers (#284)
- Fixed the Print Settings and Printer rows being cut off in the Objects tab on macOS (#276)
- Fixed per-feature fan speeds being skipped in manual fan mode
- Fixed the first slice of a project ignoring filament shrinkage compensation
- Fixed the per-object slice closing radius using the first object's value for every object
- Slicing the same project twice now gives identical G-code (except with random seams)
- Fixed the per-extruder nozzle rows showing as modified with some projects
- Fixed projects loading with the previous project's first filament selected
- Fixed the color mixing palette using stale printer colors instead of the filament colors
- Fixed the title losing its unsaved-changes marker after long editing sessions
- Fixed uploads reporting success when the G-code file couldn't be read
- Fixed objects in the notch of an L- or U-shaped custom bed counting as inside the print area
- Spiral vase with absolute extrusion now warns that it skips the entry and exit tapers
- Fixed a crash when closing preFlight during the startup update check
- Fixed Repair STL and mesh export reporting success when they failed
- Fixed several error messages showing in English in every language
- Several custom G-code errors are now easier to read
- Fixed missing solid fill and misplaced bridge anchors with Interlocking Perimeters
- Fixed clipping plane cross-sections stretching into long spikes
- The Clipping Plane entry is now available on every object and centers the view on it
- The Filament tab shows the extruder selector for every printer with more than one extruder
- Many smaller robustness fixes in 3MF loading, print host uploads, painting gizmos and fuzzy skin

### Klipper
- Moonraker v0.11.0 (released 2026-08-25) is the first version that recognizes G-code sliced by preFlight
  - Moonraker v0.9.3 and older show no thumbnails for preFlight G-code. v0.10.0 shows thumbnails but not the other details below
  - Older versions treat preFlight as an unknown slicer and read only the first layer height, object height and first layer temperatures
  - The estimated print time, filament used and weight, layer height, layer count, nozzle diameter, and filament names, types and colors are missing in Mainsail and Fluidd until Moonraker is updated

### Build
- IMPORTANT: Building from source requires a clean dependency rebuild on every platform: run `build_deps.bat -clean` (Windows) or `./build_deps.sh -clean` (Linux, macOS) before building
- Bumped bundled expat from 2.6.4 to 2.8.5 for its XML parser security fixes
- `build_deps` now names the dependency that failed instead of stopping silently


## v1.3.0

### Baobab (AKA 'Great Tree') Supports

Baobab is a new support engine that lives somewhere between Organic and Snug. Where Organic grows slender branches that end in fine tips, Baobab grows thick trunks that expand into a canopy matching the shape of the support interface.

- Generates broad canopies under the support interface which merge downward into thick trunks
- Prints faster than other support types while maintaining rigidity with minimal material
- Canopies are supported with Lightning infill to keep the structure light and interface layers solid
- Available for automatic supports, or painted via the Baobab enforcer within the Paint-on supports gizmo
- Dedicated settings under Support material control the trunk diameter, growth angles, and canopy shape and density

### Serpentine
- Added a relaxed spoke layout mode
- Added an outer surface loop option

### Tree Supports - Variable Layer Height Compatibility
- Tree Supports now work with variable layer height
  - Only extreme variability the interface layers can't bridge is still refused


### Print Stability Alerts
- Stability Analysis now runs for objects using manually placed supports (previously only available when support generation was not enabled)
  - Painted enforcers, support enforcer volumes, and enforced first layers count as coverage
  - The alert names only the trouble spots supports do not cover
  - Can be turned off entirely with the existing "Alert when supports needed" preference

### Width Control
- Renamed Athena-specific "Maximum perimeter width" to "Maximum width" as Athena continues to grow beyond perimeters
- Widened beads now slow down to hold their feature's intended volumetric flow rate when no max volumetric flow is configured
  - With a max volumetric flow set, behavior is unchanged: the ceiling governs
- New real-time width warning: if slicing generates a wall or fill bead wider than the extruder's "Width warning max" threshold, a warning is presented

### Perimeters while Interlocking
- Fixed perimeter gaps with interlocking enabled, caused by Athena bead placement drift from the third bead outward
- Reworked suppression handling so regions are reliably covered by walls or fill, with dedicated interlocking lanes through narrow flow-through corridors
- Bridge direction detection over interlocked regions now derives floating edges from the slices, fixing wrong bridge angles above interlocking
- Fixed boundary offsets for interlocking bead pairs, with narrow solid regions exempted from the boundary

### Bug Fixes
- Support brim yields to the support base flange instead of trimming its extrusions (#255)
- Organic and Baobab diameter floors now validate painted-enforcer objects, not just style-selected ones
- Fixed a crash when auto-generating paint from the supports gizmo on objects whose stability data was never computed
- Extra overhang perimeters print in anchored order with consistent direction and seams aligned to the seam placer (#253)
- Internal infill spacing too narrow for a round bead is clamped to the minimum instead of dropping the region
- Negative perimeter overlap is honored for internal beads instead of inflating them back to the spacing
- Clicking a filament color swatch commits the color straight to the filament preset (#250)
- Hardened Clipper2 geometry processing against micro self-intersections and winding edge cases
- Fixed garbled characters in settings search results on non-ASCII locales
- Fixed an inner-contour marker index in Arachne's capped beading branch

## v1.2.0

### Serpentine

Note - Serpentine is not suitable for all geometry and may produce poor or unexpected results on complex shapes. It works best on symmetric objects such as nuts and bolts

- New perimeter generator mode that fills a region with a single continuous extrusion in place of separate perimeters and infill. The path weaves inward from the boundary in a repeating serpentine pattern
  - Turn it on per region under Print Settings > Layers and perimeters > Serpentine. It runs on the Athena generator, so enabling it switches the generator to Athena (with a confirmation prompt)
- Layer ridge stacking: Aligned, Staggered, or Random. Staggered (default) shifts the pattern half a period each layer so the turns overlap each other for a stronger bond between layers
- Depth limit: form a serpentine wall to a set depth in mm and fill the interior with sparse infill and solid top/bottom layers
- Aim: Convergent or Perpendicular, controlling how the depth-limited pattern points at the inner boundary (depth mode only)
- Solid top and bottom layer option prints solid infill with a serpentine band so visible faces stay closed
- Has its own feature type in the G-code preview, with overhanging spans split as "Serp. Overhang"
- Prints at External perimeter speed, fan, and acceleration settings with its own manual fan controls under Filament > Cooling
- Extrusion width, overlap (negative values lower density), and maximum bead width are all configurable

### Preprocessing

Important - do not keep your custom preprocessing scripts inside preFlight's resources folder. preFlight ships self-contained, so a script saved there does not carry over when you install a new version. Keep your custom scripts outside of the preFlight install folder to guarantee they survive upgrades.

Previously installed Python packages (numpy, etc.) need to be reinstalled after this update, but will now survive future updates and will only need reinstalling when preFlight ships a new version of Python.

- Installed packages are stored in your preFlight data folder, so upgrading preFlight no longer wipes them
- The Python Console reliably launches preFlight's bundled Python and pip on Windows, macOS, and Linux, with pip pre-installed and HTTPS working out of the box
- Only packages that provide a prebuilt wheel can be installed (the bundled Python has no compiler)

### Interlocking
- "Perimeters while Interlocking" perimeter count reduction fills suppressed regions with the full perimeter count while the buried core keeps its reduced walls and interlocking shells (#224)

### Narrow-to-Athena
- Narrow-to-Athena infill option now splits a surface by width: thin frames, necks, and spikes convert to a smooth Athena bead while the wider area keeps its solid pattern (#239)
  - Added a separate opt-in for top and bottom surfaces (off by default); internal solid always splits when Narrow-to-Athena is on
  - If the split fails to cover the surface, the region falls back to un-split so no gap is left

### Speed & Time Estimation
- Fixed badly inflated time estimates on Klipper and RepRapFirmware profiles, where a small part could read days instead of minutes. Klipper limits are built from the four fields it actually exposes rather than hidden legacy per-axis arrays, RepRapFirmware falls back to Duet firmware defaults, and a zero acceleration limit is read as "no limit" instead of freezing the move (#246)
- Dynamic overhang speed interpolates smoothly between the configured bands instead of stepping between them, and rounds to a 2 mm/s grid to keep the G-code smaller
- Print-level max volumetric flow (renamed from max_volumetric_speed, with the old name kept as an alias) moved into the Print Settings speed group and the sidebar, where it caps each filament's flow under auto speed
- Renamed the dynamic overhang speed labels to "Speed at N% overhang"

### Layer Height
- Repeating-decimal layer heights (1/3, 1/6, 1/7, 1/9 mm) resolve to their exact fraction, so past the first layer every N layers add up to exactly 1 mm (3 at 0.3333 mm, 6 at 0.1667) instead of drifting up the object
- Organic supports follow the object's layer grid instead of landing on their own Z at fractional layer heights
- Added N-per-mm guidance and a value cheat sheet to the layer height tooltip

### Config Import
- SuperSlicer and other external config bundles that used to fail on import now load: blank extrusion widths fall back to auto, arc_fitting converts from a bool, and host_type "klipper" maps to "moonraker" (#162)
- Importing a bundle from another slicer warns that its settings may not translate exactly

### Menus & Right-Click
- Added Export G-code / Slice Platter and view-navigation entries to the Prepare and Preview right-click menus
- Fixed the object-list and preset context menus opening in the top-left corner instead of at the cursor
- Fixed Recent Projects opening the wrong project after the list reordered (#233)
- Fixed object-list setting changes leaving the action button on "Export G-code" and blocking reslices for the rest of the session (#230)
- Stopped the top menus closing themselves on open under seamless window managers such as Qubes (#233)

### Bug Fixes
- Fixed Shift+drag box selection on the plate selecting nothing (#230)
- Fixed the support blocker painting nothing on right-click, so supports were never actually blocked
- Fixed Delete removing the object instead of restarting selection while the Measure gizmo is open (#236)
- Fixed right-clicking a brim ear while hovering the object also opening the platter menu
- Fixed a lower-density part of a multipart object losing its infill on layers it shares with a taller part (#235)
- Fixed rendering issues with currency symbols outside the Latin-1 range (ruble, euro, won, rupee)
- Fixed the Preprocessing tab labels being unreadable in light themes on Linux

### Hardware
- Added the Bluetooth SpaceMouse Wireless and SpaceMouse Pro Wireless to the 3Dconnexion device list on Linux and macOS (#244)

### Developer
- Added a runtime `--debug` flag (fill, perimeters, interlock, serpentine, or all) that replaces the old compile-time debug switches

## v1.1.0

Note: You didn't read this wrong. With this release, we are jumping straight to v1.1.0 and skipping v1.0.1-stable with some significant changes.

### Themes
- UI theme system with 40+ built-in themes (Auto, Default Light / Dark, plus community favorites including Catppuccin, Matrix, Dracula, Nord, Solarized, Gruvbox, Tokyo Night, Rose Pine, and many more)
  - Change themes via the drop down in Preferences > GUI

### Sidebar
- Replaced per-row visibility checkboxes with a dedicated Edit Visibility mode and a Tabbed/Accordion view toggle at the bottom of the sidebar
  - Every option in the sidebar has a visibility checkbox - if the sidebar feels cluttered, you can hide anything you don't use
- Added "Legacy layout for Prepare" preference that mirrors the Prepare view horizontally - sidebar moves to the right, gizmo toolbar to the left
- Click the filament color swatch in the Filament dropdown to open the color picker directly

### Intel macOS
- New build available for Intel macOS machines

### Auto Speed
- Added Auto speed checkbox that calculates print move speeds from each filament's max volumetric flow (MVF), capped at an effective max print speed (MPS)
  - Setting individual speeds to 0 still triggers volumetric speed calculation for that feature
- Unified auto speed calculation paths and fixed filament max print speed override behavior - filament_max_print_speed is now an auto speed ceiling override only (MVF remains the unconditional physical safety cap)
- Added filament_max_volumetric_flow as the primary max volumetric flow setting, replacing the legacy filament_max_volumetric_speed (kept as a synced alias)
- Added filament_max_print_speed to override the print profile's max print speed on a per-filament basis

Note: MVF caps all print speeds regardless of whether Auto speed is enabled - if your speeds seem lower than expected, check that the active filament's MVF is set correctly. MPS only applies to Auto speed and does not affect manually set speeds.

### Klipper
- Added native Klipper machine limits for accurate time estimation - max_velocity, max_accel, square_corner_velocity, and minimum_cruise_ratio fields matching printer.cfg (#183)
- Time estimator handles SET_VELOCITY_LIMIT G-code commands for mid-print parameter changes
- Fixed minimum_cruise_ratio time estimation - ported Klipper's two-pass LookAheadQueue flush for accurate junction-dense geometry estimates

### GPU / Rendering
- Active GPU renderer now displayed in the title bar for easy identification and support
- Improved GPU detection - removed Intel GPU blocklist that forced basic lighting on capable cards like Arc A770
- Enabled Enhanced (Blinn-Phong) lighting on all hardware GPUs passing the OpenGL 3.2 minimum
- Lighting quality changes in Preferences now take effect without restart via deferred shader compilation

### Athena Perimeter Generation
- Perimeter compression no longer allows "Disabled" - minimal bead compression is required to prevent unfilled gaps
  - The new floor is "Minimal" (75%), and legacy profiles migrate automatically (#195)
- Promoted FillEnsuring gap fill from Arachne to Athena, eliminating oscillating-width diamond artifacts on staircase-shaped gap boundaries
- Fixed unnecessary infill in thin regions by subtracting actual innermost bead coverage from the inner contour
- Routed narrow-to-Athena fills to concentric instead of Ensuring, preventing bridge anchor geometry erosion from grouping collision

### Thumbnails
- Replaced the fragile thumbnails text field with a structured editor dialog with per-row size and format controls
- Added COLPIC (Elegoo/Chitu) and BTT_TFT (BigTreeTech) G-code thumbnail formats
- Unknown thumbnail formats are now a non-blocking warning instead of a hard error
- Platter renders in the theme accent so thumbnails match the active theme

### Orca Import
- Orca G-code variables now resolve at runtime via alias mapping to their preFlight equivalents - works in both `[bracket]` and `{brace}` syntax
- Commented-out G-code lines (starting with `;`) are no longer evaluated, preventing parse errors from leftover Orca placeholders
- Import results dialog now clearly labels skipped settings as "Orca-Exclusive" with an explanation

### G-code Variables
- Local variables (`{local ...}`) can now shadow config keys, allowing overrides like `{local retract_lift = 0.5}`
- Unified resolution order (locals > config > aliases) across both `[bracket]` and `{brace}` syntax

### Cooling
- Fixed fan always on not acting as a floor against dynamic overhang fan speed overrides
  - M107 no longer fires on supported extrusions when keep fan always on is set
- Fixed fan ramp backward insertion crossing fan-off commands, causing emitted fan speed to desync for the rest of the layer (#164)
- Fixed reset fan speed firing during manual fan mode, overriding per-feature speeds
- UI now disables fan_always_on when manual fan controls are active

### Localization
- Brought all 20 language catalogs to 100% translated (0 untranslated, 0 fuzzy) with a cross-language semantic review

### UI
- Added configurable extrusion width warning thresholds per nozzle (33-500%), replacing hardcoded 60%/150% warning limits (#146)
- Enabled right-click context menu in WebView printer UIs
- Widened seam line snap threshold from 5 to 15 degrees for more reliable activation on curved surfaces
- Rewrote interlocking overlap tooltip and added tuning guidance note
- Removed obsolete "Sequential slider applied only to top layer" preference - the behavior is now always on

### Bug Fixes
- Fixed concentric top fill incorrectly overridden to Ensuring on complex multi-hole geometry
- Fixed crash when slicing sunken objects due to dangling render context pointer (#168)
- Fixed Custom G-code error when idle_temperature is nil (#186)
- Fixed Variable Layer Height gizmo hiding the selected object
- Fixed Align to Face sliders rotating the canvas when interacting with the popup panel
- Fixed bed temperature set to 0 when "Other layers" was set to 0 - now keeps the first layer temperature
- Fixed null dereference crash when printer webview authentication completed after the panel was destroyed (#159)
- Fixed binary G-code (bGcode) export producing plain text output regardless of the binary_gcode setting (#165)
- Fixed tab button text overflow for translated labels - buttons now dynamically size to fit text with ellipsis truncation (#158)
- Fixed extruder dropdown in Filaments tab never populated with items on multi-tool printers (#176)
- Fixed Bed Shape dialog texture/model filename labels not updating on load or remove until the dialog was reopened
- Fixed auto-slice not triggering when switching to Preview via the Tab key (#177)
- Fixed extruder color swatch showing gray in sidebar for single-extruder setups
- Fixed time_estimate attribute access in sample preprocessor scripts (#221)
- Fixed Python preprocessor/export crash on GUI recreation via GIL management
- Fixed "Perimeters while Interlocking" not obeying certain geometry (#223)
- Printer webview tab now reuses the existing tab instead of opening duplicates (#159)
- Interlocking perimeters are now automatically disabled when entering spiral vase mode
- Improved performance when saving presets
- Enabled measure gizmo on sunk objects

### Windows
- Fixed nVidia splash screen transparency corruption via DirectComposition rendering

### Linux
- Fixed WebKit GBM EGL crash on NVIDIA proprietary drivers under Xorg (#157)
- Fixed AppImage crash on NVIDIA GPUs with older glibc by adding three-way launch strategy (#155)
- Fixed packaging conflicts with manifold and slicer-udev packages (#154)
- Statically linked OCCTWrapper on Linux to eliminate runtime dlopen failures

### Build / Packaging
- Bumped bundled deps cmake_minimum_required to 3.13 for CMake 4 compatibility
- Added support for building against system libraries for distro packaging
- Added support for building Intel macOS

## v1.0.1-beta2

### We listened
- Added "Legacy layout for Prepare" preference that mirrors the Prepare view horizontally - sidebar moves to the right, gizmo toolbar to the left
- Every option in the sidebar has a visibility checkbox in the main settings - if the sidebar feels cluttered, you can hide anything you don't use

### Auto Speed
- Added Auto speed checkbox that calculates print move speeds from each filament's max volumetric flow (MVF), capped at an effective max print speed (MPS)
  - Setting individual speeds to 0 still triggers volumetric speed calculation for that feature
- Added filament_max_volumetric_flow as the primary max volumetric flow setting, replacing the legacy filament_max_volumetric_speed (kept as a synced alias)
- Added filament_max_print_speed to override the print profile's max print speed on a per-filament basis

Note: MVF caps all print speeds regardless of whether Auto speed is enabled - if your speeds seem lower than expected, check that the active filament's MVF is set correctly. MPS only applies to Auto speed and does not affect manually set speeds.

### Klipper
- Added native Klipper machine limits for accurate time estimation - max_velocity, max_accel, square_corner_velocity, and minimum_cruise_ratio fields matching printer.cfg (#183)
- Time estimator handles SET_VELOCITY_LIMIT G-code commands for mid-print parameter changes

### GPU / Rendering
- Active GPU renderer now displayed in the title bar for easy identification and support
- Improved GPU detection - removed Intel GPU blocklist that forced basic lighting on capable cards like Arc A770
- Enabled Enhanced (Blinn-Phong) lighting on all hardware GPUs passing the OpenGL 3.2 minimum
- Lighting quality changes in Preferences now take effect without restart via deferred shader compilation

### Athena Perimeter Generation
- Perimeter compression no longer allows "Disabled" - minimal bead compression is required to prevent unfilled gaps. The new floor is "Minimal" (75%), and legacy profiles migrate automatically (#195)

### UI
- Added configurable extrusion width warning thresholds per nozzle (33-500%), replacing hardcoded 60%/150% warning limits (#146)
- Enabled right-click context menu in WebView printer UIs
- Widened seam line snap threshold from 5 to 15 degrees for more reliable activation on curved surfaces
- Rewrote interlocking overlap tooltip and added tuning guidance note that Interlocking must be tuned for your print settings

### Bug Fixes
- Fixed concentric top fill incorrectly overridden to Ensuring on complex multi-hole geometry
- Fixed crash when slicing sunken objects due to dangling render context pointer (#168)
- Fixed Custom G-code error when idle_temperature is nil (#186)
- Fixed Variable Layer Height gizmo hiding the selected object
- Fixed Align to Face sliders rotating the canvas when interacting with the popup panel
- Fixed bed temperature set to 0 when "Other layers" was set to 0 - now keeps the first layer temperature
- Enabled measure gizmo on sunk objects

## v1.0.1-beta1

### Orca Import
Cross-product profile importing is rare for good reason - every slicer has exclusive settings that simply don't exist elsewhere. OrcaSlicer has over 140 of them. preFlight reports these transparently so you know exactly what mapped and what didn't. In this release, we more clearly call that out. We also tackled slicing errors caused by Orca G-code variables in Custom G-code fields by natively mapping them to their preFlight equivalents.

- Orca G-code variables now resolve at runtime via alias mapping to their preFlight equivalents - works in both `[bracket]` and `{brace}` syntax
- Commented-out G-code lines (starting with `;`) are no longer evaluated, preventing parse errors from leftover Orca placeholders
- Import results dialog now clearly labels skipped settings as "Orca-Exclusive" with an explanation

### G-code Variables
- Local variables (`{local ...}`) can now shadow config keys, allowing overrides like `{local retract_lift = 0.5}`
- Unified resolution order (locals > config > aliases) across both `[bracket]` and `{brace}` syntax

### Cooling
- Fixed fan ramp backward insertion crossing fan-off commands, causing emitted fan speed to desync from firmware state for the rest of the layer (#164)
- Fixed reset fan speed firing during manual fan mode, overriding per-feature speeds
- UI now disables fan_always_on when manual fan controls are active

### Bug Fixes
- Fixed null dereference crash when printer webview authentication completed after the panel was destroyed (#159)
- Printer webview tab now reuses the existing tab instead of opening duplicates (#159)
- Fixed binary G-code (bGcode) export producing plain text output regardless of the binary_gcode setting (#165)
- Fixed tab button text overflow for translated labels - buttons now dynamically size to fit text with ellipsis truncation (#158)
- Interlocking perimeters are now automatically disabled when entering spiral vase mode
- Fixed extruder dropdown in Filaments tab never populated with items on multi-tool printers (#176)
- Fixed Bed Shape dialog texture/model filename labels not updating on load or remove until the dialog was reopened
- Fixed auto-slice not triggering when switching to Preview via the Tab key (#177)
- Improved performance when saving presets

### Infill / Fill
- Created stBridgeAnchor surface type which assigns Athena instead of potentially choppy infill segments (#173)
- Fixed narrow-to-athena ring detection

### Linux
- Fixed WebKit GBM EGL crash on NVIDIA proprietary drivers under Xorg (#157)
- Fixed AppImage crash on NVIDIA GPUs with older glibc by adding three-way launch strategy (#155)
- Fixed packaging conflicts with manifold and slicer-udev packages (#154)
- Statically linked OCCTWrapper on Linux to eliminate runtime dlopen failures

### Build / Packaging
- Bumped bundled deps cmake_minimum_required to 3.13 for CMake 4 compatibility
- Added support for building against system libraries for distro packaging

## Promotion to v1.0.0!

It's a red letter day! A small release of fixes with big implications. Yes, there are still bugs, our roadmap is a mile long, and we're just getting started, but at some point you have to rip off band-aid and ship it - this is that moment. Welcome to preFlight v1.0.0!

### Athena Perimeter Generation
- Added Max Perimeter Width setting (% of nozzle, default 150%) to control how much beads can expand to fill gaps - thin walls exceeding this limit split into a two-bead loop sized using the external/perimeter overlap setting
- Fixed bead width mismatch between perimeters=1 and perimeters=2 for identical geometry - the 0-width infill boundary marker was incorrectly handled during odd-case bead adjustment
- Fixed single-bead wall overlap at thin-wall-to-body junctions where beads grew past external width before splitting, causing heavy overlap at U-turns
- Fixed two-external thin wall contraction so both beads use external perimeter width and contract proportionally to maintain the configured overlap ratio

### Performance
- Painted snug/grid supports generation time significantly reduced via parallel processing that precomputes downward projection through sparse change points

### G-code Preview
- Fixed segments disappearing at viewport center when camera view direction was exactly perpendicular to a segment's axis

### Bug Fixes
- Fixed crash when navigating away from Output options page due to dangling pointer after page hierarchy destruction

## v0.9.15

### Performance
- Slicing has been optimized resulting in 2.5x faster processing
  - Replaced Voronoi medial axis with offset-based erosion for narrow surface detection
  - Parallelized 5 serialized loops in bridge_over_infill
  - Simplified Voronoi skeleton inner contours to reduce downstream Clipper2 cost
- Implemented GCodeObject - a unified data model carrying G-code and structured move data through the entire pipeline
- Enabled streaming move processing during G-code generation, eliminating a redundant full re-parse of all G-code
- Added Preview Detail setting in Preferences > Performance to control preview fidelity vs. slicing speed (1M/5M/10M/20M segments or Full)
  - Actual Speed preview coloring shows less detail on prints exceeding the threshold
  - Defaults to 10M on desktop, 1M on ARM Linux (Raspberry Pi)

### Export to Script
- Added Export to Script - a new export pathway that hands G-code data to a user-configured Python script for output handling (save to disk, upload via FTP, send to networked printers, or all at once)
- Script receives gcode.data (mutable list of G-code lines) and gcode.filename (from Output filename format) - no proprietary APIs, just standard Python
- Export to Script appears in the export dropdown alongside Save locally and Send to Printer when enabled
- 3MF files containing script references prompt the user on load and strip settings if declined
- Consent dialog updated to cover both Preprocessing and Export to Script under a single security prompt
- Included sample scripts: save_to_folder.py, ftps_upload.py (implicit TLS, port 990), save_and_upload.py (multi-output)
- Included type stubs and HOW_TO_USE documentation in resources/export

### Per-Filament Pressure Advance
- Added per-filament pressure advance settings with an enable toggle and configurable PA value - emits firmware-appropriate commands after start filament G-code on every tool/filament change
- Added validation warning when PA enablement is inconsistent across filaments used within a print

### Extrusion Widths
- Added option to choose if extrusion widths expressed as percentages are calculated from layer height or nozzle diameter

### Gap Fill
- Enabled Athena variable-width gap-fill for single-perimeter walls

### Infill / Fill
- Fixed bottom_fill_pattern ignored when support material was enabled and top contact distance was set to "No gap"

### Filament UI
- Consolidated Filament / Filament Properties into a main Properties group
- Expanded filament type dropdown from 20 to 45 types sourced from our profiles repo, sorted alphabetically

### Preprocessor Scripts
- Added jerk_by_feature.py sample script for per-feature jerk/junction deviation control (RepRapFirmware, Klipper, Marlin)

### Profiles
- Added local vendor profile import to Configuration Wizard - users can load vendor profile bundles from ZIP files with path traversal protection and security hardening

## v0.9.14

### G-code Preprocessing (Python Scripting) with 150 APIs and all settings
- Added embedded Python pre-processor for G-code scripting - users can run Python scripts against sliced G-code before preview and export with full read/write access to moves, layers, settings, and raw G-code lines
- Pre-processing runs inside the slicing pipeline giving unprecedented access never before achieved
- Bundled Python 3.14.4 runtime so end users don't need Python installed - fully isolated with no system directories, PATH, or registry modified
- Added per-profile preprocessing UI with consent-gated script execution - each settings panel (Print, Filament, Printer) gets a Preprocessing tab with an enable toggle and ordered script list
- Exposed motion planner data to scripts: distance, junction angle, acceleration, max entry speed, per-role metrics, and conflict detection
- Exposed per-fill-region geometry to scripts: region area (mm^2) and fill pattern name for detecting small features
- Added state isolation per slice: snapshots and restores sys.path, sys.modules, signal handlers, and CWD before/after each script
- Added pip support with a Python Console button in Preferences that launches a shell with PATH configured for the bundled runtime
- Added per-move annotations and optional comment parameters for insert, rewrite, prepend, and append operations
- Added error reporting: script errors, missing scripts, and cross-profile duplicates surface as breadcrumb notifications
- Added script validation: rejects invalid Python identifiers on add, blocks duplicate paths within the same profile, deduplicates across profiles at slice time
- Included 18 sample scripts (numpy, pressure advance, flow limiting, fan curves, motion optimizer, and more) with comprehensive API test
- Included preFlight.py type stub for autocomplete and intellisense in external editors
- Included comprehensive HOW TO document located in resources/preprocessor

### Profiles / Configuration Wizard
- Added ProfileServer client that fetches vendor profiles from profiles.preflight3d.com
- Rewrote Configuration Wizard: Choose Vendors page with saved selections, alphabetically sorted vendor printer pages, Type/Vendor toggle on filaments page
- Wizard saves printer, print, and filament presets as user .ini files with overwrite prompts and save-failure reporting
- Flattened preset combo boxes (removed system/user/template separators)
- Removed Compare preset button (no system reference to compare against)
- Filament page starts clean (no pre-selected profiles), defaults to Vendor > Type sort
- Added community-sourced disclaimer with link to profiles repository on filament page
- Added path traversal protection and HTTP status validation on profile downloads

### Rendering / Lighting
- Added Blinn-Phong per-pixel lighting with specular highlights and rim lighting as an "Enhanced" alternative to Gouraud shading - GPU auto-detection selects the appropriate mode
- Added user-configurable MSAA anti-aliasing dropdown (Auto/Off/2x/4x/8x/16x) in Preferences > Performance
- Added spherical harmonics studio-environment reflections to the Phong shader for subtle surface sheen
- Upgraded MMU painting gizmo shaders to Blinn-Phong with specular and rim lighting to match the 3D editor's lighting quality
- Fixed painting gizmos (seam, support, fuzzy skin, counterbore) invisible when Enhanced lighting was active due to shader mismatch causing depth buffer rejection

### UI Migration Prepwork
- Abstracted the entire rendering pipeline (GLCanvas3D, Selection, GCodeViewer, 3DScene, all 21 gizmos)
- Extracted stb_truetype from imgui into standalone bundled dependency
- Added engine boundary firewall preventing wx/imgui in libslic3r at configure time
- Routed all GL surface operations through toolkit-agnostic IGLSurface interface and replaced wxTimer-based timers with callback-driven ITimer interface
- Converted all event posts, mouse/keyboard input, and gizmo handlers to toolkit-agnostic types

### Interlocking Perimeters
- Added "Perimeters while interlocking" setting to reduce wall count on interlocking layers while retaining interlocking strength (issue #122)
- Stabilized interlocking inner contour across even/odd layers - phase 1 now uses consistent parameters so Athena's skeleton produces stable results regardless of layer parity
- Fixed interlocking shell ordering: replaced centroid-to-centroid distance matching with minimum point-to-point distance, sorted by inset index instead of shortest-path chaining
- Fixed interlocking shells clipped at narrow visibility zone boundary strips by applying morphological opening to the visibility zone
- Fixed solid infill overlapping perimeters on multi-hole thin shells after interlocking consumed interior space

### Infill / Fill
- Absorbed thin sparse slivers into adjacent bridge and solid fills - extended the solid absorption loop to also use bridge fills as absorbers with an effective gap test
- Fixed rectilinear/monotonic solid infill overlapping perimeters at interior cutouts by stretching holes directionally in the scan direction before fill generation
- Limited solid infill hole-stretch overlap fix to internal solid only - top and bottom surfaces retain full coverage at hole boundaries for surface quality
- Fixed solid infill producing empty regions on polygons with holes near contour edge by falling back to monotonic code path on malformed intersection data
- Fixed sparse infill (Grid, Triangles, Stars, Cubic) missing segments near holes due to misplaced tangent-line clipping intended for solid fills
- Fixed greedy chain walk silently dropping polylines causing large empty regions in Line sparse infill when segment clusters were disconnected
- Fixed counterbore bridge infill not merging with adjacent internal bridge due to angle-based grouping separating overlapping corridor regions

### Athena Perimeter Generation
- Fixed diamond artifacts on variable-width walls by blocking center bead splits when resulting beads would be too narrow
- Fixed thin-ring infill overlap where Clipper operations lost contour/hole winding on thin annular rings, turning a thin ring into a full disc covering perimeters

### Seams
- Fixed nip/tuck seam double-notch on thin walls where two external perimeters shared a single inner perimeter

### 3MF Import
- Fixed crash when importing config from PrusaSlicer 3MF projects (issue #125)

### G-code Preview
- Fixed G-code viewer losing base moves during incremental actual speed insertion, causing missing segment tips and wrong actual speed coloring

### Supports
- Fixed organic supports not generating inside closed perimeters due to Clipper2 migration breaking polygon winding preservation (issue #123)
- Fixed enforce-layers generating blanket support for all overhangs in paint-on mode instead of limiting to painted areas
- Fixed support crash on non-ExtrusionPath entities (ExtrusionMultiPath, ExtrusionLoop, ExtrusionEntityCollection)


## v0.9.13

### Reissue of v0.9.12 due to regression issue
- v0.9.12 introduced an Athena regression that dropped infill on geometry where the marker pass produced fragmented (open) output. The defensive PolylineStitcher guard prevented loose stitching that was accidentally compensating for that marker fragility. v0.9.13 reverts the defensive guard.

### CMYK Color Mixing - Preview
- Added CMYK color mixing gizmo for painting multi-ratio blends across a model using a per-filament palette - replaces the MMU segmentation gizmo in the toolbar while legacy MMU painted 3MFs still load and slice correctly
- Built the color prediction on Beer-Lambert transmission physics with a per-filament Transmission Distance (TD), so the preview adapts to the actual translucency of the user's spool instead of a fixed pigment-blend approximation
- Added target-color-driven palette solving - enter a hex color and the optimizer searches pairwise and single-filament candidates across all ratios, scoring each against the target with CIEDE2000 delta-E rather than making the user guess a ratio and eyeball the result
- Auto-generated palettes now include up to 512 entries (pure filaments, pairwise blends, tints, shades, chromatic+black darks, chromatic+white lights, and triples) so a single scan finds a match for every painted region across a complex model
- Extended the TriangleSelector to 16-bit state (up to 65,535 recipes per volume, 256x the upstream 8-bit limit) so complex models never run out of paintable palette slots
- Added TD1S sensor integration that closes the calibration loop end-to-end - measure the spool, write TD into the preset, and both preview rendering and slicer pattern-solving consume the measured value
- Added filament_transmission_distance to filament settings for accurate color prediction, with TD-aware convergence that scales simulation depth to the palette's max TD
- Added Alt+click eyedropper in the gizmo to pick the painted state under the cursor into the active brush slot
- Added color_mixing_base_layers and color_mixing_base_extruder to lock the bottom N layers to a single filament for a uniform bottom face
- Palette IDs are stable and cached via FNV-1a key over (colors, TDs, layer height); painted intent survives filament swaps because find_best_match re-resolves state against the runtime palette instead of being locked to physical extruder indices
- Collapsed extruder_colour into filament_colour as the single source of truth across sidebar, 3D view, G-code preview, and stored G-code extruder_colors
- Serialized color_mixing_facets and color_mixing_palette through ModelVolume save/load so undo/redo and 3MF round-trip preserve painted state

### Preferences (CPU / GPU Stability)
- Added Maximum slicing threads preference to cap TBB parallelism on unstable CPUs
- Added Prefer Performance cores preference that restricts the process to P-cores on Intel hybrid CPUs (Windows and Linux x64)
- Added Disable NVIDIA OpenGL Threaded Optimization toggle that writes a per-app driver profile via NVAPI - shown only when an NVIDIA driver is present, with inline manual instructions as a fallback

### Athena Perimeter Generation
- Fixed Athena emitting 0-width contour markers as real extrusions that appeared as long straight lines across empty space - LimitedBeadingStrategy marker junctions are now classified per line so pure markers feed only the inner contour
- Fixed Athena emitting tiny visible specks on top of existing perimeters when an even-paired wall couldn't close into a loop (orphan fragments smaller than their own bead width are now dropped).

### Brim / Mouse Ears
- Rewrote inner brim generation to compute width-bounded rings around all solid boundaries inside holes, filled with concentric Athena loops and quantized to whole beads to eliminate compressed extra loops
- Fixed an infinite loop in inner brim where the NORMAL group's brim_width was never set
- Rewrote painted mouse ear clipping to offset all solid boundaries by the overlap amount and diff from the ear circle in one operation, handling outer contours and inner holes uniformly
- Fixed painted mouse ears bypassing no_brim_area so per-ear overlap settings are preserved
- Preserved concentric brim adhesion order so each loop sticks to the previous one instead of being reordered by entity chaining

### Cooling
- Added "Don't slow down outer walls" per-filament setting that exempts external perimeters from the cooling buffer's minimum-layer-time slowdown, preventing wall thickness taper on thin-walled parts - imports directly from OrcaSlicer profiles

### G-code
- Fixed over-bridge speed bypassing filament_max_volumetric_speed - the override was injected after cap_speed() had been applied, allowing solid infill above bridges to exceed the volumetric limit

## v0.9.12

### CMYK Color Mixing - Preview
- Added CMYK color mixing gizmo for painting multi-ratio blends across a model using a per-filament palette - replaces the MMU segmentation gizmo in the toolbar while legacy MMU painted 3MFs still load and slice correctly
- Built the color prediction on Beer-Lambert transmission physics with a per-filament Transmission Distance (TD), so the preview adapts to the actual translucency of the user's spool instead of a fixed pigment-blend approximation
- Added target-color-driven palette solving - enter a hex color and the optimizer searches pairwise and single-filament candidates across all ratios, scoring each against the target with CIEDE2000 delta-E rather than making the user guess a ratio and eyeball the result
- Auto-generated palettes now include up to 512 entries (pure filaments, pairwise blends, tints, shades, chromatic+black darks, chromatic+white lights, and triples) so a single scan finds a match for every painted region across a complex model
- Extended the TriangleSelector to 16-bit state (up to 65,535 recipes per volume, 256x the upstream 8-bit limit) so complex models never run out of paintable palette slots
- Added TD1S sensor integration that closes the calibration loop end-to-end - measure the spool, write TD into the preset, and both preview rendering and slicer pattern-solving consume the measured value
- Added filament_transmission_distance to filament settings for accurate color prediction, with TD-aware convergence that scales simulation depth to the palette's max TD
- Added Alt+click eyedropper in the gizmo to pick the painted state under the cursor into the active brush slot
- Added color_mixing_base_layers and color_mixing_base_extruder to lock the bottom N layers to a single filament for a uniform bottom face
- Palette IDs are stable and cached via FNV-1a key over (colors, TDs, layer height); painted intent survives filament swaps because find_best_match re-resolves state against the runtime palette instead of being locked to physical extruder indices
- Collapsed extruder_colour into filament_colour as the single source of truth across sidebar, 3D view, G-code preview, and stored G-code extruder_colors
- Serialized color_mixing_facets and color_mixing_palette through ModelVolume save/load so undo/redo and 3MF round-trip preserve painted state

### Preferences (CPU / GPU Stability)
- Added Maximum slicing threads preference to cap TBB parallelism on unstable CPUs
- Added Prefer Performance cores preference that restricts the process to P-cores on Intel hybrid CPUs (Windows and Linux x64)
- Added Disable NVIDIA OpenGL Threaded Optimization toggle that writes a per-app driver profile via NVAPI - shown only when an NVIDIA driver is present, with inline manual instructions as a fallback

### Athena Perimeter Generation
- Fixed Athena emitting 0-width contour markers as real extrusions that appeared as long straight lines across empty space - LimitedBeadingStrategy marker junctions are now classified per line so pure markers feed only the inner contour

### Brim / Mouse Ears
- Rewrote inner brim generation to compute width-bounded rings around all solid boundaries inside holes, filled with concentric Athena loops and quantized to whole beads to eliminate compressed extra loops
- Fixed an infinite loop in inner brim where the NORMAL group's brim_width was never set
- Rewrote painted mouse ear clipping to offset all solid boundaries by the overlap amount and diff from the ear circle in one operation, handling outer contours and inner holes uniformly
- Fixed painted mouse ears bypassing no_brim_area so per-ear overlap settings are preserved
- Preserved concentric brim adhesion order so each loop sticks to the previous one instead of being reordered by entity chaining

### Cooling
- Added "Don't slow down outer walls" per-filament setting that exempts external perimeters from the cooling buffer's minimum-layer-time slowdown, preventing wall thickness taper on thin-walled parts - imports directly from OrcaSlicer profiles

### G-code
- Fixed over-bridge speed bypassing filament_max_volumetric_speed - the override was injected after cap_speed() had been applied, allowing solid infill above bridges to exceed the volumetric limit

## v0.9.11

### Preview / Legend
- Added Job Estimate section to G-code preview legend showing estimated print time and filament usage
- Persisted legend toggle button states across application restarts (seams, tool markers, etc)

### Align to Face Gizmo
- Fixed subtract operation not baking the CGAL boolean result into the mesh
- Fixed snap points invisible in orthographic camera mode

### Athena Perimeter Generation
- Fixed center bead split not producing correct source polygon tags
- Fixed Narrow to Athena not triggering on long narrow surfaces
- Fixed polyline stitcher rejecting valid connections across source polygons

### Infill / Fill
- Fixed monotonic fill line ordering scrambled by entity-level reorder
- Fixed infill overshoot beyond perimeters on certain geometries
- Fixed bridge anchor overlapping sparse infill and perimeters in certain geometry

### Cooling
- Fixed fan speed restoration after overhang/bridge regions

### Orca Import
- Fixed Orca import dropping acceleration values specified as percentages

### Bug Fixes
- Fixed painted brim ears not clipping against adjacent object instances
- Fixed negative volume visibility after Align gizmo operations
- Fixed black screen after drag-and-drop file load on all platforms

## v0.9.10

### Windows ARM
- Added Windows ARM64 native support for compatible hardware

### Counterbore Bridge Gizmo
- Added Counterbore Bridge paint-on gizmo for controlling bridge layers above counterbore holes
- Per-hole bridge layer count with smart fill that only bridges painted regions
- Fill direction follows corridor angle for each painted hole independently
- Transition starts at the painted layer and extends upward through the specified bridge count
- Activated through new gizmo menu item in the vertical toolbar

### Relief Gizmo
- Added Relief gizmo for embossing images as 3D heightmaps onto mesh surfaces
- Smoothing, gamma correction, and minimum thickness controls
- Real-time preview with CSG subtraction result shown in slicing shell animation
- Activated through right clicking on the platter

### Athena Perimeter Generation
- Allowed center bead split when wide enough for two beads
- Deduplicated overlapping center-pair perimeters in Athena toolpaths
- Fixed Athena center-pair overlap when perimeter overlap was active
- Fixed out-of-bounds in LimitedBeadingStrategy marker placement
- Enforced nozzle-based minimum bead width

### Interlocking Perimeters
- Interleaved interlocking with perimeters by structural feature ID for correct ordering
- Fixed interlocking inner contour missing corners, leaving infill voids
- Fixed interlocking shell spacing for even-layer boundary bead
- Fixed interlocking flow boundary detection to check both layers

### Overhang Perimeters
- Discarded extra overhang perimeters when oscillation was detected
- Fixed infinite loop in overhang perimeter generation

### Infill / Fill
- Merged stBottom with adjacent solid fill when bridge_no_gap was OFF
- Fixed bridge infill gaps when bridge_infill_overlap was above 0%
- Fixed crash in rectilinear fill when slicing dense relief meshes

### Seams
- Fixed seam notch taper creating tiny travel moves at element boundaries

### Security
- Suppressed post-process scripts in 3MF imports (CVE-2023-47268)
- Confirmed preFlight is not affected by the Zip Slip path traversal vulnerability - all 3MF extraction uses in-memory buffers

### G-code / Post-Processing
- Fixed G-code command window not displaying when binary G-code support was enabled
- Fixed post-processing scripts on exported G-code from virtual file
- Fixed several post-processing issues

### Orca Import
- Fixed Orca import silently destroying ConfigOptionStrings array values

### Platform / Build
- Added unified build script for all platforms
- Unified Windows deps build path to deps/build
- Renamed platform identifiers to win-amd64/linux-amd64
- Fixed deps build reliability on Windows

### UI / Bug Fixes
- Fixed bridge infill double extrusion along obstacle boundaries
- Fixed text emboss gizmo closing on keypress and restored font preview rendering
- Fixed negative volumes invisibility
- Fixed Slice Platter button getting stuck on Export G-code
- Fixed preset save dialog rejecting valid names with shared prefixes
- Guarded delete_preset against nonexistent preset names
- Rebuilt preset maps after deletion to prevent stale matches on re-import
- Suppressed all config validation dialogs during startup to prevent splash deadlock
- Renamed bridge_no_gap label for clarity

## v0.9.9

### Align to Face Gizmo
- Added Align to Face gizmo for precise object alignment using face selection, snap points, and boolean operations (weld and subtract)
  - Flip, scale/size with proportional lock, depth slider, position/nudge, Shift+drag snap-to-point with visual ring indicator

### Interlocking Perimeters
- Promoted interlocking perimeters to the true perimeter generator with full pipeline integration
- Coverage-walking visibility system with clipping and travel ordering
- Contour-ring feature ordering with seam joining
- Reduced interlocking flow to 100% on top layer
- New Solid layers above/below option to precisely start/stop interlocking inside the horizontal shells

### Painting / Splitting
- Preserved MMU painting, fuzzy skin, and seam data through Split to Parts and Split to Objects

### Bug Fixes / Improvements
- Fixed solid infill gaps between fill and innermost perimeters on bottom/top solid layers caused by progressive edge erosion during horizontal shell propagation
- Fixed solid infill overflowing into object through-holes when hole removal failed to recognize real object features vs trimming artifacts
- Fixed excessive solid infill consuming entire sparse regions on small objects at low infill densities - reduced absorption threshold from 16x to 4x line spacing squared
- Added erosion test to solid hole removal so thin projected features from geometry above are filled solid instead of left as unfillable sparse gaps
- Fixed large top/bottom surfaces being falsely classified as narrow, causing them to be skipped or have their fill pattern overridden
- Fixed greedy polyline chaining silently dropping disconnected segment clusters, which caused missing infill on some layers with Adaptive Cubic
- Fixed thin solid bridge anchor strips producing no fill by retrying with a smaller boundary offset
- Replaced Narrow to Concentric with Narrow to Athena - narrow solid surfaces now use variable-width fill instead of concentric, eliminating zigzag and diamond artifacts at narrow transitions
- Fixed small perimeter speed percentage resolving against internal perimeter speed for all perimeters - now correctly resolves against external perimeter speed for external perimeters
- Reordered speed settings to show Perimeters, External perimeters, then Small perimeters - clarifies that Small perimeters applies to both
- Fixed time-based legend showing false bands for near-identical layer times
- Fixed preview gap rendering when M207 Z retract lift equals layer height
- Fixed fill density field multiplying value by 100x when entered without a percent sign in the sidebar or loaded from configs without the % suffix
- Fixed preview clipping plane persisting during slicing shell animation after reslice
- Fixed slicing animation freezing when switching to another window during slice

## v0.9.8

### Interlocking Perimeters
- Replaced polygon-offset shell generation with Athena's skeletal trapezoidation engine for interlocking perimeters - naturally handles narrow channels and bead count transitions
- Redesigned interlocking bead geometry to use three flow-scaled tiers (100%, ~146%, 200%) - the ~146% boundary bead width is derived so outer edges align with the 100% boundary bead on alternate layers to keep inter-shell gaps uniform
- Replaced flow-rate overlap with geometric centerline spacing modification - overlap bonding now works the same way as perimeter-to-perimeter overlap, eliminating over-extrusion at shell boundaries

### Alternating Nip/Tuck Seams
- Added Alt. Nip/Tuck seam type - alternates between Nip on even layers and Tuck on odd layers to distribute seam disturbance across both sides of the junction

### Orca Import
- Added warning dialog before OrcaSlicer profile import informing users that imported profiles will need careful review due to differences between applications

### Print Host
- Fixed RRF standalone machine limits race condition where stale rr_reply queue entries caused values to be incorrectly applied to the wrong fields in stand-alone mode

### Preview/Legend
- Modified fan speed legend to always show 10 fixed bands (0-10%, 11-20%, ..., 91-100%) regardless of data distribution
- Capped all range-based legends to 10 bands maximum - values exceeding 10 distinct groups are merged via frequency-weighted quantiles

### Bug Fixes
- Fixed slicing animation not switching to Preview tab during slice
- Fixed accidental object deletion in Preview when pressing Delete/Backspace - objects are now deselected when slicing begins
- Fixed empty sidebar groups not hiding on GTK/macOS due to spacers being counted as visible rows (thanks topisani!)
- Accumulated partial scroll events on XWayland are no longer silently dropped (thanks topisani!)

### UI
- Added "View release page" hyperlink to the update available dialog


## v0.9.7

### Raspberry Pi
- Added RPi 5 support for 64-bit Raspberry Pi OS
- OpenGL 3.1 / GLSL 1.40 with flat shader fallback
- Now compatible with both Bookworm and Trixie

### macOS Support
- preFlight for macOS is now digitally signed and notarized

### New Features
- **Painted Seam Alignment**: Bidirectional blending system for stable vertical and diagonal seam tracking - forward pass tracks diagonal seams while filtering vertex noise, backward pass straightens early-layer convergence lag. Only activates for painted enforcers on smooth surfaces
- **Travel Optimization**: Replaced default extrusion ordering with nearest-neighbor chaining across the G-code pipeline - reduced unnecessary travel moves between islands, added 2-opt refinement for shorter travel paths, cross-fragment polyline chaining for connected infill lines
- **Nip and Tuck Seams**: Added two new seam types: Nip, and Tuck. Nip conceals the start point. Tuck conceals the end point.
- **Athena Thin Wall Width Precision**: Added user-configurable snap grid (0.001 - 0.1mm) under Print Settings > Advanced to control width oscillation on uniform thin walls
- **Legend-Specific Tooltips**: Legend-specific values now appear on G-code horizontal slider
- **3mf Warnings**: Added warning when opening 3mf files from other slicers about configuration differences

### Infill / Fill Improvements
- Absorbed small sparse infill gaps into adjacent solid fills - eliminated unfilled holes/gaps within solid infill on layers above bridges and internal solid floors
- Merged fragmented bridge infill regions into unified fills with correct bridge angle
- Fixed solid infill merge: boundary clipping, adjacency transfer, and hole safety to prevent overlap, flooding, and top-surface overwriting
- Fixed SOB/InternalSolid merge to use geometric adjacency instead of layer-wide thin heuristic, and corrected tiny-SOB removal threshold from sparse density to solid fill spacing
- Skipped bridge-over-infill for single-layer sparse gaps - prevented monotonic bridge pattern on isolated layers sandwiched between normal infill
- Optimized concentric fill: cluster spatially adjacent loops, rotate to nearest vertex

### G-code
- Eliminated redundant standalone `G1 F` lines
- Fixed manual fan controls producing no M106 for non-bridge features
- Stopped emitting machine envelope G-code for RRF/Rapid/Klipper firmware
- Fixed dynamic overhang speed bucket snapping with sane defaults

### Athena / Wall Generation
- Added Athena support for concentric infill when Athena is selected as the perimeter generator
- Fixed certain geometry by treating small polygons as thin walls
- Fixed thin-wall fragmentation under certain conditions

### Crash Fixes
- Fixed empty Preview after slicing caused by GL context loss during gcode loading
- Fixed Clipper2 stack overflow crash
- Fixed concentric fill hang
- Fixed int64 overflow in Clipper2 Z callback

### Bug Fixes
- Fixed painted mouse ears merging with overlapping merged ears
- Fixed painted mouse ears overlapping object under certain geometry
- Modified mouse ears to use Athena perimeter generator for better coverage
- Fixed preview rendering bug when retract_lift equals layer_height
- Auto-corrected Orca-format shrinkage compensation values on config load
- Fixed enforce_layers generating support when no auto or painted supports existed

### GPU / Rendering
- Rolled back over-zealous GPU power-saving event suppression that caused delayed context
- Scoped GPU power-saving to Preview tab only - Platter reverts to stock responsiveness

### UI Fixes/Changes
- Layer slider position remains on current layer after reslice
- Previous layers now darkened in G-code preview except during full render
- Fixed first mouse scroll over ImGui windows (e.g. G-code command legend) zooming the canvas instead of scrolling
- Fixed G-code command legend highlighted line not centered during scroll
- Fixed sidebar items not hiding when individually unticked
- Fixed object settings panel not expanding to fill available sidebar space
- Fixed macOS gizmo tooltips to show "Cmd" instead of "Ctrl"

### Packaging
- Fixed Linux build issues with CGAL GMP guard


## v0.9.6

### macOS Support (New)
- Added macOS 11.0+ support for Apple Silicon
- Dark mode and Retina display support
- ***preFlight for macOS is currently not digitally signed - this will be finalized soon and released in v0.9.7***

### New Features
- Allow Slice Platter from any tab (not just Prepare)
- Enabled background processing preference in settings so users can opt in to automatic slicing
- Added "Remember my choice" to upload overwrite dialog and "Reset Upload Preferences" button in Send G-Code dialog

### DPI / Multi-Monitor Fixes
- Fixed ImGui Legend sidebar rendering at wrong width after cross-monitor DPI change
- Fixed DPI scaling corruption when dragging window across monitors - full rescale now triggers on drag end
- Fixed sidebar preset combo box text vertical centering after DPI change

### GPU / Rendering
- Fixed GPU retention after interaction in Preview canvas

### Bug Fixes
- Fixed fan ramp segment split producing wrong E values in absolute E mode
- Fixed missing icons in Settings/Export dropdowns after DPI fix broke uncached bitmap items
- Fixed MsgDialog HTML content rendering with white background on Windows
- Fixed missing tree view icons in settings

### Linux
- Clipped popup menu background to rounded borders on GTK3


## v0.9.5

### Print Host Improvements
- Fixed host upload crash when sending large files to printer
- Added file overwrite protection - checks if the file already exists on the printer before uploading and prompts to overwrite or rename (Duet DSF/RRF, OctoPrint, LocalLink, Moonraker)
- Added post-upload prompt to switch to the Printer WebView tab (with "Remember my choice" option)
- Changed Duet connection order to try DSF before RRF - SBC-based printers no longer waste a failed RRF request on every connection

### Orca Import Improvements
- Resolved most `@System` filament inheritance - imported profiles now get correct values instead of falling back to defaults
- Added "Yes to All / No to All" buttons to overwrite and validation dialogs so large imports don't require clicking through every duplicate
- Auto-appended `[0]` index to vector variables during G-code placeholder translation to prevent post-import parsing errors
- Hardened import pipeline: per-profile error handling so one failure doesn't abort the batch, always show results dialog, reject empty/corrupt bundles with a clear message
- Added 27 new key mappings (acceleration, overhang speeds, bridge flow, line widths, infill anchors, resolution, wall distribution, and more) and registered 53 additional Orca-only keys so they are properly classified instead of falling through as unknown

### Preview/Legend Improvements
- Replaced linear color range with frequency-aware band system for the preview legend - outliers no longer compress useful data into a single color; bands are based on quantile splitting of actual value frequencies
- Enabled preview layer ruler by default

### Cooling
- Made "Enable manual fan speeds" and "Enable auto cooling" mutually exclusive to prevent auto cooling from overriding manual fan settings
- Updated cooling hint text to guide users toward manual controls

### Bug Fixes
- Fixed placeholder parsing inside G-code comments - variables after `;` no longer trigger parse errors
- Restored sidebar and allow reslice after a slicing error
- Fixed Stealth Mode column persisting in Machine Limits when toggled from sidebar
- Fixed stale lock icons when parent preset was temporarily null during load
- Fixed thin-walled geometry collapse in mesh slicer closing operation
- Fixed division by zero crash in rectilinear fill segment intersection
- Fixed interlocking perimeters missing on combined-infill void layers
- Fixed over-bridge speed having no effect on solid infill above bridges
- Fixed SSL certificate revocation check unavailable on Linux/macOS

### UI/Theme Improvements
- Replaced native scrollbars with custom themed scrollbars in all message dialogs for consistent dark mode appearance
- Improved text fields: tooltips dismiss upon typing and added right-click context menu (Undo/Cut/Copy/Paste/Delete)
- Settings description text now wraps dynamically to available panel width
- Eliminated expensive full-app rescale when dragging window between monitors - removes visible lag on multi-monitor setups
- Replaced fuzzy search in the settings search dialog with contiguous substring match for more predictable results
- Mouse wheel scrolling in multiline text areas now requires clicking inside the field first to prevent accidental changes

### Linux
- Fixed OCCTWrapper.so not found for STEP file import

## v0.9.4

### New Features
- **OrcaSlicer Bundle Import**: Import printer, filament, and process profiles from `.orca_printer`, `.orca_filament`, and `.zip` bundles via File > Import > Import OrcaSlicer Bundle — includes key mapping with value transforms, bed temperature plate selection, G-code macro translation, and a results dialog showing imported profiles, lossy mappings, dropped settings, and G-code warnings

### Bug Fixes
- Fix Nip/Tuck only processing the first external perimeter per island — now handles multiple external perimeters correctly
- Improve seam vertical alignment by increasing snap tolerance to eliminate zigzag drift from polygon vertex discretization
- Skip staggered seam on outermost inner perimeter when Nip/Tuck is enabled to keep the trimmed gap aligned with the V-notch
- Fix Printer Settings sections (Capabilities, Machine Limits, RRF M-codes) not hiding when unchecked in sidebar visibility toggles
- Fix native scrollbar bleed-through in multiline TextInput fields
- **Camera View Shortcuts**: Number key view shortcuts (1–6) now recenter on the build plate

### Linux
- Fix AppImage WebKit crash on Arch and non-Debian distros caused by patching order leaving hardcoded `/usr` paths in library copies
- Add EGL probe to prevent WebKit crash on VMs without working GPU — falls back to system browser when EGL initialization fails

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
