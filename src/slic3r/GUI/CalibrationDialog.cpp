///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <wx/radiobut.h>
#include <wx/statbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/FileReader.hpp"
#include <LocalesUtils.hpp>
#include "libslic3r/Flow.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/Utils.hpp"

#include "Field.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r {
namespace GUI {

// The temperature tower model is a stack of blocks, one block per temperature step, the bottom most block
// being marked with the highest temperature. The model was taken from OrcaSlicer.
static constexpr int    temp_tower_max_temperature = 500;
static constexpr int    temp_tower_min_temperature = 155;
static constexpr int    temp_tower_step            = 5;
static constexpr double temp_tower_block_height    = 10.;

namespace {

struct FilamentTypeRange
{
    const char *name;
    int         start;
    int         end;
};

// The last item stands for custom temperatures.
const std::vector<FilamentTypeRange> filament_type_ranges {
    { "PLA",     230, 190 },
    { "PETG",    250, 230 },
    { "ABS/ASA", 270, 230 },
    { "PCTG",    280, 240 },
    { "FLEX",    240, 210 },
    { "PA-CF",   320, 280 },
    { "PET-CF",  320, 280 },
    { nullptr,   230, 190 }
};

int round_to_step(int temperature)
{
    return int(std::lround(double(temperature) / temp_tower_step)) * temp_tower_step;
}

size_t num_extruders()
{
    return wxGetApp().preset_bundle->extruders_filaments.size();
}

// The dialogs are built from the styled controls of PrusaSlicer, so that they look like the rest of the application.

// A text field for a number, which is shown without trailing zeros. The unit is shown inside of the field.
::TextInput* create_number_field(wxWindow *parent, double value, int width, const wxString &unit = wxEmptyString)
{
    auto *field = new ::TextInput(parent, double_to_string(value), unit, "", wxDefaultPosition, wxSize(width, -1));
    wxGetApp().UpdateDarkUI(field);
    return field;
}

// Set the number without emitting an event.
void set_number(::TextInput *field, double value)
{
    field->GetTextCtrl()->ChangeValue(double_to_string(value));
}

// Returns NaN if the field does not hold a number. Both the decimal point and the decimal comma are accepted.
double get_number(const ::TextInput *field)
{
    wxString text = field->GetTextCtrl()->GetValue();
    text.Trim().Trim(false);
    text.Replace(",", ".");
    double value = 0.;
    return ! text.empty() && text.ToCDouble(&value) ? value : std::numeric_limits<double>::quiet_NaN();
}

// A drop down list.
::ComboBox* create_combo(wxWindow *parent)
{
    auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(20 * wxGetApp().em_unit(), -1), 0, nullptr,
        wxCB_READONLY | DD_NO_CHECK_ICON);
    wxGetApp().UpdateDarkUI(combo);
    return combo;
}

::ComboBox* create_extruder_choice(wxWindow *parent)
{
    auto *choice = create_combo(parent);
    for (size_t i = 0; i < num_extruders(); ++ i)
        choice->Append(format_wxstr("%1% %2%", _L("Extruder"), i + 1));
    choice->SetSelection(0);
    return choice;
}

// A box with a bold title, as used by the sidebar.
wxStaticBoxSizer* create_group(wxWindow *parent, const wxString &title)
{
    auto *box = new wxStaticBox(parent, wxID_ANY, title);
    box->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(box);
    return new wxStaticBoxSizer(box, wxVERTICAL);
}

// A separator line and the OK / Cancel buttons at the bottom of a dialog.
void add_buttons(wxDialog *dialog, wxSizer *top_sizer, int em)
{
    top_sizer->Add(new StaticLine(dialog), 0, wxEXPAND | wxLEFT | wxRIGHT, em);
    top_sizer->Add(dialog->CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, em);
}

// Load a calibration model from the resources, center it in XY and put it on the Z = 0 plane.
// Throws if the model can not be loaded.
TriangleMesh load_calibration_mesh(const std::string &relative_path)
{
    Model model = FileReader::load_model(resources_dir() + "/calib/" + relative_path);
    if (model.objects.empty())
        throw Slic3r::RuntimeError("The model is empty.");
    TriangleMesh mesh = model.objects.front()->raw_mesh();
    if (mesh.empty())
        throw Slic3r::RuntimeError("The model is empty.");
    const BoundingBoxf3 bbox = mesh.bounding_box();
    mesh.translate(Vec3f(- float(bbox.center().x()), - float(bbox.center().y()), - float(bbox.min.z())));
    return mesh;
}

// Start a new project and add the mesh as its only object, assigned to the extruder (zero based).
// The object is either centered on the bed, or it is left where the mesh is.
// Returns nullptr if the user canceled creating of the new project.
ModelObject* start_calibration_project(const TriangleMesh &mesh, const wxString &name, int extruder, bool center = true)
{
    Plater *plater = wxGetApp().plater();
    plater->new_project();
    if (! plater->model().objects.empty())
        // Creating of the new project was canceled.
        return nullptr;
    wxGetApp().obj_list()->load_mesh_object(mesh, into_u8(name), center);
    if (plater->model().objects.empty())
        return nullptr;
    ModelObject *object = plater->model().objects.back();
    object->config.set_key_value("extruder", new ConfigOptionInt(extruder + 1));
    return object;
}

// Change the options of the filament loaded into the extruder (zero based).
void modify_filament_preset(int extruder, const std::function<void(DynamicPrintConfig&)> &modify)
{
    if (TabFilament *tab = dynamic_cast<TabFilament*>(wxGetApp().get_tab(Preset::TYPE_FILAMENT)); tab != nullptr && tab->set_active_extruder(extruder)) {
        DynamicPrintConfig new_config = *tab->get_config();
        modify(new_config);
        tab->load_config(new_config);
    }
}

// To be called after the calibration object and the presets were set up.
void finish_calibration_project()
{
    Plater *plater = wxGetApp().plater();
    plater->on_config_change(wxGetApp().preset_bundle->full_config());
    // Refresh the object list including the object settings and the layer ranges.
    wxGetApp().obj_list()->update_after_undo_redo();
    plater->update_project_dirty_from_presets();
}

} // namespace

TempTowerDialog::TempTowerDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Temperature tower"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    const int em = em_unit();

    auto *grid = new wxFlexGridSizer(2, em, 2 * em);
    grid->AddGrowableCol(1, 1);
    auto add_row = [this, grid](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_extruder = create_extruder_choice(this);
    add_row(_L("Extruder") + ":", m_extruder);
    if (num_extruders() < 2) {
        // Hide the row of a single extruder printer.
        grid->Hide(size_t(0));
        grid->Hide(size_t(1));
    }

    m_filament_type = create_combo(this);
    for (const FilamentTypeRange &range : filament_type_ranges)
        m_filament_type->Append(range.name ? from_u8(range.name) : _L("Custom"));
    add_row(_L("Filament type") + ":", m_filament_type);

    const wxString unit = wxString::FromUTF8("°C");
    m_start = create_number_field(this, 230., 10 * em, unit);
    m_end   = create_number_field(this, 190., 10 * em, unit);
    add_row(_L("Start temperature (bottom)") + ":", m_start);
    add_row(_L("End temperature (top)") + ":", m_end);

    auto *note = new wxStaticText(this, wxID_ANY,
        format_wxstr(_L("The temperature decreases by %1% °C with each %2% mm high block of the tower.\n"
                        "A new project will be created and the nozzle temperatures of the selected filament will be changed."),
                     temp_tower_step, int(temp_tower_block_height)));
    note->SetFont(wxGetApp().small_font());

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(grid, 0, wxEXPAND | wxALL, 2 * em);
    top_sizer->Add(note, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2 * em);
    add_buttons(this, top_sizer, em);

    m_filament_type->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { this->apply_filament_type(); });
    m_extruder->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { this->select_filament_type_for_extruder(); });
    // Typing a temperature switches to the custom range.
    auto set_custom = [this](wxCommandEvent &evt) { m_filament_type->SetSelection(int(filament_type_ranges.size()) - 1); evt.Skip(); };
    m_start->GetTextCtrl()->Bind(wxEVT_TEXT, set_custom);
    m_end->GetTextCtrl()->Bind(wxEVT_TEXT, set_custom);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
        // The temperatures are zero for a field not holding a number.
        const int start = this->start_temperature();
        const int end   = this->end_temperature();
        if (start > temp_tower_max_temperature || end < temp_tower_min_temperature) {
            show_error(this, format_wxstr(_L("Enter temperatures from %1% °C to %2% °C."), temp_tower_min_temperature, temp_tower_max_temperature));
            return;
        }
        if (start < end + temp_tower_step) {
            show_error(this, format_wxstr(_L("The start temperature has to be at least %1% °C higher than the end temperature."), temp_tower_step));
            return;
        }
        evt.Skip();
    }, wxID_OK);

    select_filament_type_for_extruder();

    SetSizerAndFit(top_sizer);
    wxGetApp().UpdateDlgDarkUI(this, true);
    CenterOnParent();
}

int TempTowerDialog::start_temperature() const
{
    const double value = get_number(m_start);
    return std::isnan(value) ? 0 : round_to_step(int(std::lround(value)));
}

int TempTowerDialog::end_temperature() const
{
    const double value = get_number(m_end);
    return std::isnan(value) ? 0 : round_to_step(int(std::lround(value)));
}

int TempTowerDialog::extruder() const
{
    return std::max(0, m_extruder->GetSelection());
}

void TempTowerDialog::apply_filament_type()
{
    const int selection = m_filament_type->GetSelection();
    if (selection >= 0 && selection + 1 < int(filament_type_ranges.size())) {
        // Set without emitting the events, which switch the filament type to custom.
        set_number(m_start, filament_type_ranges[selection].start);
        set_number(m_end, filament_type_ranges[selection].end);
    }
}

// Preselect the temperature range by the type of the filament loaded into the selected extruder.
void TempTowerDialog::select_filament_type_for_extruder()
{
    std::string type;
    if (const Preset *preset = wxGetApp().preset_bundle->extruders_filaments[this->extruder()].get_selected_preset(); preset != nullptr)
        type = preset->config.opt_string("filament_type", 0u);
    const char *name =
        type == "PETG" || type == "PET" ? "PETG" :
        type == "ABS" || type == "ASA"  ? "ABS/ASA" :
        type == "PCTG"                  ? "PCTG" :
        type == "FLEX" || type == "TPU" ? "FLEX" :
                                          "PLA";
    for (size_t i = 0; i + 1 < filament_type_ranges.size(); ++ i)
        if (std::string(filament_type_ranges[i].name) == name) {
            m_filament_type->SetSelection(int(i));
            break;
        }
    apply_filament_type();
}

void TempTowerDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

void calibrate_temperature_tower()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    int start_temp, end_temp, extruder;
    {
        // The dialog has to be destroyed before the new project is started, as starting of the new project may open other modal dialogs.
        TempTowerDialog dialog(wxGetApp().mainframe);
        if (dialog.ShowModal() != wxID_OK)
            return;
        start_temp = dialog.start_temperature();
        end_temp   = dialog.end_temperature();
        extruder   = dialog.extruder();
    }

    const int    num_blocks   = (start_temp - end_temp) / temp_tower_step + 1;
    const double tower_height = num_blocks * temp_tower_block_height;
    if (const double max_print_height = wxGetApp().preset_bundle->printers.get_edited_preset().config.opt_float("max_print_height");
        tower_height > max_print_height) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("The temperature tower would be %1% mm high, which is more than the maximum print height "
                                                         "of the printer (%2% mm). Use a smaller temperature range."), tower_height, max_print_height));
        return;
    }

    // Load the tower and cut the blocks of the requested temperatures out of it before the current project is discarded.
    TriangleMesh tower;
    try {
        TriangleMesh mesh = load_calibration_mesh("temperature_tower/temperature_tower.3mf");
        // The bottom most block of the model is marked with the highest temperature.
        const double z_low  = double(temp_tower_max_temperature - start_temp) / temp_tower_step * temp_tower_block_height;
        const double z_high = z_low + tower_height;
        indexed_triangle_set its = mesh.its;
        if (z_high < mesh.bounding_box().size().z() - EPSILON) {
            indexed_triangle_set lower;
            cut_mesh(its, float(z_high - EPSILON), nullptr, &lower);
            its = std::move(lower);
        }
        if (z_low > EPSILON) {
            indexed_triangle_set upper;
            cut_mesh(its, float(z_low + EPSILON), &upper, nullptr);
            its = std::move(upper);
        }
        tower = TriangleMesh(std::move(its));
        tower.translate(0.f, 0.f, - float(z_low));
        if (tower.empty())
            throw Slic3r::RuntimeError("The model is empty.");
    } catch (const std::exception &ex) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("Loading of the temperature tower model failed: %1%"), ex.what()));
        return;
    }

    ModelObject *object = start_calibration_project(tower, _L("Temperature tower"), extruder);
    if (object == nullptr)
        return;
    object->config.set_key_value("brim_width", new ConfigOptionFloat(5.));

    // Print the tower with the start temperature from the first layer on.
    modify_filament_preset(extruder, [start_temp](DynamicPrintConfig &config) {
        config.set_key_value("temperature", new ConfigOptionInts{ start_temp });
        config.set_key_value("first_layer_temperature", new ConfigOptionInts{ start_temp });
    });

    // Change the temperature with the first layer of each of the following blocks.
    CustomGCode::Info &custom_gcodes = plater->model().custom_gcode_per_print_z();
    custom_gcodes.gcodes.clear();
    custom_gcodes.mode = num_extruders() == 1 ? CustomGCode::SingleExtruder : CustomGCode::MultiAsSingle;
    for (int block = 1; block < num_blocks; ++ block) {
        const int temperature = start_temp - block * temp_tower_step;
        custom_gcodes.gcodes.push_back({ block * temp_tower_block_height + 0.05, CustomGCode::Custom, extruder + 1, "",
            "M104 S" + std::to_string(temperature) + " ; temperature tower" });
    }

    finish_calibration_project();
}

MaxVolumetricSpeedDialog::MaxVolumetricSpeedDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Max volumetric speed test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    const int em = em_unit();

    auto *grid = new wxFlexGridSizer(2, em, 2 * em);
    grid->AddGrowableCol(1, 1);
    auto add_row = [this, grid](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_extruder = create_extruder_choice(this);
    add_row(_L("Extruder") + ":", m_extruder);
    if (num_extruders() < 2) {
        // Hide the row of a single extruder printer.
        grid->Hide(size_t(0));
        grid->Hide(size_t(1));
    }

    const wxString unit = wxString::FromUTF8("mm³/s");
    m_start = create_number_field(this, 5., 10 * em, unit);
    m_end   = create_number_field(this, 20., 10 * em, unit);
    m_step  = create_number_field(this, 0.5, 10 * em, unit);
    add_row(_L("Start volumetric speed") + ":", m_start);
    add_row(_L("End volumetric speed") + ":", m_end);
    add_row(_L("Step") + ":", m_step);

    auto *note = new wxStaticText(this, wxID_ANY,
        _L("The volumetric speed grows by the step with each millimeter of height.\n"
           "A new project will be created and the print and filament settings will be changed."));
    note->SetFont(wxGetApp().small_font());

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(grid, 0, wxEXPAND | wxALL, 2 * em);
    top_sizer->Add(note, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2 * em);
    add_buttons(this, top_sizer, em);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
        // The comparisons are false for a field not holding a number.
        if (! (this->start() > 0. && this->step() > 0. && this->end() >= this->start() + this->step())) {
            show_error(this, _L("Enter a start volumetric speed and a step higher than zero and an end volumetric speed "
                                "higher than the start volumetric speed by at least one step."));
            return;
        }
        evt.Skip();
    }, wxID_OK);

    SetSizerAndFit(top_sizer);
    wxGetApp().UpdateDlgDarkUI(this, true);
    CenterOnParent();
}

double MaxVolumetricSpeedDialog::start() const { return get_number(m_start); }
double MaxVolumetricSpeedDialog::end() const { return get_number(m_end); }
double MaxVolumetricSpeedDialog::step() const { return get_number(m_step); }
int    MaxVolumetricSpeedDialog::extruder() const { return std::max(0, m_extruder->GetSelection()); }

void MaxVolumetricSpeedDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

void calibrate_max_volumetric_speed()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    double start, end, step;
    int    extruder;
    {
        // The dialog has to be destroyed before the new project is started, as starting of the new project may open other modal dialogs.
        MaxVolumetricSpeedDialog dialog(wxGetApp().mainframe);
        if (dialog.ShowModal() != wxID_OK)
            return;
        start    = dialog.start();
        end      = dialog.end();
        step     = dialog.step();
        extruder = dialog.extruder();
    }
    // One millimeter of height per volumetric speed.
    const int    num_steps = int(std::lround((end - start) / step)) + 1;
    const double height    = double(num_steps);

    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    if (const double max_print_height = printer_config.opt_float("max_print_height"); height > max_print_height) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("The test object would be %1% mm high, which is more than the maximum print height "
                                                         "of the printer (%2% mm). Use a smaller range or a bigger step."), height, max_print_height));
        return;
    }

    // A wide and thick extrusion, so that the volumetric speed is reached at a moderate print speed.
    // The layer height is chosen so that each millimeter of height holds a whole number of layers.
    const double nozzle_diameter  = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(extruder);
    const double max_layer_height = printer_config.option<ConfigOptionFloats>("max_layer_height")->get_at(extruder);
    const double layer_height     = 1. / std::ceil(1. / std::min(0.8 * nozzle_diameter, max_layer_height > 0. ? max_layer_height : 0.75 * nozzle_diameter) - EPSILON);
    const double line_width       = 1.75 * nozzle_diameter;
    double       extrusion_multiplier = 1.;
    if (const Preset *preset = wxGetApp().preset_bundle->extruders_filaments[extruder].get_selected_preset(); preset != nullptr)
        extrusion_multiplier = preset->config.opt_float("extrusion_multiplier", 0u);
    const double mm3_per_mm = Flow(float(line_width), float(layer_height), float(nozzle_diameter)).mm3_per_mm() * extrusion_multiplier;

    TriangleMesh mesh;
    try {
        mesh = load_calibration_mesh("volumetric_speed/SpeedTestStructure.3mf");
        // Fit the object to a narrow bed.
        const BoundingBoxf3 bbox = mesh.bounding_box();
        const double bed_width = plater->build_volume().bounding_volume2d().size().x();
        if (const double scale = (bed_width - 10.) / bbox.size().x(); scale > 0. && scale < 1.)
            mesh.scale(Vec3f(float(scale), 1.f, 1.f));
        if (height < bbox.size().z() - EPSILON) {
            indexed_triangle_set lower;
            cut_mesh(mesh.its, float(height), nullptr, &lower);
            mesh = TriangleMesh(std::move(lower));
        }
        if (mesh.empty())
            throw Slic3r::RuntimeError("The model is empty.");
    } catch (const std::exception &ex) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("Loading of the test model failed: %1%"), ex.what()));
        return;
    }

    ModelObject *object = start_calibration_project(mesh, _L("Max volumetric speed test"), extruder);
    if (object == nullptr)
        return;

    // A single wall without top, bottom and infill.
    object->config.set_key_value("perimeters", new ConfigOptionInt(1));
    object->config.set_key_value("top_solid_layers", new ConfigOptionInt(0));
    object->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(0));
    object->config.set_key_value("fill_density", new ConfigOptionPercent(0));
    object->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    object->config.set_key_value("external_perimeter_extrusion_width", new ConfigOptionFloatOrPercent(line_width, false));
    object->config.set_key_value("perimeter_extrusion_width", new ConfigOptionFloatOrPercent(line_width, false));
    object->config.set_key_value("enable_dynamic_overhang_speeds", new ConfigOptionBool(false));
    object->config.set_key_value("brim_width", new ConfigOptionFloat(5.));

    // The print speed is set by a layer range per millimeter of height.
    // Spiral vase mode can not be used, as it does not allow layer ranges.
    object->layer_config_ranges.clear();
    for (int i = 0; i < num_steps; ++ i) {
        ModelConfig range_config;
        // The object list expects each layer range to carry the extruder (zero for the default one) and the layer height.
        range_config.set_key_value("extruder", new ConfigOptionInt(0));
        range_config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        range_config.set_key_value("external_perimeter_speed", new ConfigOptionFloatOrPercent((start + i * step) / mm3_per_mm, false));
        object->layer_config_ranges[{ double(i), double(i + 1) }].assign_config(std::move(range_config));
    }

    // Remove everything that would limit or change the print speed.
    modify_filament_preset(extruder, [](DynamicPrintConfig &config) {
        const double max_volumetric_speed = config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(0);
        config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{ std::max(max_volumetric_speed, 200.) });
        config.set_key_value("slowdown_below_layer_time", new ConfigOptionInts{ 0 });
    });
    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT); tab != nullptr) {
        DynamicPrintConfig new_config = *tab->get_config();
        new_config.set_key_value("spiral_vase", new ConfigOptionBool(false));
        new_config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(layer_height, false));
        new_config.set_key_value("max_volumetric_speed", new ConfigOptionFloat(0.));
        new_config.set_key_value("max_volumetric_extrusion_rate_slope_positive", new ConfigOptionFloat(0.));
        new_config.set_key_value("max_volumetric_extrusion_rate_slope_negative", new ConfigOptionFloat(0.));
        tab->load_config(new_config);
    }

    finish_calibration_project();
}

PressureAdvanceDialog::PressureAdvanceDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("PA Calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    const int em = em_unit();

    // The layout and the defaults follow the pressure advance calibration dialog of OrcaSlicer.
    auto add_radios = [em](wxStaticBoxSizer *group, std::vector<wxRadioButton*> &radios, std::initializer_list<wxString> labels) {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        for (const wxString &label : labels) {
            auto *radio = new wxRadioButton(group->GetStaticBox(), wxID_ANY, label, wxDefaultPosition, wxDefaultSize, radios.empty() ? wxRB_GROUP : 0);
            row->Add(radio, 0, wxRIGHT, 2 * em);
            radios.emplace_back(radio);
        }
        group->Add(row, 0, wxALL, em);
    };
    auto *extruder_types = create_group(this, _L("Extruder type"));
    add_radios(extruder_types, m_extruder_types, { _L("DDE"), _L("Bowden") });
    m_extruder_types.front()->SetValue(true);
    auto *methods = create_group(this, _L("Method"));
    add_radios(methods, m_methods, { _L("PA Tower"), _L("PA Line"), _L("PA Pattern") });
    m_methods[size_t(Method::Line)]->SetValue(true);

    auto *settings = create_group(this, _L("Settings"));
    auto *grid = new wxFlexGridSizer(2, em, 2 * em);
    grid->AddGrowableCol(1, 1);
    auto add_row = [settings, grid](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(settings->GetStaticBox(), wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_extruder = create_extruder_choice(settings->GetStaticBox());
    add_row(_L("Extruder") + ":", m_extruder);
    if (num_extruders() < 2) {
        // Hide the row of a single extruder printer.
        grid->Hide(size_t(0));
        grid->Hide(size_t(1));
    }

    m_start = create_number_field(settings->GetStaticBox(), 0., 10 * em);
    m_end   = create_number_field(settings->GetStaticBox(), 0.1, 10 * em);
    m_step  = create_number_field(settings->GetStaticBox(), 0.002, 10 * em);
    m_speed = create_number_field(settings->GetStaticBox(), 100., 10 * em, "mm/s");
    add_row(_L("Start PA") + ":", m_start);
    add_row(_L("End PA") + ":", m_end);
    add_row(_L("PA step") + ":", m_step);
    add_row(_L("Print speed") + ":", m_speed);

    // Preselect the command by the printer.
    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const GCodeFlavor         flavor         = printer_config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    const std::string         notes          = printer_config.opt_string("printer_notes");
    bool prusa_input_shaper_firmware = false;
    for (const char *model : { "COREONE", "MK4IS", "MK4S", "MK3.9S", "MK3.5", "XLIS", "MINIIS" })
        prusa_input_shaper_firmware |= notes.find(model) != std::string::npos;
    m_command = create_combo(settings->GetStaticBox());
    m_command->Append("M572 S (Prusa)");
    m_command->Append("M900 K (Marlin)");
    m_command->Append("SET_PRESSURE_ADVANCE (Klipper)");
    m_command->Append("M572 D S (RepRapFirmware)");
    m_command->SetSelection(
        flavor == gcfKlipper        ? int(Command::Klipper) :
        flavor == gcfRepRapFirmware ? int(Command::RepRapFirmware) :
        prusa_input_shaper_firmware ? int(Command::M572) : int(Command::M900));
    add_row(_L("Command") + ":", m_command);

    m_numbers = new ::CheckBox(settings->GetStaticBox(), _L("Print numbers"));
    settings->Add(grid, 0, wxEXPAND | wxALL, em);
    settings->Add(m_numbers, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(extruder_types, 0, wxEXPAND | wxALL, 2 * em);
    top_sizer->Add(methods, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2 * em);
    top_sizer->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2 * em);
    add_buttons(this, top_sizer, em);

    for (wxRadioButton *radio : m_extruder_types)
        radio->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { this->reset_params(); });
    for (wxRadioButton *radio : m_methods)
        radio->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { this->reset_params(); });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
        // The comparisons are false for a field not holding a number.
        if (! (this->start() >= 0. && this->step() > 0. && this->end() >= this->start() + this->step())) {
            show_error(this, _L("Enter a PA step higher than zero and an end PA higher than the start PA by at least one step."));
            return;
        }
        if (this->method() != Method::Tower && ! (this->speed() >= 10.)) {
            show_error(this, _L("Enter a print speed of at least 10 mm/s."));
            return;
        }
        evt.Skip();
    }, wxID_OK);

    reset_params();

    SetSizerAndFit(top_sizer);
    wxGetApp().UpdateDlgDarkUI(this, true);
    CenterOnParent();
}

void PressureAdvanceDialog::reset_params()
{
    // The defaults are those of OrcaSlicer.
    const bool   dde    = m_extruder_types.front()->GetValue();
    const Method method = this->method();
    set_number(m_start, 0.);
    set_number(m_end,  ! dde ? 1. : method == Method::Pattern ? 0.08 : 0.1);
    set_number(m_step, method == Method::Pattern ? (dde ? 0.005 : 0.05) : (dde ? 0.002 : 0.02));
    // The numbers are optional for the lines only: the tower has none, the pattern always has them.
    m_numbers->SetValue(method != Method::Tower);
    m_numbers->Enable(method == Method::Line);
    // The tower is printed with the speeds of the print settings.
    m_speed->Enable(method != Method::Tower);
}

PressureAdvanceDialog::Method PressureAdvanceDialog::method() const
{
    for (size_t i = 0; i < m_methods.size(); ++ i)
        if (m_methods[i]->GetValue())
            return Method(i);
    return Method::Line;
}

double PressureAdvanceDialog::start() const { return get_number(m_start); }
double PressureAdvanceDialog::end() const { return get_number(m_end); }
double PressureAdvanceDialog::step() const { return get_number(m_step); }
double PressureAdvanceDialog::speed() const { return get_number(m_speed); }
bool   PressureAdvanceDialog::print_numbers() const { return m_numbers->GetValue(); }
int    PressureAdvanceDialog::extruder() const { return std::max(0, m_extruder->GetSelection()); }
PressureAdvanceDialog::Command PressureAdvanceDialog::command() const { return Command(std::max(0, m_command->GetSelection())); }

void PressureAdvanceDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

namespace {

// Writes the G-code of the pressure advance line test and of the pressure advance pattern test.
// The E axis is expected to be relative. The layouts follow the tests of OrcaSlicer.
class PressureAdvanceGCode
{
public:
    // Layout of the test.
    static constexpr double length_short   = 20.;
    static constexpr double space_y        = 3.5;
    static constexpr double digit_segment  = 2.;
    static constexpr double digit_spacing  = 3.;
    static constexpr int    max_digits     = 5;

    // Layout of the pattern test.
    static constexpr int    pattern_layers      = 4;
    static constexpr int    pattern_walls       = 3;
    static constexpr double pattern_side_length = 30.;
    static constexpr double pattern_spacing     = 2.;

    double nozzle_diameter   { 0.4 };
    // Height of the first layer.
    double layer_height      { 0.2 };
    // Height of the other layers and the print speed of the first layer, used by the pattern test.
    double other_layer_height { 0.2 };
    double first_layer_speed { 30. };
    // Z of the first layer including the Z offset of the printer.
    double first_layer_z     { 0.2 };
    double filament_diameter { 1.75 };
    double extrusion_multiplier { 1. };
    double retract_length    { 0.8 };
    double retract_speed     { 35. };
    double deretract_speed   { 35. };
    double travel_speed      { 150. };
    double fast_speed        { 100. };
    double acceleration      { 0. };
    int    extruder          { 0 };
    GCodeFlavor flavor       { gcfMarlinFirmware };
    PressureAdvanceDialog::Command command { PressureAdvanceDialog::Command::M572 };
    bool   print_numbers     { true };

    double line_width() const { return 1.5 * nozzle_diameter; }
    double number_line_width() const { return 1.2 * nozzle_diameter; }
    double length_long(double bed_width) const { return 40. + std::min(bed_width - 120., 0.); }
    double box_width() const { return print_numbers ? digit_spacing * 8. : 0.; }
    // Size of the test including the box with the numbers.
    Vec2d  size(double bed_width, int num_lines) const
        { return Vec2d(2. * length_short + length_long(bed_width) + line_width() + box_width(), (num_lines + 1) * space_y); }
    static int max_lines(double bed_depth) { return std::max(2, int((bed_depth - 20.) / space_y) - 1); }

    // The command setting the pressure advance, without a new line.
    std::string pressure_advance_command(double value) const
    {
        using Command = PressureAdvanceDialog::Command;
        switch (command) {
        case Command::M572:           return format("M572 S%.4f", value);
        case Command::M900:           return format("M900 K%.4f", value);
        case Command::Klipper:        return format("SET_PRESSURE_ADVANCE ADVANCE=%.4f", value);
        case Command::RepRapFirmware: return format("M572 D%d S%.4f", extruder, value);
        }
        return {};
    }

    // Pattern test: a chevron per pressure advance, each chevron made of a couple of nested walls.
    double pattern_line_width() const { return 1.125 * nozzle_diameter; }
    double pattern_line_spacing(double height) const { return pattern_line_width() - height * (1. - M_PI / 4.); }
    // Distance of the walls of a chevron along the X axis.
    double pattern_wall_step() const { return pattern_line_spacing(other_layer_height) * std::sqrt(2.); }
    double pattern_pitch() const { return (pattern_walls - 1) * pattern_wall_step() + pattern_spacing + pattern_line_width(); }
    // Size of a chevron along the X axis and along the Y axis.
    static double pattern_chevron_depth() { return pattern_side_length / std::sqrt(2.); }
    static double pattern_chevron_height() { return pattern_side_length * std::sqrt(2.); }
    double pattern_margin() const { return 2. * pattern_line_width(); }
    // Height of the box with the numbers above the frame.
    static double pattern_tab_height() { return max_digits * digit_spacing + 2.; }
    // Width of the outer walls of the frame around the inner rectangle of the frame.
    double pattern_frame_width() const { return (pattern_walls - 1) * pattern_line_spacing(layer_height) + 0.5 * pattern_line_width(); }
    // Size of the inner rectangle of the frame.
    Vec2d  pattern_inner_size(int num_patterns) const
    {
        return Vec2d(2. * pattern_margin() + (num_patterns - 1) * pattern_pitch() + (pattern_walls - 1) * pattern_wall_step() + pattern_chevron_depth(),
                     pattern_chevron_height());
    }
    // Size of the test including the frame and the box with the numbers.
    Vec2d  pattern_size(int num_patterns) const
    {
        const Vec2d inner = this->pattern_inner_size(num_patterns);
        return Vec2d(inner.x() + 2. * pattern_frame_width(), inner.y() + 2. * pattern_frame_width() + pattern_tab_height());
    }
    int    pattern_max_count(double bed_width) const
    {
        const double fixed = this->pattern_size(1).x();
        return std::max(2, 1 + int((bed_width - 20. - fixed) / this->pattern_pitch()));
    }

    // Returns the G-code of each of the layers of the pattern test.
    // center: center of the test in the coordinates of the printer.
    std::vector<std::string> generate_pattern(const Vec2d &center, double start_pa, double step_pa, int num_patterns)
    {
        const double w      = this->pattern_line_width();
        const Vec2d  size   = this->pattern_size(num_patterns);
        const Vec2d  inner  = this->pattern_inner_size(num_patterns);
        const double frame  = this->pattern_frame_width();
        // The inner rectangle of the frame.
        const double x0     = center.x() - 0.5 * size.x() + frame;
        const double y0     = center.y() - 0.5 * size.y() + frame;
        const double x1     = x0 + inner.x();
        const double y1     = y0 + inner.y();
        const double depth  = pattern_chevron_depth();
        const int    decimals = step_pa < 0.01 - EPSILON ? 3 : 2;

        std::vector<std::string> layers;
        for (int layer = 0; layer < pattern_layers; ++ layer) {
            const double height = layer == 0 ? layer_height : other_layer_height;
            const double z      = first_layer_z + layer * other_layer_height;
            const double speed  = layer == 0 ? first_layer_speed : fast_speed;
            const double e      = this->e_per_mm(w, height);

            m_gcode.clear();
            m_retracted = false;
            m_gcode += "; pressure advance pattern test\n";
            m_gcode += ";TYPE:Custom\n";
            this->tags(w, height);
            if (acceleration > 0.)
                m_gcode += flavor == gcfKlipper ? format("SET_VELOCITY_LIMIT ACCEL=%d\n", int(acceleration)) :
                           flavor == gcfMarlinLegacy ? format("M204 S%d\n", int(acceleration)) : format("M204 P%d\n", int(acceleration));
            m_gcode += format("G1 Z%.3f F%.0f\n", z, travel_speed * 60.);
            this->set_pressure_advance(start_pa);

            if (layer == 0) {
                // The frame, from its inner most wall outwards.
                const double spacing = this->pattern_line_spacing(height);
                for (int i = 0; i < pattern_walls; ++ i) {
                    const double o = i * spacing;
                    this->travel(x0 - o, y0 - o);
                    this->extrude(x0 - o, y1 + o, e, speed);
                    this->extrude(x1 + o, y1 + o, e, speed);
                    this->extrude(x1 + o, y0 - o, e, speed);
                    this->extrude(x0 - o, y0 - o, e, speed);
                }
                // A filled box above the frame as a base of the numbers.
                const double o       = (pattern_walls - 1) * spacing;
                const double tab_y0  = y1 + o + spacing;
                const double tab_y1  = tab_y0 + pattern_tab_height() - spacing;
                bool to_right = true;
                this->travel(x0 - o, tab_y0);
                for (double y = tab_y0; y <= tab_y1 + EPSILON; y += spacing) {
                    if (y > tab_y0)
                        this->extrude(to_right ? x0 - o : x1 + o, y, e, speed);
                    this->extrude(to_right ? x1 + o : x0 - o, y, e, speed);
                    to_right = ! to_right;
                }
            } else if (layer == 1) {
                // The numbers read from the bottom to the top, each second chevron is marked.
                const double tab_y0 = y1 + (pattern_walls - 1) * this->pattern_line_spacing(layer_height) + 1.5;
                for (int i = 0; i < num_patterns; i += 2)
                    this->draw_number(x0 + this->pattern_margin() + i * this->pattern_pitch() + 0.5 * (pattern_walls - 1) * this->pattern_wall_step() + digit_segment,
                                      tab_y0, start_pa + i * step_pa, decimals, true);
            }

            // The first layer chevrons end short of the frame, the other chevrons start and end over the frame.
            const double inset = layer == 0 ? w : 0.;
            for (int i = 0; i < num_patterns; ++ i) {
                this->set_pressure_advance(start_pa + i * step_pa);
                for (int wall = 0; wall < pattern_walls; ++ wall) {
                    const double x = x0 + this->pattern_margin() + i * this->pattern_pitch() + wall * this->pattern_wall_step();
                    this->travel(x + inset, y0 + inset);
                    this->extrude(x + depth, 0.5 * (y0 + y1), e, speed);
                    this->extrude(x + inset, y1 - inset, e, speed);
                }
            }
            this->set_pressure_advance(start_pa);

            // Leave the filament in the state the slicer expects it at the start of a layer.
            this->retract();
            this->unretract();
            m_gcode += "; end of the pressure advance pattern test\n";
            layers.emplace_back(std::move(m_gcode));
        }
        return layers;
    }

    // center: center of the test in the coordinates of the printer.
    std::string generate(const Vec2d &center, double bed_width, double start_pa, double step_pa, int num_lines)
    {
        const Vec2d  size        = this->size(bed_width, num_lines);
        const double start_x     = center.x() - 0.5 * size.x();
        // Y of the first line.
        const double start_y     = center.y() - 0.5 * size.y() + space_y;
        const double long_len    = this->length_long(bed_width);
        const double slow_speed  = std::max(10., fast_speed / 10.);
        const double e_line      = this->e_per_mm(this->line_width());
        const double x1          = start_x + length_short;
        const double x2          = x1 + long_len;
        const double x3          = x2 + length_short;
        const double top_y       = start_y + num_lines * space_y;

        m_gcode.clear();
        m_retracted = false;
        m_gcode += "; pressure advance line test\n";
        m_gcode += ";TYPE:Custom\n";
        this->tags(this->line_width(), layer_height);
        if (acceleration > 0.)
            m_gcode += flavor == gcfKlipper ? format("SET_VELOCITY_LIMIT ACCEL=%d\n", int(acceleration)) :
                       flavor == gcfMarlinLegacy ? format("M204 S%d\n", int(acceleration)) : format("M204 P%d\n", int(acceleration));
        m_gcode += format("G1 Z%.3f F%.0f\n", first_layer_z, travel_speed * 60.);

        // Anchor line along the left side of the lines, it also primes the nozzle.
        this->set_pressure_advance(0.);
        this->travel(start_x, top_y);
        this->extrude(start_x, start_y, e_line * 1.2, slow_speed);

        for (int i = 0; i < num_lines; ++ i) {
            const double y = start_y + i * space_y;
            this->set_pressure_advance(start_pa + i * step_pa);
            this->travel(start_x, y);
            this->extrude(x1, y, e_line, slow_speed);
            this->extrude(x2, y, e_line, fast_speed);
            this->extrude(x3, y, e_line, slow_speed);
            if (i == 0) {
                // Anchor line along the right side of the lines.
                this->set_pressure_advance(0.);
                this->extrude(x3, top_y, e_line * 1.2, slow_speed);
            }
        }
        this->set_pressure_advance(0.);

        if (print_numbers) {
            // A filled box as a base of the numbers.
            const double box_min_x = x3 + this->line_width();
            const double box_max_x = box_min_x + this->box_width();
            const double box_min_y = start_y - space_y;
            const double box_max_y = top_y;
            const double spacing   = this->line_width() - layer_height * (1. - M_PI / 4.);
            const double box_speed = std::min(fast_speed, 50.);
            this->travel(box_min_x, box_min_y);
            bool to_right = true;
            for (double y = box_min_y; y <= box_max_y + EPSILON; y += spacing) {
                if (y > box_min_y)
                    this->extrude(to_right ? box_min_x : box_max_x, y, e_line, box_speed);
                this->extrude(to_right ? box_max_x : box_min_x, y, e_line, box_speed);
                to_right = ! to_right;
            }

            // The numbers are printed over the box, each second line is marked.
            this->retract();
            m_gcode += format("G1 Z%.3f F%.0f\n", first_layer_z + layer_height, travel_speed * 60.);
            this->tags(this->number_line_width(), layer_height);
            const int decimals = step_pa < 0.01 - EPSILON ? 3 : 2;
            for (int i = 0; i < num_lines; i += 2)
                this->draw_number(box_min_x + digit_spacing, start_y + i * space_y - digit_segment, start_pa + i * step_pa, decimals);
            this->retract();
            m_gcode += format("G1 Z%.3f F%.0f\n", first_layer_z, travel_speed * 60.);
        }

        // Leave the filament in the state the slicer expects it at the start of a layer.
        this->unretract();
        m_gcode += "; end of the pressure advance line test\n";
        return std::move(m_gcode);
    }

private:
    static std::string format(const char *fmt, ...)
    {
        char buffer[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        return buffer;
    }

    double e_per_mm(double width) const { return this->e_per_mm(width, layer_height); }
    double e_per_mm(double width, double height) const
    {
        const double filament_area = M_PI * 0.25 * filament_diameter * filament_diameter;
        return Flow(float(width), float(height), float(nozzle_diameter)).mm3_per_mm() * extrusion_multiplier / filament_area;
    }

    void tags(double width, double height)
    {
        m_gcode += format(";WIDTH:%.3f\n", width);
        m_gcode += format(";HEIGHT:%.3f\n", height);
    }

    void set_pressure_advance(double value)
    {
        m_gcode += this->pressure_advance_command(value) + "\n";
    }

    void retract()
    {
        if (! m_retracted && retract_length > 0.)
            m_gcode += format("G1 E-%.5f F%.0f\n", retract_length, retract_speed * 60.);
        m_retracted = true;
    }

    void unretract()
    {
        if (m_retracted && retract_length > 0.)
            m_gcode += format("G1 E%.5f F%.0f\n", retract_length, deretract_speed * 60.);
        m_retracted = false;
    }

    void travel(double x, double y, bool with_retract = true)
    {
        if (with_retract)
            this->retract();
        m_gcode += format("G1 X%.3f Y%.3f F%.0f\n", x, y, travel_speed * 60.);
        if (with_retract)
            this->unretract();
        m_pos = Vec2d(x, y);
    }

    void extrude(double x, double y, double e_per_mm, double speed)
    {
        const double length = (Vec2d(x, y) - m_pos).norm();
        if (length < EPSILON)
            return;
        m_gcode += format("G1 X%.3f Y%.3f E%.5f F%.0f\n", x, y, length * e_per_mm, speed * 60.);
        m_pos = Vec2d(x, y);
    }

    // Seven segment digits. (x, y) is the bottom left corner of the number, the digits are twice as high as wide.
    // A vertical number reads from the bottom to the top, (x, y) is then its bottom right corner.
    void draw_number(double x, double y, double value, int decimals, bool vertical = false)
    {
        // Position of a point of a digit, given along the text and across it.
        auto at = [x, y, vertical](double along, double across) { return vertical ? Vec2d(x - across, y + along) : Vec2d(x + along, y + across); };
        double offset = 0.;
        char text[32];
        snprintf(text, sizeof(text), "%.*f", decimals, value);
        const double e      = this->e_per_mm(this->number_line_width());
        const double speed  = std::min(fast_speed, 60.);
        const double s      = digit_segment;
        // Segments: top, upper right, lower right, bottom, lower left, upper left, middle.
        const double segments[7][4] = {
            { 0, 2 * s, s, 2 * s }, { s, 2 * s, s, s }, { s, s, s, 0 }, { 0, 0, s, 0 }, { 0, s, 0, 0 }, { 0, 2 * s, 0, s }, { 0, s, s, s } };
        const unsigned char digits[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
        bool first = true;
        for (int i = 0; text[i] != 0 && i < max_digits; ++ i, offset += digit_spacing) {
            if (text[i] == '.') {
                // A short dash at the bottom.
                const Vec2d from = at(offset + 0.3 * s, 0.);
                const Vec2d to   = at(offset + 0.7 * s, 0.);
                this->travel(from.x(), from.y(), first);
                this->extrude(to.x(), to.y(), e, speed);
                first = false;
            } else if (text[i] >= '0' && text[i] <= '9') {
                const unsigned char mask = digits[text[i] - '0'];
                for (int segment = 0; segment < 7; ++ segment)
                    if (mask & (1 << segment)) {
                        const double *pts = segments[segment];
                        const Vec2d from = at(offset + pts[0], pts[1]);
                        const Vec2d to   = at(offset + pts[2], pts[3]);
                        // Retract just before the first segment of a number, the other travels are short.
                        if ((from - m_pos).norm() > EPSILON)
                            this->travel(from.x(), from.y(), first);
                        this->extrude(to.x(), to.y(), e, speed);
                        first = false;
                    }
            }
        }
    }

    std::string m_gcode;
    Vec2d       m_pos { Vec2d::Zero() };
    bool        m_retracted { false };
};

// A flat PrusaSlicer logo: two half discs divided by a diagonal gap. The geometry is taken from resources/icons/PrusaSlicer.svg.
// The logo is centered around the origin, it is about 17 mm wide.
indexed_triangle_set make_logo(double height)
{
    // Millimeters per unit of the 800 x 800 units large icon.
    const double scale   = 0.025;
    const double radius  = 240.4 * scale;
    // The gap of the icon is too narrow to be printed, thus the halves are moved apart a bit.
    const double min_gap = 0.9;
    const double gap     = 20.6 * scale;
    const double shift   = 0.5 * std::max(0., min_gap - gap);
    const int    steps   = 48;

    indexed_triangle_set logo;
    // A prism over a half disc. The half disc faces the direction given by the angle.
    auto add_half_disc = [&](double cx, double cy, double angle) {
        cx += shift * std::cos(angle);
        cy += shift * std::sin(angle);
        const int base = int(logo.vertices.size());
        const int cnt  = steps + 1;
        // Counter clockwise contour, the bottom vertices first, the top vertices second.
        for (double z : { 0., height })
            for (int i = 0; i < cnt; ++ i) {
                const double a = angle - 0.5 * M_PI + M_PI * double(i) / steps;
                logo.vertices.emplace_back(float(cx + radius * std::cos(a)), float(cy + radius * std::sin(a)), float(z));
            }
        for (int i = 1; i + 1 < cnt; ++ i) {
            // Bottom facing down, top facing up.
            logo.indices.emplace_back(base, base + i + 1, base + i);
            logo.indices.emplace_back(base + cnt, base + cnt + i, base + cnt + i + 1);
        }
        for (int i = 0; i < cnt; ++ i) {
            const int j = (i + 1) % cnt;
            logo.indices.emplace_back(base + i, base + j, base + cnt + j);
            logo.indices.emplace_back(base + i, base + cnt + j, base + cnt + i);
        }
    };
    // The dark half faces the top left, the orange half faces the bottom right.
    add_half_disc( 29.3 * scale,  43.2 * scale, 0.75 * M_PI);
    add_half_disc(-27.3 * scale, -42.5 * scale, -0.25 * M_PI);
    return logo;
}

} // namespace

namespace {

// The line and the pattern tests are drawn by custom G-code only, while the slicer needs an object to print.
// Thus start the project with a small PrusaSlicer logo next to the top left corner of the test, which is printed after the test.
// Returns false if the user canceled creating of the new project.
bool start_drawn_test_project(const wxString &name, int extruder, const BoundingBoxf &bed, const Vec2d &test_size, double logo_height, double layer_height)
{
    const double logo_gap = 4.;
    const Vec2d  test_min = bed.center() - 0.5 * test_size;
    TriangleMesh logo(make_logo(logo_height));
    {
        const BoundingBoxf3 bbox = logo.bounding_box();
        // Left of the test if it fits the bed, right of it otherwise.
        const bool   fits_left = test_min.x() - logo_gap - bbox.size().x() >= bed.min.x() + 2.;
        const double min_x     = fits_left ? test_min.x() - logo_gap - bbox.size().x() : test_min.x() + test_size.x() + logo_gap;
        // Aligned with the top of the test.
        logo.translate(float(min_x - bbox.min.x()), float(test_min.y() + test_size.y() - bbox.max.y()), 0.f);
    }

    ModelObject *object = start_calibration_project(logo, name, extruder, false);
    if (object == nullptr)
        return false;
    object->config.set_key_value("brim_width", new ConfigOptionFloat(0.));
    object->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    // A skirt would be printed around the logo only and it could cross the test.
    if (Tab *tab_print = wxGetApp().get_tab(Preset::TYPE_PRINT); tab_print != nullptr && tab_print->get_config()->opt_int("skirts") > 0) {
        DynamicPrintConfig new_config = *tab_print->get_config();
        new_config.set_key_value("skirts", new ConfigOptionInt(0));
        tab_print->load_config(new_config);
    }
    return true;
}

void calibrate_pressure_advance_tower(const PressureAdvanceGCode &generator, double start, double end, double step)
{
    Plater   *plater   = wxGetApp().plater();
    const int extruder = generator.extruder;

    // The pressure advance grows by the step with each millimeter of height.
    const int num_steps = int(std::lround(std::ceil((end - start) / step - EPSILON))) + 1;
    TriangleMesh tower;
    try {
        tower = load_calibration_mesh("pressure_advance/tower_with_seam.3mf");
        const double model_height = tower.bounding_box().size().z();
        if (double(num_steps) > model_height + EPSILON) {
            show_error(wxGetApp().mainframe, format_wxstr(_L("The tower would be %1% mm high, while the tower model is %2% mm high. "
                                                             "Use a smaller range or a bigger step."), num_steps, model_height));
            return;
        }
        if (double(num_steps) < model_height - EPSILON) {
            indexed_triangle_set lower;
            cut_mesh(tower.its, float(num_steps), nullptr, &lower);
            tower = TriangleMesh(std::move(lower));
        }
        if (tower.empty())
            throw Slic3r::RuntimeError("The model is empty.");
    } catch (const std::exception &ex) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("Loading of the pressure advance tower model failed: %1%"), ex.what()));
        return;
    }

    ModelObject *object = start_calibration_project(tower, _L("Pressure advance tower"), extruder);
    if (object == nullptr)
        return;
    // Two perimeters only, with the seam at the rear, so that the corners of the tower show the pressure advance.
    object->config.set_key_value("perimeters", new ConfigOptionInt(2));
    object->config.set_key_value("top_solid_layers", new ConfigOptionInt(0));
    object->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(0));
    object->config.set_key_value("fill_density", new ConfigOptionPercent(0));
    object->config.set_key_value("seam_position", new ConfigOptionEnum<SeamPosition>(spRear));
    object->config.set_key_value("brim_width", new ConfigOptionFloat(5.));

    // Do not let the cooling slow the print down.
    modify_filament_preset(extruder, [](DynamicPrintConfig &config) {
        config.set_key_value("slowdown_below_layer_time", new ConfigOptionInts{ 1 });
    });

    CustomGCode::Info &custom_gcodes = plater->model().custom_gcode_per_print_z();
    custom_gcodes.gcodes.clear();
    custom_gcodes.mode = num_extruders() == 1 ? CustomGCode::SingleExtruder : CustomGCode::MultiAsSingle;
    for (int i = 0; i < num_steps; ++ i)
        custom_gcodes.gcodes.push_back({ i + 0.05, CustomGCode::Custom, extruder + 1, "",
            generator.pressure_advance_command(start + i * step) + " ; pressure advance tower" });

    finish_calibration_project();
}

void calibrate_pressure_advance_line(PressureAdvanceGCode &generator, double start, double end, double step)
{
    Plater   *plater   = wxGetApp().plater();
    const int extruder = generator.extruder;

    // Fit the number of lines to the bed.
    const BoundingBoxf bed = plater->build_volume().bounding_volume2d();
    const int requested_lines = int(std::lround(std::ceil((end - start) / step - EPSILON))) + 1;
    const int num_lines       = std::min(requested_lines, PressureAdvanceGCode::max_lines(bed.size().y()));
    const Vec2d test_size     = generator.size(bed.size().x(), num_lines);

    if (! start_drawn_test_project(_L("Pressure advance line test"), extruder, bed, test_size, generator.layer_height, generator.other_layer_height))
        return;

    // The test is centered on the bed.
    const std::string gcode = generator.generate(bed.center(), bed.size().x(), start, step, num_lines);
    CustomGCode::Info &custom_gcodes = plater->model().custom_gcode_per_print_z();
    custom_gcodes.gcodes.clear();
    custom_gcodes.mode = num_extruders() == 1 ? CustomGCode::SingleExtruder : CustomGCode::MultiAsSingle;
    // The test is printed at the start of the first layer.
    custom_gcodes.gcodes.push_back({ 0.05, CustomGCode::Custom, extruder + 1, "", gcode });

    finish_calibration_project();

    if (num_lines < requested_lines)
        show_info(wxGetApp().mainframe, format_wxstr(_L("Only %1% of the %2% lines fit the bed. The test ends at the pressure advance of %3%."),
                                                     num_lines, requested_lines, start + (num_lines - 1) * step), _L("Pressure advance line test"));
}

void calibrate_pressure_advance_pattern(PressureAdvanceGCode &generator, double start, double end, double step)
{
    Plater   *plater   = wxGetApp().plater();
    const int extruder = generator.extruder;

    // Fit the number of chevrons to the bed.
    const BoundingBoxf bed = plater->build_volume().bounding_volume2d();
    const int requested_patterns = int(std::lround(std::ceil((end - start) / step - EPSILON))) + 1;
    const int num_patterns       = std::min(requested_patterns, generator.pattern_max_count(bed.size().x()));
    const Vec2d test_size        = generator.pattern_size(num_patterns);

    // The logo is as high as the pattern, so that the slicer generates all the layers of the pattern.
    const double test_height = generator.layer_height + (PressureAdvanceGCode::pattern_layers - 1) * generator.other_layer_height;
    if (! start_drawn_test_project(_L("Pressure advance pattern test"), extruder, bed, test_size, test_height, generator.other_layer_height))
        return;

    // The test is centered on the bed. Each of its layers is printed at the start of the layer of the logo.
    const std::vector<std::string> layers = generator.generate_pattern(bed.center(), start, step, num_patterns);
    CustomGCode::Info &custom_gcodes = plater->model().custom_gcode_per_print_z();
    custom_gcodes.gcodes.clear();
    custom_gcodes.mode = num_extruders() == 1 ? CustomGCode::SingleExtruder : CustomGCode::MultiAsSingle;
    for (size_t i = 0; i < layers.size(); ++ i)
        custom_gcodes.gcodes.push_back({ generator.layer_height + i * generator.other_layer_height - 0.05, CustomGCode::Custom, extruder + 1, "", layers[i] });

    finish_calibration_project();

    if (num_patterns < requested_patterns)
        show_info(wxGetApp().mainframe, format_wxstr(_L("Only %1% of the %2% chevrons fit the bed. The test ends at the pressure advance of %3%."),
                                                     num_patterns, requested_patterns, start + (num_patterns - 1) * step), _L("Pressure advance pattern test"));
}

} // namespace

void calibrate_pressure_advance()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    using Method = PressureAdvanceDialog::Method;
    PressureAdvanceGCode generator;
    Method method;
    double start, end, step;
    {
        // The dialog has to be destroyed before the new project is started, as starting of the new project may open other modal dialogs.
        PressureAdvanceDialog dialog(wxGetApp().mainframe);
        if (dialog.ShowModal() != wxID_OK)
            return;
        method                  = dialog.method();
        start                   = dialog.start();
        end                     = dialog.end();
        step                    = dialog.step();
        generator.command       = dialog.command();
        generator.print_numbers = dialog.print_numbers();
        generator.extruder      = dialog.extruder();
        if (method != Method::Tower)
            generator.fast_speed = dialog.speed();
    }
    const int extruder = generator.extruder;

    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const DynamicPrintConfig &print_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (method != Method::Tower && ! printer_config.opt_bool("use_relative_e_distances")) {
        show_error(wxGetApp().mainframe, _L("The pressure advance line and pattern tests require relative extruder distances. "
                                            "Enable \"Use relative E distances\" in Printer Settings."));
        return;
    }

    generator.flavor             = printer_config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    generator.nozzle_diameter    = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(extruder);
    generator.retract_length     = printer_config.option<ConfigOptionFloats>("retract_length")->get_at(extruder);
    generator.retract_speed      = printer_config.option<ConfigOptionFloats>("retract_speed")->get_at(extruder);
    generator.deretract_speed    = printer_config.option<ConfigOptionFloats>("deretract_speed")->get_at(extruder);
    if (generator.deretract_speed <= 0.)
        generator.deretract_speed = generator.retract_speed;
    generator.other_layer_height = print_config.opt_float("layer_height");
    generator.layer_height       = print_config.option<ConfigOptionFloatOrPercent>("first_layer_height")->get_abs_value(generator.other_layer_height);
    generator.first_layer_z      = generator.layer_height + printer_config.opt_float("z_offset");
    generator.travel_speed       = print_config.opt_float("travel_speed");
    generator.first_layer_speed  = print_config.option<ConfigOptionFloatOrPercent>("first_layer_speed")->get_abs_value(generator.fast_speed);
    if (generator.first_layer_speed <= 0.)
        generator.first_layer_speed = 30.;
    generator.acceleration       = print_config.opt_float("external_perimeter_acceleration");
    if (generator.acceleration <= 0.)
        generator.acceleration = print_config.opt_float("default_acceleration");
    if (const Preset *preset = wxGetApp().preset_bundle->extruders_filaments[extruder].get_selected_preset(); preset != nullptr) {
        generator.filament_diameter    = preset->config.opt_float("filament_diameter", 0u);
        generator.extrusion_multiplier = preset->config.opt_float("extrusion_multiplier", 0u);
    }

    switch (method) {
    case Method::Tower:   calibrate_pressure_advance_tower(generator, start, end, step); break;
    case Method::Line:    calibrate_pressure_advance_line(generator, start, end, step); break;
    case Method::Pattern: calibrate_pressure_advance_pattern(generator, start, end, step); break;
    }
}

FlowRatioDialog::FlowRatioDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Flow Ratio Calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    const int em = em_unit();

    // The layout and the defaults follow the flow ratio calibration dialog of OrcaSlicer.
    auto *tests = create_group(this, _L("Calibration Test Type"));
    for (const wxString &label : { _L("Pass 1 (Coarse)"), _L("Pass 2 (Fine)"), _L("YOLO (Recommended)"), _L("YOLO (Perfectionist)") }) {
        auto *radio = new wxRadioButton(tests->GetStaticBox(), wxID_ANY, label, wxDefaultPosition, wxDefaultSize, m_tests.empty() ? wxRB_GROUP : 0);
        tests->Add(radio, 0, wxLEFT | wxRIGHT | wxTOP, em);
        m_tests.emplace_back(radio);
    }
    tests->AddSpacer(em);
    m_tests[size_t(Test::Yolo)]->SetValue(true);

    auto *settings = create_group(this, _L("Settings"));
    auto *grid = new wxFlexGridSizer(2, em, 2 * em);
    grid->AddGrowableCol(1, 1);
    auto add_row = [settings, grid](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(settings->GetStaticBox(), wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_extruder = create_extruder_choice(settings->GetStaticBox());
    add_row(_L("Extruder") + ":", m_extruder);
    if (num_extruders() < 2) {
        // Hide the row of a single extruder printer.
        grid->Hide(size_t(0));
        grid->Hide(size_t(1));
    }

    m_pattern = create_combo(settings->GetStaticBox());
    m_pattern->Append(_L("Archimedean Chords"));
    m_pattern->Append(_L("Monotonic"));
    m_pattern->SetSelection(0);
    add_row(_L("Top Surface Pattern") + ":", m_pattern);
    settings->Add(grid, 0, wxEXPAND | wxALL, em);

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(tests, 0, wxEXPAND | wxALL, 2 * em);
    top_sizer->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2 * em);
    add_buttons(this, top_sizer, em);

    SetSizerAndFit(top_sizer);
    wxGetApp().UpdateDlgDarkUI(this, true);
    CenterOnParent();
}

FlowRatioDialog::Test FlowRatioDialog::test() const
{
    for (size_t i = 0; i < m_tests.size(); ++ i)
        if (m_tests[i]->GetValue())
            return Test(i);
    return Test::Yolo;
}
bool FlowRatioDialog::archimedean_chords() const { return m_pattern->GetSelection() != 1; }
int  FlowRatioDialog::extruder() const { return std::max(0, m_extruder->GetSelection()); }

void FlowRatioDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

void calibrate_flow_ratio()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    using Test = FlowRatioDialog::Test;
    Test test;
    bool archimedean_chords;
    int  extruder;
    {
        // The dialog has to be destroyed before the new project is started, as starting of the new project may open other modal dialogs.
        FlowRatioDialog dialog(wxGetApp().mainframe);
        if (dialog.ShowModal() != wxID_OK)
            return;
        test               = dialog.test();
        archimedean_chords = dialog.archimedean_chords();
        extruder           = dialog.extruder();
    }

    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const DynamicPrintConfig &print_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const double nozzle_diameter    = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(extruder);
    // The tiles were designed for a 0.4 mm nozzle and 0.2 mm layers. They are ten layers high: two bottom, three sparse and five top layers.
    const double layer_height       = 0.5 * nozzle_diameter;
    const double first_layer_height = std::max(layer_height, print_config.option<ConfigOptionFloatOrPercent>("first_layer_height")->get_abs_value(layer_height));
    const double tile_height        = first_layer_height + 9. * layer_height;
    const double xy_scale           = nozzle_diameter / 0.6 > 1.2 ? nozzle_diameter / 0.6 : 1.;

    // The "YOLO" tests change the flow ratio by absolute steps, the other tests change it by percents.
    const bool   absolute_steps     = test == Test::Yolo || test == Test::YoloPerfectionist;
    double       extrusion_multiplier = 1.;
    if (const Preset *preset = wxGetApp().preset_bundle->extruders_filaments[extruder].get_selected_preset(); preset != nullptr)
        extrusion_multiplier = preset->config.opt_float("extrusion_multiplier", 0u);

    // The tiles of the test, each named by its change of flow, as "flowrate_5", "flowrate_m10" or "flowrate_m0.02".
    struct Tile {
        double       change;
        TriangleMesh mesh;
    };
    std::vector<Tile> tiles;
    try {
        Model model = FileReader::load_model(resources_dir() + "/calib/filament_flow/" +
            (test == Test::Pass1 ? "flowrate-test-pass1.3mf" : test == Test::Pass2 ? "flowrate-test-pass2.3mf" :
             test == Test::Yolo  ? "Orca-LinearFlow.3mf"     : "Orca-LinearFlow_fine.3mf"));
        for (const ModelObject *model_object : model.objects) {
            const std::string prefix = "flowrate_";
            if (model_object->name.compare(0, prefix.size(), prefix) != 0)
                continue;
            std::string number = model_object->name.substr(prefix.size());
            const bool  negative = ! number.empty() && number.front() == 'm';
            if (negative)
                number.erase(number.begin());
            Tile tile;
            tile.change = (negative ? -1. : 1.) * string_to_double_decimal_point(number);
            tile.mesh   = model_object->raw_mesh();
            if (tile.mesh.empty())
                continue;
            // Keep the tiles where they were designed: they are laid out along a serpentine,
            // so that the tiles next to each other in flow are next to each other on the bed.
            if (! model_object->instances.empty())
                tile.mesh.transform(model_object->instances.front()->get_matrix());
            tiles.emplace_back(std::move(tile));
        }
        if (tiles.empty())
            throw Slic3r::RuntimeError("The model is empty.");
    } catch (const std::exception &ex) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("Loading of the test model failed: %1%"), ex.what()));
        return;
    }
    std::sort(tiles.begin(), tiles.end(), [](const Tile &l, const Tile &r) { return l.change < r.change; });

    // Center the layout on the bed and scale the tiles to the nozzle.
    {
        BoundingBoxf3 bbox;
        for (const Tile &tile : tiles)
            bbox.merge(tile.mesh.bounding_box());
        const BoundingBoxf bed = plater->build_volume().bounding_volume2d();
        for (Tile &tile : tiles) {
            tile.mesh.translate(Vec3f(- float(bbox.center().x()), - float(bbox.center().y()), - float(bbox.min.z())));
            tile.mesh.scale(Vec3f(float(xy_scale), float(xy_scale), float(tile_height / bbox.size().z())));
            tile.mesh.translate(float(bed.center().x()), float(bed.center().y()), 0.f);
        }
    }

    auto tile_name = [absolute_steps, test](const Tile &tile) {
        const char *sign = tile.change > 0 ? "+" : "";
        return absolute_steps ?
            format_wxstr("%1% %2%%3%", _L("Flow"), sign, float_to_string_decimal_point(tile.change, test == Test::YoloPerfectionist ? 3 : 2)) :
            format_wxstr("%1% %2%%3% %%", _L("Flow"), sign, int(std::lround(tile.change)));
    };
    if (start_calibration_project(tiles.front().mesh, tile_name(tiles.front()), extruder, false) == nullptr)
        return;
    for (size_t i = 1; i < tiles.size(); ++ i)
        wxGetApp().obj_list()->load_mesh_object(tiles[i].mesh, into_u8(tile_name(tiles[i])), false);

    Model &model = plater->model();
    for (size_t i = 0; i < tiles.size() && i < model.objects.size(); ++ i) {
        ModelConfigObject &config = model.objects[i]->config;
        config.set_key_value("extruder", new ConfigOptionInt(extruder + 1));
        config.set_key_value("print_flow_ratio", new ConfigOptionFloat(absolute_steps ?
            (extrusion_multiplier + tiles[i].change) / extrusion_multiplier : 1. + 0.01 * tiles[i].change));
        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        // A single perimeter and a wide monotonic top infill, so that the top surface shows the flow.
        config.set_key_value("perimeters", new ConfigOptionInt(1));
        config.set_key_value("top_one_perimeter_type", new ConfigOptionEnum<TopOnePerimeterType>(TopOnePerimeterType::TopSurfaces));
        config.set_key_value("thin_walls", new ConfigOptionBool(true));
        config.set_key_value("gap_fill_enabled", new ConfigOptionBool(false));
        config.set_key_value("thick_bridges", new ConfigOptionBool(false));
        config.set_key_value("ironing", new ConfigOptionBool(false));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(2));
        config.set_key_value("top_solid_layers", new ConfigOptionInt(5));
        config.set_key_value("bottom_solid_min_thickness", new ConfigOptionFloat(0.));
        config.set_key_value("top_solid_min_thickness", new ConfigOptionFloat(0.));
        config.set_key_value("fill_density", new ConfigOptionPercent(35));
        config.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        config.set_key_value("fill_angle", new ConfigOptionFloat(45.));
        config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(archimedean_chords ? ipArchimedeanChords : ipMonotonic));
        // The arcs at the corners first, the center spiral last and from its center outwards, as OrcaSlicer does it.
        config.set_key_value("calib_flowrate_topinfill_special_order", new ConfigOptionBool(archimedean_chords));
        config.set_key_value("top_infill_extrusion_width", new ConfigOptionFloatOrPercent(1.2 * nozzle_diameter, false));
        config.set_key_value("solid_infill_extrusion_width", new ConfigOptionFloatOrPercent(1.2 * nozzle_diameter, false));
        config.set_key_value("brim_width", new ConfigOptionFloat(0.));
    }

    finish_calibration_project();
}

RetractionDialog::RetractionDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Retraction test"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    const int em = em_unit();

    auto *settings = create_group(this, _L("Settings"));
    auto *grid = new wxFlexGridSizer(2, em, 2 * em);
    grid->AddGrowableCol(1, 1);
    auto add_row = [settings, grid](const wxString &label, wxWindow *ctrl) {
        grid->Add(new wxStaticText(settings->GetStaticBox(), wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_extruder = create_extruder_choice(settings->GetStaticBox());
    add_row(_L("Extruder") + ":", m_extruder);
    if (num_extruders() < 2) {
        // Hide the row of a single extruder printer.
        grid->Hide(size_t(0));
        grid->Hide(size_t(1));
    }

    // The defaults are those of OrcaSlicer.
    m_start = create_number_field(settings->GetStaticBox(), 0., 10 * em, "mm");
    m_end   = create_number_field(settings->GetStaticBox(), 2., 10 * em, "mm");
    m_step  = create_number_field(settings->GetStaticBox(), 0.1, 10 * em, "mm");
    add_row(_L("Start retraction length") + ":", m_start);
    add_row(_L("End retraction length") + ":", m_end);
    add_row(_L("Step") + ":", m_step);
    settings->Add(grid, 0, wxEXPAND | wxALL, em);

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(settings, 0, wxEXPAND | wxALL, 2 * em);
    add_buttons(this, top_sizer, em);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
        // The comparisons are false for a field not holding a number.
        if (! (this->start() >= 0. && this->step() > 0. && this->end() >= this->start() + this->step())) {
            show_error(this, _L("Enter a step higher than zero and an end retraction length higher than the start retraction length by at least one step."));
            return;
        }
        evt.Skip();
    }, wxID_OK);

    SetSizerAndFit(top_sizer);
    wxGetApp().UpdateDlgDarkUI(this, true);
    CenterOnParent();
}

double RetractionDialog::start() const { return get_number(m_start); }
double RetractionDialog::end() const { return get_number(m_end); }
double RetractionDialog::step() const { return get_number(m_step); }
int    RetractionDialog::extruder() const { return std::max(0, m_extruder->GetSelection()); }

void RetractionDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

void calibrate_retraction()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    double start, end, step;
    int    extruder;
    {
        // The dialog has to be destroyed before the new project is started, as starting of the new project may open other modal dialogs.
        RetractionDialog dialog(wxGetApp().mainframe);
        if (dialog.ShowModal() != wxID_OK)
            return;
        start    = dialog.start();
        end      = dialog.end();
        step     = dialog.step();
        extruder = dialog.extruder();
    }

    // The tower has a base, above which the retraction length grows with each millimeter of height.
    const double base_height = 0.4;
    const int    num_steps   = int(std::lround((end - start) / step)) + 1;
    const double height      = base_height + num_steps;

    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    if (const double max_print_height = printer_config.opt_float("max_print_height"); height > max_print_height) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("The retraction tower would be %1% mm high, which is more than the maximum print height "
                                                         "of the printer (%2% mm). Use a smaller range or a bigger step."), height, max_print_height));
        return;
    }
    if (printer_config.opt_bool("use_firmware_retraction")) {
        show_error(wxGetApp().mainframe, _L("The retraction test can not be used with the firmware retraction. "
                                            "Disable \"Use firmware retraction\" in Printer Settings."));
        return;
    }

    TriangleMesh tower;
    try {
        tower = load_calibration_mesh("retraction/retraction_tower.3mf");
        if (height < tower.bounding_box().size().z() - EPSILON) {
            indexed_triangle_set lower;
            cut_mesh(tower.its, float(height - EPSILON), nullptr, &lower);
            tower = TriangleMesh(std::move(lower));
        }
        if (tower.empty())
            throw Slic3r::RuntimeError("The model is empty.");
    } catch (const std::exception &ex) {
        show_error(wxGetApp().mainframe, format_wxstr(_L("Loading of the retraction tower model failed: %1%"), ex.what()));
        return;
    }

    ModelObject *object = start_calibration_project(tower, _L("Retraction tower"), extruder);
    if (object == nullptr)
        return;

    // Two perimeters without infill, so that the nozzle travels between the two columns of the tower at each layer.
    const double nozzle_diameter = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(extruder);
    const double layer_height    = nozzle_diameter <= 0.1 ? 0.05 : nozzle_diameter <= 0.2 ? 0.1 : 0.2;
    object->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    object->config.set_key_value("perimeters", new ConfigOptionInt(2));
    object->config.set_key_value("top_solid_layers", new ConfigOptionInt(0));
    object->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(3));
    object->config.set_key_value("fill_density", new ConfigOptionPercent(0));
    object->config.set_key_value("seam_position", new ConfigOptionEnum<SeamPosition>(spAligned));

    // Change the retraction length with the first layer of each millimeter of height above the base.
    CustomGCode::Info &custom_gcodes = plater->model().custom_gcode_per_print_z();
    custom_gcodes.gcodes.clear();
    custom_gcodes.mode = num_extruders() == 1 ? CustomGCode::SingleExtruder : CustomGCode::MultiAsSingle;
    for (int i = 0; i < num_steps; ++ i)
        custom_gcodes.gcodes.push_back({ base_height + i + 0.05, CustomGCode::Custom, extruder + 1, "",
            // This comment is interpreted by the G-code generator.
            ";CALIBRATION_RETRACT_LENGTH:" + float_to_string_decimal_point(start + i * step, 3) + " ; retraction tower" });

    finish_calibration_project();
}

} // namespace GUI
} // namespace Slic3r
