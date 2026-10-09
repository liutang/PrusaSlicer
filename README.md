
![PrusaSlicer logo](/resources/icons/PrusaSlicer.png?raw=true)

# PrusaSlicer 2.9.6 Extended

PrusaSlicer 2.9.6 Extended is a community fork of PrusaSlicer 2.9.6 that adds quality of life improvements to the stable 2.x release, with a focus on multi-color and multi-material printing on the Bondtech INDX.

PrusaSlicer 2.9.6 is the last release of the 2.x line. Prusa has moved its development to PrusaSlicer 3.x, which is not ready yet. Until it is, this fork keeps 2.9.6 as the base and builds on it:

* improvements for multi-toolhead printing, especially the 8-toolhead INDX
* selected improvements ported from [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) where they are needed
* no changes to file formats or settings, so projects and profiles stay compatible with PrusaSlicer 2.9.6

The application identifies itself as version `2.9.6-extended` in the title bar, the splash screen, About and System Info. Project files, G-code and configuration still carry the plain `2.9.6` version.

This is not an official Prusa Research release.

## Enhancements

| Enhancement | What it does |
|---|---|
| [Swap toolheads](#swap-toolheads) | Exchange two toolheads, with their filaments and everything in the project that refers to them. |
| [Extruder numbers in the sidebar](#extruder-numbers-in-the-sidebar) | Number each filament selector so you can tell the extruders apart. |
| [Base layer under the support interface](#base-layer-under-the-support-interface) | Print one dense layer of support base material under an interface made of a different material. |

### Swap toolheads

A multi-material project assigns each filament to a specific toolhead, and that assignment often does not match the order in which filaments are loaded on your printer. Swapping two toolheads exchanges their filaments and updates everything in the project that refers to them, so the project matches the printer and can be printed without remapping filaments at the printer.

Click the gear button next to a filament in the sidebar and choose **Swap with extruder**, then pick the other extruder.

<img src="/doc/images/toolhead-remap/swap-with-extruder.png?raw=true" alt="Swap with extruder menu on a filament in the sidebar" width="418">

The filament presets and colors move together with the object and part assignments, multi-material painting, tool and color changes on the layer slider, purging volumes and the extruder numbers in the print settings. Nozzle diameter, extruder offsets and retraction settings stay with the physical toolhead. Undo and redo work across a swap.

### Extruder numbers in the sidebar

On a multi-extruder printer, each filament selector in the sidebar has a numbered cell on its left, so you can tell which extruder is which at a glance. The numbers belong to the extruder positions and stay in place when filaments are swapped.

### Base layer under the support interface

When the support interface is printed with a different extruder than the support base, for example a PLA interface on PETG supports, one dense layer of the base material is printed directly under the interface layers. The interface material then lies on a continuous surface and does not have to bridge the gaps of the sparse support base.

The layer is added to the configured number of top interface layers, it does not replace one of them. It applies to the Grid, Snug and Organic support styles. Supports printed with a single extruder and soluble interfaces are not changed. The behavior is adapted from OrcaSlicer.

# PrusaSlicer

You may want to check the [PrusaSlicer project page](https://www.prusa3d.com/prusaslicer/).
Prebuilt Windows, OSX and Linux binaries are available through the [git releases page](https://github.com/prusa3d/PrusaSlicer/releases) or from the [Prusa3D downloads page](https://www.prusa3d.com/drivers/). There are also [3rd party Linux builds available](https://github.com/prusa3d/PrusaSlicer/wiki/PrusaSlicer-on-Linux---binary-distributions).

PrusaSlicer takes 3D models (STL, OBJ, AMF) and converts them into G-code
instructions for FFF printers or PNG layers for mSLA 3D printers. It's
compatible with any modern printer based on the RepRap toolchain, including all
those based on the Marlin, Prusa, Sprinter and Repetier firmware. It also works
with Mach3, LinuxCNC and Machinekit controllers.

PrusaSlicer is based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

See the [project homepage](https://www.prusa3d.com/slic3r-prusa-edition/) and
the [documentation directory](doc/) for more information.

### What language is it written in?

All user facing code is written in C++.
The slicing core is the `libslic3r` library, which can be built and used in a standalone way.
The command line interface is a thin wrapper over `libslic3r`.

### What are PrusaSlicer's main features?

Key features are:

* **multi-platform** (Linux/Mac/Win) and packaged as standalone-app with no dependencies required
* complete **command-line interface** to use it with no GUI
* multi-material **(multiple extruders)** object printing
* multiple G-code flavors supported (RepRap, Makerbot, Mach3, Machinekit etc.)
* ability to plate **multiple objects having distinct print settings**
* **multithread** processing
* **STL auto-repair** (tolerance for broken models)
* wide automated unit testing

Other major features are:

* combine infill every 'n' perimeters layer to speed up printing
* **3D preview** (including multi-material files)
* **multiple layer heights** in a single print
* **spiral vase** mode for bumpless vases
* fine-grained configuration of speed, acceleration, extrusion width
* several infill patterns including honeycomb, spirals, Hilbert curves
* support material, raft, brim, skirt
* **standby temperature** and automatic wiping for multi-extruder printing
* [customizable **G-code macros**](https://github.com/prusa3d/PrusaSlicer/wiki/PrusaSlicer-Macro-Language) and output filename with variable placeholders
* support for **post-processing scripts**
* **cooling logic** controlling fan speed and dynamic print speed

### Development

If you want to compile the source yourself, follow the instructions on one of
these documentation pages:
* [Linux](doc/How%20to%20build%20-%20Linux%20et%20al.md)
* [macOS](doc/How%20to%20build%20-%20Mac%20OS.md)
* [Windows](doc/How%20to%20build%20-%20Windows.md)

### Can I help?

Sure! You can do the following to find things that are available to help with:
* Add an [issue](https://github.com/prusa3d/PrusaSlicer/issues) to the github tracker if it isn't already present.
* Look at [issues labeled "volunteer needed"](https://github.com/prusa3d/PrusaSlicer/issues?utf8=%E2%9C%93&q=is%3Aopen+is%3Aissue+label%3A%22volunteer+needed%22)

### What's PrusaSlicer license?

PrusaSlicer is licensed under the _GNU Affero General Public License, version 3_.
The PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

### How can I use PrusaSlicer from the command line?

Please refer to the [Command Line Interface](https://github.com/prusa3d/PrusaSlicer/wiki/Command-Line-Interface) wiki page.
