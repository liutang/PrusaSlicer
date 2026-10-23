#include "GUI_ObjectSettingsTabs.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

#include <wx/dcbuffer.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "ConfigManipulation.hpp"
#include "Field.hpp"
#include "format.hpp"
#include "GUI_App.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "Sidebar.hpp"
#include "Tab.hpp"

namespace Slic3r {
namespace GUI {

// A key without the index of a vector option ("opt_key#1").
static std::string option_key(const std::string &opt_id)
{
    return opt_id.substr(0, opt_id.find('#'));
}

// The same filter as the "Add settings" menu of the object list.
static bool is_improper_category(const std::string &category, int extruders_cnt, bool is_object)
{
    return category.empty() ||
        (extruders_cnt == 1 && (category == "Extruders" || category == "Wipe options")) ||
        (!is_object && category == "Support material");
}

// The icon of a page of the Print Settings.
static std::string page_icon(const wxString &title)
{
    static const std::vector<std::pair<const char*, const char*>> icons {
        { L("Layers and perimeters"), "layers" },
        { L("Infill"),                "infill" },
        { L("Skirt and brim"),        "skirt+brim" },
        { L("Support material"),      "support" },
        { L("Speed"),                 "time" },
        { L("Multiple Extruders"),    "funnel" },
        { L("Advanced"),              "wrench" },
    };
    for (const auto &[page, icon] : icons)
        if (title == _(page))
            return icon;
    return "cog";
}

// An options group whose "undo" icon removes the setting from the selected items, so that they use the
// value they inherit again.
class OverrideOptionsGroup : public ConfigOptionsGroup
{
public:
    OverrideOptionsGroup(wxWindow *parent, const wxString &title, DynamicPrintConfig *config, std::function<void(const std::string&)> revert)
        : ConfigOptionsGroup(parent, title, config, true), m_revert(std::move(revert)) {}

    void back_to_initial_value(const std::string &opt_key) override { if (m_revert) m_revert(opt_key); }
    void back_to_sys_value(const std::string &opt_key) override { if (m_revert) m_revert(opt_key); }

private:
    std::function<void(const std::string&)> m_revert;
};

// A row of icon tabs; the tab shown is highlighted and its name drawn next to the icons.
class ObjectSettingsTabs::TabBar : public wxPanel
{
public:
    TabBar(wxWindow *parent, std::function<void(int)> on_select) :
        wxPanel(parent, wxID_ANY), m_on_select(std::move(on_select))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        update_size();
        Bind(wxEVT_PAINT, &TabBar::on_paint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
            if (const int tab = tab_at(evt.GetPosition()); tab >= 0 && tab != m_active)
                m_on_select(tab);
        });
        Bind(wxEVT_MOTION, [this](wxMouseEvent &evt) { set_hover(tab_at(evt.GetPosition())); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { set_hover(-1); });
    }

    void set_tabs(const std::vector<std::pair<std::string, wxString>> &tabs, int active)
    {
        m_icons.clear();
        m_names.clear();
        for (const auto &[icon, name] : tabs) {
            m_icons.emplace_back(this, icon);
            m_names.push_back(name);
        }
        m_active = active;
        m_hover  = -1;
        UnsetToolTip();
        Refresh();
    }

    void sys_color_changed()
    {
        for (ScalableBitmap &icon : m_icons)
            icon.sys_color_changed();
        Refresh();
    }

    void msw_rescale()
    {
        update_size();
        Refresh();
    }

private:
    int    cell() const { return int(2.4 * wxGetApp().em_unit()); }
    wxRect tab_rect(int tab) const { return wxRect(tab * cell(), 0, cell(), cell()); }
    void   update_size() { SetMinSize(wxSize(-1, cell() + 3)); }

    int tab_at(const wxPoint &pos) const
    {
        for (int tab = 0; tab < int(m_icons.size()); ++ tab)
            if (tab_rect(tab).Contains(pos))
                return tab;
        return -1;
    }

    void set_hover(int tab)
    {
        if (tab == m_hover)
            return;
        m_hover = tab;
        if (tab >= 0)
            SetToolTip(m_names[tab]);
        else
            UnsetToolTip();
        Refresh();
    }

    void on_paint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();

        const wxColour accent(237, 107, 33); // Prusa orange
        for (int tab = 0; tab < int(m_icons.size()); ++ tab) {
            const wxRect rect = tab_rect(tab).Deflate(1);
            if (tab == m_active) {
                dc.SetPen(wxPen(accent, 1));
                dc.SetBrush(wxBrush(wxGetApp().get_highlight_default_clr()));
                dc.DrawRoundedRectangle(rect, 3);
            } else if (tab == m_hover) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxGetApp().get_highlight_default_clr()));
                dc.DrawRoundedRectangle(rect, 3);
            }
            const wxBitmap bmp = m_icons[tab].bmp().GetBitmapFor(this);
            dc.DrawBitmap(bmp, rect.x + (rect.width - bmp.GetScaledWidth()) / 2, rect.y + (rect.height - bmp.GetScaledHeight()) / 2, true);
        }

        if (m_active >= 0 && m_active < int(m_names.size())) {
            dc.SetFont(wxGetApp().bold_font());
            dc.SetTextForeground(wxGetApp().get_label_clr_default());
            const wxSize size = dc.GetTextExtent(m_names[m_active]);
            dc.DrawText(m_names[m_active], int(m_icons.size()) * cell() + wxGetApp().em_unit(), (cell() - size.y) / 2);
        }

        dc.SetPen(wxPen(accent, 1));
        dc.DrawLine(0, cell() + 1, GetSize().x, cell() + 1);
    }

    std::function<void(int)>    m_on_select;
    std::vector<ScalableBitmap> m_icons;
    std::vector<wxString>       m_names;
    int                         m_active { 0 };
    int                         m_hover { -1 };
};

ObjectSettingsTabs::ObjectSettingsTabs(wxWindow *parent) :
    m_parent(parent),
    m_bmp_revert(parent, "undo"),
    m_bmp_dot(parent, "dot"),
    m_bmp_empty(parent, "empty")
{
    m_sizer   = new wxBoxSizer(wxVERTICAL);
    m_tab_bar = new TabBar(parent, [this](int tab) { select_tab(tab); });
    m_tab_bar->Hide();
    m_sizer->Add(m_tab_bar, 0, wxEXPAND | wxTOP, 5);

    m_page_window = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_page_window->SetScrollRate(0, 10);
    // Below this height the whole sidebar scrolls instead.
    m_page_window->SetMinSize(wxSize(-1, 8 * wxGetApp().em_unit()));
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(m_page_window);
#elif !defined(__APPLE__)
    m_page_window->SetBackgroundColour(parent->GetBackgroundColour());
#endif
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    m_page_window->SetSizer(m_page_sizer);
    m_page_window->Hide();
    m_sizer->Add(m_page_window, 1, wxEXPAND | wxTOP, 2);
}

ObjectSettingsTabs::~ObjectSettingsTabs()
{
    for (auto &group : m_groups)
        group->clear(false);
}

bool ObjectSettingsTabs::first_tab_active() const
{
    return m_has_first_tab && m_active_title.empty();
}

const ObjectSettingsTabs::PageDef* ObjectSettingsTabs::active_page() const
{
    if (m_active_title.empty())
        return nullptr;
    for (const PageDef &page : m_pages)
        if (page.title == m_active_title)
            return &page;
    return nullptr;
}

void ObjectSettingsTabs::show(Kind kind, std::vector<SettingsTabsItem> items, const std::string &first_tab_icon, const wxString &first_tab_name)
{
    if (items.empty()) {
        hide();
        return;
    }
    m_kind           = kind;
    m_items          = std::move(items);
    m_has_first_tab  = !first_tab_icon.empty();
    m_first_tab_icon = first_tab_icon;
    m_first_tab_name = first_tab_name;

    switch (m_kind) {
    case Kind::Object:      m_tt_not_overridden = _L("Same as in the print settings"); break;
    case Kind::Part:        m_tt_not_overridden = _L("Same as for the object"); break;
    case Kind::HeightRange: m_tt_not_overridden = _L("Same as for the object"); break;
    }

    build_page_defs();
    if (m_pages.empty() && !m_has_first_tab) {
        hide();
        return;
    }
    // Stay on the page shown, if the new selection has it.
    if (active_page() == nullptr)
        m_active_title.clear();
    if (!m_has_first_tab && m_active_title.empty())
        m_active_title = m_pages.front().title;

    compute_config();

    std::vector<std::pair<std::string, wxString>> tabs;
    int active = 0;
    if (m_has_first_tab)
        tabs.emplace_back(m_first_tab_icon, m_first_tab_name);
    for (const PageDef &page : m_pages) {
        if (page.title == m_active_title)
            active = int(tabs.size());
        tabs.emplace_back(page.icon, page.title);
    }
    m_tab_bar->set_tabs(tabs, active);
    m_tab_bar->Show();
    m_shown = true;
    build_page();
}

void ObjectSettingsTabs::hide()
{
    clear_page();
    m_tab_bar->Hide();
    m_page_window->Hide();
    m_items.clear();
    m_shown = false;
    m_built_title.clear();
}

void ObjectSettingsTabs::set_first_tab_sizer(wxSizer *sizer)
{
    m_first_tab_sizer = sizer;
    m_sizer->Insert(1, sizer, 0, wxEXPAND);
    m_first_tab_spacer = m_sizer->AddStretchSpacer(0);
}

void ObjectSettingsTabs::stretch_first_tab(bool stretch)
{
    if (m_first_tab_spacer != nullptr)
        m_first_tab_spacer->SetProportion(stretch ? 1 : 0);
}

int ObjectSettingsTabs::tab_bar_height() const
{
    // The tab bar and its border.
    return m_tab_bar->GetMinSize().y + 5;
}

void ObjectSettingsTabs::select_tab(int tab)
{
    const int page = m_has_first_tab ? tab - 1 : tab;
    m_active_title = page >= 0 && page < int(m_pages.size()) ? m_pages[page].title : wxString();
    // Shows or hides the panel of the first tab and builds the page.
    wxGetApp().obj_list()->part_selection_changed();
}

void ObjectSettingsTabs::build_page_defs()
{
    m_pages.clear();
    m_allowed.clear();
    Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (tab == nullptr)
        return;

    const bool is_object = m_kind == Kind::Object;
    for (const std::string &key : SettingsFactory::get_options(!is_object))
        m_allowed.insert(key);
    const int              extruders_cnt = wxGetApp().extruders_edited_cnt();
    const ConfigOptionMode mode          = wxGetApp().get_mode();

    auto usable = [&](const Option &option) {
        const std::string key = option_key(option.opt_id);
        if (m_allowed.find(key) == m_allowed.end())
            return false;
        const ConfigOptionDef *def = print_config_def.get(key);
        return def != nullptr && !is_improper_category(def->category, extruders_cnt, is_object);
    };

    for (const PageShp &page : tab->get_pages()) {
        PageDef page_def { page->title(), page_icon(page->title()), {} };
        bool    visible = false;
        for (const ConfigOptionsGroupShp &group : page->m_optgroups) {
            GroupDef group_def { group->title, {} };
            for (const Line &line : group->get_lines()) {
                LineDef line_def { line.label, line.label_tooltip, line.label_path, {} };
                for (const Option &option : line.get_options())
                    if (usable(option)) {
                        line_def.options.push_back(option);
                        visible |= option.opt.mode <= mode;
                    }
                if (!line_def.options.empty())
                    group_def.lines.push_back(std::move(line_def));
            }
            if (!group_def.lines.empty())
                page_def.groups.push_back(std::move(group_def));
        }
        if (visible)
            m_pages.push_back(std::move(page_def));
    }
}

void ObjectSettingsTabs::compute_config()
{
    m_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const SettingsTabsItem &first = m_items.front();
    if (first.object_config != nullptr)
        m_config.apply(first.object_config->get(), true);
    m_config.apply(first.config->get(), true);
}

void ObjectSettingsTabs::clear_page()
{
    m_page_sizer->Clear(true);
    for (auto &group : m_groups)
        group->clear(false);
    m_groups.clear();
    m_override_tooltips.clear();
}

void ObjectSettingsTabs::build_page()
{
    clear_page();
    const PageDef *page = active_page();
    m_page_window->Show(page != nullptr);
    if (page == nullptr) {
        m_built_title.clear();
        return;
    }

    const int extruders_cnt = wxGetApp().extruders_edited_cnt() + wxGetApp().virtual_extruders_cnt();
    // The sidebar is narrower than the Print Settings: every setting gets a line of its own, with its
    // unit in the label instead of next to the field, and the fields are as wide as in the settings the
    // object list adds.
    auto sidebar_option = [extruders_cnt](ConfigOptionsGroup &group, const Option &tab_option, const LineDef &line_def) {
        // The group has to make the option itself, so that it loads its value from the configuration and
        // stores the changes there; the definition is the one of the Print Settings.
        const std::string &opt_id = tab_option.opt_id;
        const size_t       hash   = opt_id.find('#');
        Option option = group.get_option(opt_id.substr(0, hash), hash == std::string::npos ? -1 : std::atoi(opt_id.c_str() + hash + 1));
        option.opt = tab_option.opt;
        if (option.opt.category == "Extruders")
            option.opt.max = extruders_cnt;
        option.opt.width = 12;

        // A line of several settings ("Solid layers: Top, Bottom") is split up.
        wxString label = line_def.options.size() == 1 ? line_def.label :
                         !option.opt.full_label.empty() ? _(option.opt.full_label) :
                                                          line_def.label + " " + _(option.opt.label);
        if (!option.opt.sidetext.empty()) {
            // "mm" -> "Label (mm)", "(minimum)" -> "Label (minimum)", "mm (zero to disable)" -> "Label (mm, zero to disable)"
            wxString unit = _(option.opt.sidetext);
            if (!unit.StartsWith("(")) {
                if (const int note = unit.Find(" ("); note != wxNOT_FOUND && unit.EndsWith(")"))
                    unit = unit.Left(note) + ", " + unit.Mid(note + 2, unit.Len() - note - 3);
                unit = "(" + unit + ")";
            }
            label += " " + unit;
            option.opt.sidetext.clear();
        }
        option.opt.label = into_u8(label);
        return option;
    };

    for (const GroupDef &group_def : page->groups) {
        auto group = std::make_shared<OverrideOptionsGroup>(m_page_window, group_def.title, &m_config,
            [this](const std::string &opt_id) { on_revert(opt_id); });
        // As in the Print Settings, for the search of settings.
        group->set_config_category_and_type(page->title, Preset::TYPE_PRINT);
        group->label_width       = 15;
        // The panel only shows the settings of the mode anyway.
        group->show_mode_markers = false;
        group->on_change   = [this](const t_config_option_key &opt_id, const boost::any &) { on_value_change(opt_id); };

        for (const LineDef &line_def : group_def.lines)
            for (const Option &option : line_def.options)
                group->append_single_option_line(sidebar_option(*group, option, line_def), line_def.path);
        group->activate();

        // First take a snapshot for undo, then change the configuration.
        ConfigOptionsGroup *group_ptr = group.get();
        for (const LineDef &line_def : group_def.lines)
            for (const Option &option : line_def.options)
                if (Field *field = group->get_field(option.opt_id))
                    field->m_on_change = [group_ptr](const std::string &opt_id, const boost::any &value) {
                        wxGetApp().plater()->take_snapshot(format_wxstr(_L("Change Option %s"), opt_id));
                        group_ptr->on_change_OG(opt_id, value);
                    };

        group->reload_config();
        group->update_visibility(wxGetApp().get_mode());
        // Room on the right, also for an overlay scroll bar.
        m_page_sizer->Add(group->sizer, 0, wxEXPAND | wxBOTTOM | wxRIGHT, wxGetApp().em_unit());
        m_groups.push_back(group);
    }

    update_override_marks();
    apply_config_manipulation(false);

    m_page_window->FitInside();
    // A page opened anew starts at its top; the same page keeps its place, e.g. after a value changed.
    if (m_built_title != page->title)
        m_page_window->Scroll(0, 0);
    m_built_title = page->title;
}

void ObjectSettingsTabs::update_override_marks()
{
    const PageDef *page = active_page();
    if (page == nullptr)
        return;

    // The values the first item would print with without its own settings.
    DynamicPrintConfig inherited = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (m_items.front().object_config != nullptr)
        inherited.apply(m_items.front().object_config->get(), true);

    m_override_tooltips.clear();
    for (size_t group_idx = 0; group_idx < m_groups.size() && group_idx < page->groups.size(); ++ group_idx) {
        ConfigOptionsGroup *group = m_groups[group_idx].get();
        for (const LineDef &line_def : page->groups[group_idx].lines)
            for (const Option &option : line_def.options) {
                Field *field = group->get_field(option.opt_id);
                if (field == nullptr)
                    continue;
                const std::string key = option_key(option.opt_id);
                const size_t overridden = std::count_if(m_items.begin(), m_items.end(),
                    [&key](const SettingsTabsItem &item) { return item.config->has(key); });
                if (overridden > 0) {
                    wxString tooltip;
                    if (m_items.size() > 1)
                        tooltip = format_wxstr(_L("Changed for %1% of the %2% selected items. Click to remove the change from all of them."),
                                               overridden, m_items.size());
                    else {
                        const wxString value = inherited.has(key) ? from_u8(inherited.opt_serialize(key)) : wxString();
                        switch (m_kind) {
                        case Kind::Object:
                            tooltip = format_wxstr(_L("Changed for this object. Click to use the value of the print settings: %1%"), value); break;
                        case Kind::Part:
                            tooltip = format_wxstr(_L("Changed for this part. Click to use the value of the object: %1%"), value); break;
                        case Kind::HeightRange:
                            tooltip = format_wxstr(_L("Changed for this height range. Click to use the value of the object: %1%"), value); break;
                        }
                    }
                    m_override_tooltips[option.opt_id] = tooltip;
                    field->set_undo_bitmap(&m_bmp_revert);
                    field->set_undo_tooltip(&m_override_tooltips[option.opt_id]);
                    // The field only reacts to a click on its undo icon while it is marked as modified.
                    field->m_is_modified_value = true;
                    field->set_label_colour(&wxGetApp().get_label_clr_modified());
                } else {
                    field->set_undo_bitmap(&m_bmp_dot);
                    field->set_undo_tooltip(&m_tt_not_overridden);
                    field->m_is_modified_value = false;
                    field->set_label_colour(&wxGetApp().get_label_clr_default());
                }
                field->set_undo_to_sys_bitmap(&m_bmp_empty);
                field->set_undo_to_sys_tooltip(&m_tt_empty);
                field->m_is_nonsys_value = false;
            }
        group->refresh();
    }
}

void ObjectSettingsTabs::apply_config_manipulation(bool check_values)
{
    auto toggle = [this](const std::string &key, bool enable, int opt_index) {
        for (auto &group : m_groups)
            if (Field *field = group->get_fieldc(key, opt_index))
                field->toggle(enable);
    };
    DynamicPrintConfig before = m_config;
    // A check may change other settings as well, e.g. the infill pattern for 100% infill:
    // those are changes of the selected items too.
    auto load_config = [this, &before]() {
        for (const std::string &key : m_config.diff(before))
            if (m_allowed.find(key) != m_allowed.end())
                for (SettingsTabsItem &item : m_items)
                    item.config->set_key_value(key, m_config.option(key)->clone());
        before = m_config;
        for (auto &group : m_groups)
            group->reload_config();
    };
    ConfigManipulation manipulation(load_config, toggle, nullptr, nullptr, wxGetApp().plater());
    if (check_values)
        manipulation.update_print_fff_config(&m_config);
    manipulation.toggle_print_fff_options(&m_config);
}

void ObjectSettingsTabs::on_value_change(const std::string &opt_id)
{
    const std::string   key    = option_key(opt_id);
    const ConfigOption *option = m_config.option(key);
    if (option == nullptr)
        return;
    for (SettingsTabsItem &item : m_items)
        item.config->set_key_value(key, option->clone());
    apply_config_manipulation(true);
    after_items_changed();

    // Once the event of the edited field is over: the suggestion may change the Print Settings, which rebuilds
    // the page with that field.
    if (key == "support_material_interface_extruder")
        wxTheApp->CallAfter([this]() {
            if (m_shown && m_kind == Kind::Object && !m_items.empty())
                suggest_support_interface_settings();
        });
}

void ObjectSettingsTabs::suggest_support_interface_settings()
{
    // As in the Print Settings, for the filaments of the selected objects only.
    std::vector<const ModelObject*> objects;
    for (const SettingsTabsItem &item : m_items)
        if (const ModelObject *object = wxGetApp().model().objects[item.obj_idx];
            std::find(objects.begin(), objects.end(), object) == objects.end())
            objects.push_back(object);
    const DynamicPrintConfig changes = GUI::suggest_support_interface_settings(m_config, objects);
    if (changes.empty())
        return;

    // The settings of the support are stored on the selected objects; the bed temperature extruder is a setting
    // of the whole print, so it changes in the Print Settings.
    wxGetApp().plater()->take_snapshot(_L("Support interface settings"));
    DynamicPrintConfig print_changes;
    for (const std::string &key : changes.keys()) {
        if (m_allowed.find(key) != m_allowed.end()) {
            for (SettingsTabsItem &item : m_items)
                item.config->set_key_value(key, changes.option(key)->clone());
        } else
            print_changes.set_key_value(key, changes.option(key)->clone());
    }
    compute_config();
    for (auto &group : m_groups)
        group->reload_config();
    apply_config_manipulation(false);
    after_items_changed();

    // Last, as loading the Print Settings selects the object anew, which rebuilds this page.
    if (!print_changes.empty())
        if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
            DynamicPrintConfig new_conf = *tab->get_config();
            new_conf.apply(print_changes);
            tab->load_config(new_conf);
        }
}

void ObjectSettingsTabs::on_revert(const std::string &opt_id)
{
    const std::string key = option_key(opt_id);
    if (std::none_of(m_items.begin(), m_items.end(), [&key](const SettingsTabsItem &item) { return item.config->has(key); }))
        return;
    wxGetApp().plater()->take_snapshot(format_wxstr(_L("Reset Option %s"), key));
    for (SettingsTabsItem &item : m_items)
        item.config->erase(key);
    compute_config();
    for (auto &group : m_groups)
        group->reload_config();
    apply_config_manipulation(false);
    after_items_changed();
}

void ObjectSettingsTabs::after_items_changed()
{
    std::set<int> objects;
    for (const SettingsTabsItem &item : m_items)
        objects.insert(item.obj_idx);
    for (int obj_idx : objects)
        wxGetApp().obj_list()->changed_object(obj_idx);
    update_override_marks();

    // Update the Settings items of the object list once the event of the edited field is over:
    // it may change the selection, which rebuilds the page with that field.
    std::vector<wxDataViewItem> items;
    for (const SettingsTabsItem &item : m_items)
        items.push_back(item.item);
    wxTheApp->CallAfter([items]() {
        for (const wxDataViewItem &item : items)
            wxGetApp().obj_list()->update_settings_item(item);
    });
}

void ObjectSettingsTabs::reload()
{
    if (!m_shown || m_items.empty())
        return;
    compute_config();
    for (auto &group : m_groups)
        group->reload_config();
    update_override_marks();
    apply_config_manipulation(false);
}

void ObjectSettingsTabs::sys_color_changed()
{
    m_bmp_revert.sys_color_changed();
    m_bmp_dot.sys_color_changed();
    m_bmp_empty.sys_color_changed();
    m_tab_bar->sys_color_changed();
    for (auto &group : m_groups)
        group->sys_color_changed();
}

void ObjectSettingsTabs::msw_rescale()
{
    m_tab_bar->msw_rescale();
    for (auto &group : m_groups)
        group->msw_rescale();
}

} // namespace GUI
} // namespace Slic3r
