#ifndef slic3r_GUI_ObjectSettingsTabs_hpp_
#define slic3r_GUI_ObjectSettingsTabs_hpp_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wx/dataview.h>

#include "libslic3r/PrintConfig.hpp"
#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"

class wxBoxSizer;
class wxScrolledWindow;

namespace Slic3r {
class ModelConfig;
namespace GUI {

// An object, a part or modifier, or a height range whose settings the tabs show.
struct SettingsTabsItem
{
    wxDataViewItem      item;
    int                 obj_idx { -1 };
    ModelConfig*        config { nullptr };
    // The settings of the object a part or height range belongs to, nullptr for an object.
    const ModelConfig*  object_config { nullptr };
};

// The settings of the selected objects, parts or height ranges in the sidebar, as tabs laid out as
// the pages of the Print Settings: every setting the selection can override shows the value it prints
// with, and a changed value is stored on the selected items only. The first tab holds the panel the
// sidebar shows for the selection without the tabs: Object manipulation, or the height range.
class ObjectSettingsTabs
{
public:
    enum class Kind { Object, Part, HeightRange };

    explicit ObjectSettingsTabs(wxWindow* parent);
    ~ObjectSettingsTabs();

    wxSizer*    get_sizer() { return m_sizer; }
    // Whether the first tab (Object manipulation, height range) is the one shown.
    bool        first_tab_active() const;
    // Whether a page of settings is shown, which then takes the height of the sidebar left.
    bool        page_shown() const { return m_shown && active_page() != nullptr; }

    // first_tab_icon / first_tab_name describe the first tab; an empty icon if the selection has no such panel.
    void        show(Kind kind, std::vector<SettingsTabsItem> items, const std::string &first_tab_icon, const wxString &first_tab_name);
    void        hide();
    // Read the values again, e.g. after the print settings changed.
    void        reload();
    void        sys_color_changed();
    void        msw_rescale();

private:
    class TabBar;

    struct LineDef {
        wxString            label;
        wxString            tooltip;
        std::string         path;
        std::vector<Option> options;
    };
    struct GroupDef {
        wxString             title;
        std::vector<LineDef> lines;
    };
    struct PageDef {
        wxString              title;
        std::string           icon;
        std::vector<GroupDef> groups;
    };

    void        build_page_defs();
    void        select_tab(int tab);
    void        build_page();
    void        clear_page();
    void        compute_config();
    void        update_override_marks();
    void        apply_config_manipulation(bool check_values);
    void        on_value_change(const std::string &opt_id);
    // The suggestion of the Print Settings for a support interface of a different or soluble material.
    void        suggest_support_interface_settings();
    void        on_revert(const std::string &opt_id);
    void        after_items_changed();
    // The page shown, or nullptr for the first tab.
    const PageDef* active_page() const;

    wxWindow*                           m_parent;
    wxBoxSizer*                         m_sizer;
    // The settings page scrolls on its own, below the tabs.
    wxScrolledWindow*                   m_page_window;
    wxBoxSizer*                         m_page_sizer;
    wxString                            m_built_title;
    TabBar*                             m_tab_bar;

    Kind                                m_kind { Kind::Object };
    std::vector<SettingsTabsItem>       m_items;
    std::vector<PageDef>                m_pages;
    // The settings the selection can override.
    std::set<std::string>               m_allowed;
    bool                                m_has_first_tab { false };
    std::string                         m_first_tab_icon;
    wxString                            m_first_tab_name;
    // Title of the page shown, kept while the selection changes; empty for the first tab.
    wxString                            m_active_title;
    bool                                m_shown { false };

    // The values the selection prints with: the print settings, the object's and the item's own.
    DynamicPrintConfig                  m_config;
    std::vector<std::shared_ptr<ConfigOptionsGroup>> m_groups;
    std::map<std::string, wxString>     m_override_tooltips;

    ScalableBitmap                      m_bmp_revert;
    ScalableBitmap                      m_bmp_dot;
    ScalableBitmap                      m_bmp_empty;
    wxString                            m_tt_not_overridden;
    wxString                            m_tt_empty;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ObjectSettingsTabs_hpp_
