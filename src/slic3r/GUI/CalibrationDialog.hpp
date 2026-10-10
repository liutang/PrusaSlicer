///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_CalibrationDialog_hpp_
#define slic3r_GUI_CalibrationDialog_hpp_

#include "GUI_Utils.hpp"

#include <vector>

// Styled controls of PrusaSlicer.
class CheckBox;
class ComboBox;
class TextInput;
class wxRadioButton;

namespace Slic3r {
namespace GUI {

// Parameters of the temperature tower calibration. Adapted from OrcaSlicer.
class TempTowerDialog : public DPIDialog
{
public:
    explicit TempTowerDialog(wxWindow *parent);

    // Temperature of the bottom most block, rounded to the temperature step of the tower.
    int start_temperature() const;
    // Temperature of the top most block, rounded to the temperature step of the tower.
    int end_temperature() const;
    // Zero based index of the extruder to calibrate.
    int extruder() const;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void apply_filament_type();
    void select_filament_type_for_extruder();

    ::ComboBox   *m_extruder      { nullptr };
    ::ComboBox   *m_filament_type { nullptr };
    ::TextInput *m_start         { nullptr };
    ::TextInput *m_end           { nullptr };
};

// Parameters of the maximum volumetric speed calibration. Adapted from OrcaSlicer.
class MaxVolumetricSpeedDialog : public DPIDialog
{
public:
    explicit MaxVolumetricSpeedDialog(wxWindow *parent);

    // Volumetric speeds in mm3/s.
    double start() const;
    double end() const;
    double step() const;
    // Zero based index of the extruder to calibrate.
    int    extruder() const;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    ::ComboBox         *m_extruder { nullptr };
    ::TextInput *m_start    { nullptr };
    ::TextInput *m_end      { nullptr };
    ::TextInput *m_step     { nullptr };
};

// Parameters of the pressure advance calibrations. Adapted from OrcaSlicer.
class PressureAdvanceDialog : public DPIDialog
{
public:
    // How the pressure advance is set by the firmware.
    enum class Command { M572, M900, Klipper, RepRapFirmware };
    // Tower: the pressure advance grows with the height of a tower.
    // Line: a line per pressure advance, printed slow - fast - slow.
    // Pattern: a chevron per pressure advance, printed over a couple of layers.
    enum class Method { Tower, Line, Pattern };

    explicit PressureAdvanceDialog(wxWindow *parent);

    Method  method() const;
    double  start() const;
    double  end() const;
    double  step() const;
    // Print speed of the line and of the pattern test in mm/s.
    double  speed() const;
    Command command() const;
    bool    print_numbers() const;
    // Zero based index of the extruder to calibrate.
    int     extruder() const;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    // Set the defaults of the selected extruder type and method.
    void reset_params();

    std::vector<wxRadioButton*> m_extruder_types;
    std::vector<wxRadioButton*> m_methods;
    ::ComboBox  *m_extruder { nullptr };
    ::ComboBox  *m_command  { nullptr };
    ::TextInput *m_start    { nullptr };
    ::TextInput *m_end      { nullptr };
    ::TextInput *m_step     { nullptr };
    ::TextInput *m_speed    { nullptr };
    ::CheckBox  *m_numbers  { nullptr };
};

// Parameters of the flow ratio calibration. Adapted from OrcaSlicer.
class FlowRatioDialog : public DPIDialog
{
public:
    explicit FlowRatioDialog(wxWindow *parent);

    // Pass1 and Pass2 change the flow by percents, the "YOLO" tests of OrcaSlicer change the flow ratio by absolute steps.
    enum class Test { Pass1, Pass2, Yolo, YoloPerfectionist };

    Test test() const;
    // Top surface pattern of the tiles: true for Archimedean chords, false for monotonic.
    bool archimedean_chords() const;
    // Zero based index of the extruder to calibrate.
    int extruder() const;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    std::vector<wxRadioButton*> m_tests;
    ::ComboBox   *m_extruder { nullptr };
    ::ComboBox   *m_pattern  { nullptr };
};

// Parameters of the retraction calibration. Adapted from OrcaSlicer.
class RetractionDialog : public DPIDialog
{
public:
    explicit RetractionDialog(wxWindow *parent);

    // Retraction lengths in mm.
    double start() const;
    double end() const;
    double step() const;
    // Zero based index of the extruder to calibrate.
    int    extruder() const;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    ::ComboBox  *m_extruder { nullptr };
    ::TextInput *m_start    { nullptr };
    ::TextInput *m_end      { nullptr };
    ::TextInput *m_step     { nullptr };
};

// Ask for the parameters, start a new project and fill it with a temperature tower,
// the nozzle temperature of which changes with each of its blocks.
void calibrate_temperature_tower();

// Ask for the parameters, start a new project and fill it with a single wall test object,
// the print speed of which grows with each millimeter of height, so that the volumetric speed
// grows by a fixed step.
void calibrate_max_volumetric_speed();

// Ask for the parameters, start a new project and fill it with one of the pressure advance tests:
// a tower, a set of lines or a pattern of chevrons.
void calibrate_pressure_advance();

// Ask for the parameters, start a new project and fill it with a set of test tiles,
// each tile printed with a different flow ratio.
void calibrate_flow_ratio();

// Ask for the parameters, start a new project and fill it with a tower of two columns,
// the retraction length of which grows with each millimeter of height.
void calibrate_retraction();

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_CalibrationDialog_hpp_
