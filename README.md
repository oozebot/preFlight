<p align="center">
  <img src="resources/icons/preFlight.png" alt="preFlight logo" width="600">
</p>

# preFlight

**The Engineer's Slicer**

preFlight is an advanced 3D printing slicer built for precision and performance. Building on the Slic3r legacy as a spiritual successor to PrusaSlicer, it offers exclusive features and a comprehensive under-the-hood overhaul, bringing the entire dependency stack up to modern standards. Given this massive modernization, preFlight has evolved beyond the constraints of the original codebase, making upstream merging irrelevant.

## oozeBot

Based in Georgia, USA, oozeBot is a small but ambitious team currently preparing for the take-off of our Elevate line of 3D printers. preFlight is the cornerstone of the ecosystem we are building - a genuinely new option in the 3D printing space designed to benefit all makers, regardless of the hardware they use.

## Donate

While preFlight is open-source and free for everyone, your support helps us maintain the infrastructure, fund R&D, and keep our team in orbit. If you find value in our tools, consider contributing to the mission.

[Support the preFlight Mission (via Stripe)](https://donate.stripe.com/eVqfZbgoVf9y1c1aXe63K00)

## Requirements

**Windows and Linux.** macOS support coming soon.

**Windows:** [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) - Install this first if preFlight won't launch.

**Linux:** Download the AppImage from [GitHub Releases](https://github.com/oozebot/preFlight/releases), make it executable (`chmod +x`), and run. No installation required.

## Security & Authenticity

To ensure the integrity of your installation and protect yourself, please following these security guidelines:

* **Official Downloads:** Only download preFlight binaries directly from our [GitHub Releases](https://github.com/oozebot/preFlight/releases) page. We do not distribute preFlight through third-party mirror sites.
* **Verified Signature:** All official Windows binaries are digitally signed by **oozeBot, LLC** using an **Organization Validation (OV) Code Signing Certificate**. 
* **Verification:** Before running the installer, right-click the file, select **Properties**, and navigate to the **Digital Signatures** tab. Ensure the "Name of signer" is explicitly listed as **oozeBot, LLC**.
* **Safety First:** If you receive a "Windows protected your PC" (SmartScreen) warning on a file that is *not* signed by oozeBot, LLC, do not proceed with the installation and [report the issue](https://github.com/oozebot/preFlight/issues) immediately.

## Why preFlight?

| What You Get | The Difference |
|--------------|----------------|
| **Athena Perimeter Generator** | Independent overlap control no other slicer offers |
| **Interlocking Perimeters** | Enhanced Z-bonding without added cost or complexity |
| **True 64-bit Architecture** | No coordinate overflow, no silent failures |
| **High Precision** | Clipper2 compiled with 10-decimal high precision |
| **In-Memory Processing** | No temp files, ~50% less RAM usage |
| **Modern Stack** | C++20, Clipper2, Boost 1.90, CGAL 6.1, OpenCASCADE 7.9, Eigen 5.0 |

---

## Flagship Features

### Athena Perimeter Generator

In Greek mythology, Athena defeated Arachne not through greater complexity, but through discipline and precision. We named our perimeter generator after her for the same reason.

**Why Athena Exists**

We forked Arachne to modernize it in several ways. Athena uses **fixed extrusion width** instead of variable and **independent overlap control** between perimeters. Arachne calculates overlap automatically. Athena lets you specify exactly how much perimeters overlap. It even enables negative overlap for creating gaps between perimeters.

#### Unique Controls

| Setting | What It Controls | Range |
|---------|------------------|-------|
| `Ext. perimeter/perimeter overlap` | Gap between external wall and first internal wall | -100% to +100% |
| `Perimeter/perimeter overlap` | Gap between all internal perimeters | -100% to +80% |
| `Perimeter compression` | How aggressively perimeters narrow in tight areas | |

- **Positive overlap**: Perimeters merge into each other (stronger bonding)
- **Zero overlap**: Perimeters just touch
- **Negative overlap**: Gap between perimeters (useful for flexible or soft materials)

#### Additional Characteristics

- Fixed extrusion widths with variation absorbed in spacing, not width
- Predictable wall shell thickness
- Full thin wall support

**When to Use Athena:** You need control over how perimeters bond, want consistent external perimeter width, or are tuning for strength/flex behavior.

**When to Use Arachne:** You prefer automatic overlap calculation or don't care about perimeter spacing.

### Interlocking Perimeters

A novel approach to layer bonding using **spacing variation and compression bonding** - fundamentally different from "brick layers".

**How it works:**
- Alternates perimeter spacing between layers (X/Y axis manipulation)
- Over-extrusion compresses material into horizontal gaps
- Creates diagonal bonding surfaces as material compresses into gaps
- All beads printed at **constant layer height** (no Z-axis manipulation)

**Key distinction from "brick layers":**
| Aspect | Brick Layers (others) | Interlocking (preFlight) |
|--------|----------------------|--------------------------|
| **Mechanism** | Height variation (Z-axis) | Spacing variation (X/Y axis) |
| **Bead heights** | Variable (half/full) | Constant |
| **Bonding type** | Geometric interlocking | Compression bonding |

**Benefits:**
- 5-15% strength increase (estimated)
- No material or time penalty at 100% strength
- Maintains dimensional accuracy (constant layer heights)
- Requires Athena perimeter generator

---

## Exclusive Features

### True 64-bit Architecture

**64-bit coordinate types throughout.**

```cpp
// PrusaSlicer/OrcaSlicer/SuperSlicer:
using coord_t = int32_t;  // Overflow risk

// preFlight:
using coord_t = int64_t;  // No overflow, native Clipper2
```

Why it matters:
- 32-bit coords overflow in cross products with large coordinates
- Clipper2 uses 64-bit internally - type mismatch causes bugs
- Large print volumes can exceed 32-bit range

### In-Memory G-code Processing

Zero temp files during slicing:
- No disk I/O during slicing
- ~50% less RAM (no per-line string overhead)
- Faster slicing (no file system operations)

### Multi-Type Support Painting

**Mixed support types on a single object.**

- Paint **Snug** (Blue) - Strong, close contact
- Paint **Grid** (Orange) - Balanced strength/removal
- Paint **Organic** (Green) - Easy removal, delicate areas

Strong supports under critical overhangs, easy-to-remove supports elsewhere. All on one print.

### Paint-on Seams Line Drawing Mode
- Draw straight seam lines between points instead of painting freehand. Perfect for placing seams along edges or in straight grooves. Includes Z-axis snapping (within 5°) for vertical lines.
- Minimum brush size reduced to 0.1mm

### 2-opt Travel Optimization

Intelligent perimeter ordering eliminates crossing travel paths:
- Centroid-based starting position for better initial grouping
- 2-opt optimization algorithm to eliminate crossing travel paths
- Adaptive iteration count scaling with group complexity

### Region-Aware Infill Ordering

Intelligent print ordering minimizes travel:
- Interlocking perimeters build containment tree
- Concentric fill uses depth-first traversal
- Sparse infill uses union-find clustering
- 30-50% reduction in travel distance for gyroid on multi-island layers

### RepRapFirmware Direct Integration

**Direct RRF integration for automatic machine limit configuration.**

One-click retrieval of all machine limits directly from Duet/RRF printers:
- M566 (Jerk), M201 (Acceleration), M203 (Feedrate)
- M204 (Print/travel accel), M207 (Firmware retract)

### Physics-Based Time Estimation

- Junction Deviation support (Marlin M205 J)
- RepRapFirmware acceleration model
- Print time estimates that actually match reality

### G-code Complexity Analysis

"Max cmd/s" column shows:
- Maximum commands per second for each extrusion role
- Which layer the maximum occurred on
- Identify bottlenecks before printing

### Enhanced G-code Viewer

Interactive G-code exploration for troubleshooting and analysis.

**Mouse Wheel Scrubbing:**
Hover over the G-code window and scroll to scrub through commands in real-time. The 3D preview updates as you scroll, letting you pinpoint exactly which command produces which movement. Right-click any G-code command to instantly copy to your clipboard.

**Additional Features:**
- Keyboard arrow navigation (command-by-command)
- Right-click to copy any command to clipboard
- Width matched to legend for clean UI

### Print Quality Enhancements

- **Top Surface Flow Reduction** - Reduce flow on top layers for smoother finish
- **Narrow Solid to Concentric** - Automatically switch to concentric pattern when solid areas are too narrow for rectilinear
- **Bridge Infill Overlap** - Independent control over overlap between bridge extrusions

### Support System Redesign

- **Always-Synced Support Layers** - Support layers perfectly align with object layers (no fractional heights)
- **Variable Interface Layer Heights** - Achieve desired gap through interface height adjustment, not Z-offset
- **Simplified Bottom Contact** - Clear options: No Gap, Half Layer, Full Layer (instead of arbitrary mm values)

### Cooling & Extrusion Control

- **Full Manual Fan Control** - Complete manual cooling control for each feature type
- **Fan Spin-Up Options** - Configure fan spin-up timing for precise cooling with overhang perimeters and bridge infill
- **Wipe Enhancements** - Improved wipe/retraction behavior

### Additional Features

- **Layer 0 Preview** - See blank build plate before first extrusion
- **Preview Slider Accelerators** - Ctrl (2x), Shift (4x), Ctrl+Shift (8x) navigation
- **Settings Tab Auto-Commit** - Click dead space to commit fields
- **Progressive Slicing Feedback** - Visual feedback during slicing
- **Solid Fill Pattern Selection** - Choose pattern for internal solid layers

---

## Ported Features (with additional enhancements)

### Mouse Ear Brims
Ported from OrcaSlicer with enhancements:
- Per-ear overlap control (-100% to +100%)
- Always-visible rendering
- Full undo/redo support

### Paint-on Fuzzy Skin
Ported from OrcaSlicer with enhancements

**Noise Types:**
| Type | Effect |
|------|--------|
| **Classic** | Original random displacement |
| **Perlin** | Smooth, organic patterns |
| **Billow** | Cloud-like, puffy texture |
| **Ridged Multi** | Sharp, jagged features |
| **Voronoi** | Cell-based patchwork patterns |

**Advanced Controls:**
- **Visibility Detection** - Optional skip fuzzy on top-visible surfaces
- **Bottom Detection** - Optional skip fuzzy on bottom layers - always skip overhangs
- **Per-Perimeter Control** - Limit fuzzy to external perimeters only
- **Point placement** - Standard or Shape following
- **Fuzzy skin mode** - Displacement, Extrusion, Combined
- **Scale** - Feature size in mm
- **Octaves** - Detail levels (noise complexity)
- **Persistence** - How much each octave contributes

---

## Modern Infrastructure

### Core Libraries

| Library | Version | Notes |
|---------|---------|-------|
| **Clipper2** | 2.0.1 | Polygon clipping with native 64-bit support |
| **Boost** | 1.90.0 | Filesystem, logging, threading, geometry |
| **CGAL** | 6.1 | Computational geometry (Boost.Multiprecision backend) |
| **OpenCASCADE** | 7.9.3 | STEP/IGES CAD file import |
| **Eigen** | 5.0.0 | Linear algebra (vectors, matrices, transforms) |
| **TBB** | 2022.3.0 | Parallel task execution |
| **NLopt** | 2.10.0 | Nonlinear optimization |
| **GLAD** | 2.0.8 | OpenGL loader |

### Removed Legacy Dependencies
- **GMP/MPFR** - CGAL 6.x uses Boost.Multiprecision instead
- **OpenCSG** - Dead/unused code removed
- **GLEW** - Replaced with actively maintained GLAD

### Build Improvements
- C++20 compilation
- Memory leak fixes (gigabytes saved over long sessions)
- CMake 4.x ready

---

## Compatibility

preFlight works with any modern 3D printer accepting RepRap-flavored G-code:
- Marlin firmware
- Prusa firmware
- RepRapFirmware (Duet)
- Klipper
- Smoothieware

### Supported Formats
- **Input**: STL, OBJ, 3MF, AMF, STEP
- **Output**: G-code for FFF printers

---

## Building from Source

### Windows

**Requires Visual Studio 2026** (VS 2022 is not supported)

```bash
# Build dependencies (first time only)
build_win.bat -STEPS deps

# Build release
run_build.bat -ninja

# Build debug (for development)
run_build.bat -ninja -debug
```

**Build Options:**
| Flag | Description |
|------|-------------|
| `-ninja` | Use Ninja build system (recommended) |
| `-debug` | Build debug configuration |
| `-clean` | Full rebuild from scratch |
| `-flush` | Pick up image/resource changes |

### Linux

```bash
# Build dependencies (first time only)
./build_deps.sh

# Build release
./build_linux.sh

# Package as AppImage
./pack_appimage.sh
```

### macOS

Not yet available.

---

## Documentation

Coming soon.

---

## License

preFlight is licensed under the **GNU Affero General Public License, version 3**. See [LICENSE](LICENSE) for details.

preFlight is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is based on [Slic3r](https://github.com/slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

---

## Support

- **Email:** [support@ooze.bot](mailto:support@ooze.bot)
- **GitHub Issues:** [github.com/oozebot/preFlight/issues](https://github.com/oozebot/preFlight/issues)

---

*preFlight - Precision where it matters.*
