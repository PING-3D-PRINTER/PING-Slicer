// PING 原生選機視窗——實作。裁定、原型與分工見 PingPrinterPicker.hpp 檔頭。
// 只用 wx 標準元件：這個視窗存在的理由就是「不靠 WebView2 也一定開得起來」。

#include "PingPrinterPicker.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/wrapsizer.h>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cstring>
#include <set>

namespace Slic3r {
namespace GUI {

namespace {

wxString u8(const std::string& s) { return wxString::FromUTF8(s.c_str()); }

/* ── 與網頁版選機頁（resources/web/guide/21/21.js）同一套判準 ──────────────────
   配置字尾的順序就是畫面上的順序（Eric 2026-11-10：本體→同進→單料頭→3in1→關門）。
   「單噴頭」是舊名，只為了認得舊機型名，排在單料頭同一位。 */
struct SuffixDef { const char* suffix; int order; };
const SuffixDef PING_SUFFIXES[] = {
    { "同進",   1 },
    { "單料頭", 2 },
    { "單噴頭", 2 },
    { "3in1",   3 },
    { "關門",   4 },
};

// Classic（前代）系列——同 21.js 的 PingClassicModels，收的是「系列名」。
const char* PING_CLASSIC_SERIES[] = {
    "EDU 200", "PING 200", "PING 270", "PING 300+", "DUAL 300", "DUAL 450", "DUAL 600", "DUAL 800",
};

bool ends_with(const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// 機型名的配置字尾（沒有＝空字串＝本體）
std::string suffix_of(const std::string& model)
{
    for (const SuffixDef& d : PING_SUFFIXES)
        if (ends_with(model, d.suffix))
            return d.suffix;
    return std::string();
}

int suffix_order(const std::string& suffix)
{
    for (const SuffixDef& d : PING_SUFFIXES)
        if (suffix == d.suffix)
            return d.order;
    return 0;   // 本體排最前
}

bool is_classic_series(const std::string& series)
{
    for (const char* s : PING_CLASSIC_SERIES)
        if (series == s)
            return true;
    return false;
}

// 本體的顯示名＝料數（同 21.js PingBaseLabel：FF＝四料、FD／DUAL＝雙料、其餘單料）
wxString base_label(const std::string& series)
{
    if (series.rfind("FF", 0) == 0)
        return _L("Quad material");
    if (series.rfind("FD", 0) == 0 || series.rfind("DUAL", 0) == 0)
        return _L("Dual material");
    return _L("Single material");
}

wxString variant_label(const std::string& model)
{
    const std::string suffix = suffix_of(model);
    if (suffix.empty())
        return base_label(ping_picker_series_of(model));
    if (suffix == "關門")
        return u8("關門（ABS）");
    return u8(suffix);   // 同進／單料頭／3in1：就是機型名上的那個詞
}

wxString variant_hint(const std::string& model)
{
    const std::string suffix = suffix_of(model);
    if (suffix.empty()) {
        const std::string series = ping_picker_series_of(model);
        if (series.rfind("FF", 0) == 0)
            return _L("Four materials, set separately: multi-color, multi-material, easy-release supports");
        if (series.rfind("FD", 0) == 0 || series.rfind("DUAL", 0) == 0)
            return _L("Two materials, set separately: multi-color, multi-material, easy-release supports");
        return _L("One material");
    }
    if (suffix == "同進")
        return _L("Same material fed together: higher flow, color-blend gradients");
    if (suffix == "單料頭" || suffix == "單噴頭")
        return _L("One material, a single spool is enough");
    if (suffix == "3in1")
        return _L("Three main materials merged for triple flow; the fourth prints easy-release supports");
    if (suffix == "關門")
        return _L("For printing ABS with the door closed; the printable area is smaller");
    return wxEmptyString;
}

wxString full_name(const std::string& model)
{
    return u8(ping_picker_series_of(model)) + " " + variant_label(model);
}

/* 機器圖：Fast 用選機頁那套裁好的直式卡片圖（web/guide/img/cards/<系列>.png），
   沒有的（Classic）退回 profiles/PING/<系列>_cover.png。兩種都只是本機 PNG 檔，不經過網頁。
   cover 是 600×600、機器只佔中間一小塊 ⇒ 先裁掉透明留白再縮放，清單小圖才看得出是哪一台。 */
wxImage load_series_image(const std::string& series)
{
    namespace fs = boost::filesystem;
    const fs::path candidates[] = {
        fs::path(resources_dir()) / "web" / "guide" / "img" / "cards" / (series + ".png"),
        fs::path(resources_dir()) / "profiles" / "PING" / (series + "_cover.png"),
    };
    wxLogNull no_popup;   // 圖壞了就當沒有圖，不要跳 wx 的錯誤框擋住選機
    for (const fs::path& p : candidates) {
        boost::system::error_code ec;
        if (!fs::exists(p, ec) || ec)
            continue;
        wxImage img;
        if (!img.LoadFile(u8(p.string()), wxBITMAP_TYPE_PNG) || !img.IsOk())
            continue;
        if (img.HasAlpha()) {
            int x0 = img.GetWidth(), y0 = img.GetHeight(), x1 = -1, y1 = -1;
            for (int y = 0; y < img.GetHeight(); ++y)
                for (int x = 0; x < img.GetWidth(); ++x)
                    if (img.GetAlpha(x, y) > 8) {
                        x0 = std::min(x0, x); x1 = std::max(x1, x);
                        y0 = std::min(y0, y); y1 = std::max(y1, y);
                    }
            if (x1 < x0 || y1 < y0)
                continue;   // 全透明的佔位檔＝當沒有圖，試下一個
            img = img.GetSubImage(wxRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1));
        }
        return img;
    }
    return wxImage();
}

// 等比縮進 w×h 的透明畫布、置中。src 無效＝回傳全透明的空圖（版面不會跳）。
wxBitmap fit_bitmap(const wxImage& src, int w, int h)
{
    wxImage canvas(w, h);
    canvas.InitAlpha();
    std::memset(canvas.GetAlpha(), 0, size_t(w) * size_t(h));
    if (src.IsOk() && src.GetWidth() > 0 && src.GetHeight() > 0) {
        const double k  = std::min(double(w) / src.GetWidth(), double(h) / src.GetHeight());
        const int    nw = std::max(1, int(src.GetWidth() * k));
        const int    nh = std::max(1, int(src.GetHeight() * k));
        wxImage scaled = src.Scale(nw, nh, wxIMAGE_QUALITY_HIGH);
        if (!scaled.HasAlpha())
            scaled.InitAlpha();
        const int ox = (w - nw) / 2, oy = (h - nh) / 2;
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                canvas.SetRGB(ox + x, oy + y, scaled.GetRed(x, y), scaled.GetGreen(x, y), scaled.GetBlue(x, y));
                canvas.SetAlpha(ox + x, oy + y, scaled.GetAlpha(x, y));
            }
    }
    return wxBitmap(canvas);
}

struct Series
{
    std::string              name;
    bool                     classic = false;
    std::vector<std::string> models;   // 已照配置順序排好
    wxImage                  image;
};

class PingPrinterPickerDialog : public wxDialog
{
public:
    PingPrinterPickerDialog(wxWindow* parent, const std::vector<PingPickerModel>& models,
                            const std::vector<std::string>& enabled, const std::string& active, bool first_run);

    PingPickerResult result() const { return m_result; }

private:
    enum class Action { First, Replace, Add, Ok };

    void build();
    void select_line(bool classic);          // 切 Fast／Classic 頁籤（不改選中的機器）
    void fill_series_list();
    void select_series(int series_index, const std::string& prefer_model = std::string());
    void rebuild_config_panel();
    void update_nozzle_note();
    void rebuild_context_panel();
    void refresh_footer();
    void finish(Action action);
    void on_close(bool by_window_close);

    const Series* series_of_pick() const;
    int           series_index_of(const std::string& model) const;
    bool          in_draft(const std::string& model) const
    {
        return std::find(m_draft.begin(), m_draft.end(), model) != m_draft.end();
    }
    const PingPickerModel* model_def(const std::string& id) const
    {
        for (const PingPickerModel& m : m_models)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    const std::vector<PingPickerModel> m_models;
    const bool                         m_first_run;
    std::vector<Series>                m_series;
    std::vector<int>                   m_list_series;     // 清單第 n 列 → m_series 的索引
    size_t                             m_initial_count = 0;
    std::vector<std::string>           m_draft;           // 「我的機器」草稿（按確定才生效）
    std::string                        m_pick;            // 畫面上選中的那一台
    bool                               m_classic = false; // 目前顯示的頁籤
    bool                               m_filling = false; // 程式自己在改清單選取，忽略事件
    PingPickerResult                   m_result;

    wxPanel*        m_ctx_panel   = nullptr;
    wxToggleButton* m_tab_fast    = nullptr;
    wxToggleButton* m_tab_classic = nullptr;
    wxListCtrl*     m_list        = nullptr;
    wxImageList*    m_list_images = nullptr;
    wxStaticBitmap* m_picture     = nullptr;
    wxStaticText*   m_series_name = nullptr;
    wxPanel*        m_cfg_panel   = nullptr;
    wxStaticText*   m_nozzle_note = nullptr;
    wxStaticText*   m_summary     = nullptr;
    wxButton*       m_btn_primary = nullptr;
    wxButton*       m_btn_add     = nullptr;
    wxButton*       m_btn_cancel  = nullptr;
    Action          m_primary_action = Action::First;
};

PingPrinterPickerDialog::PingPrinterPickerDialog(wxWindow* parent, const std::vector<PingPickerModel>& models,
                                                 const std::vector<std::string>& enabled, const std::string& active,
                                                 bool first_run)
    : wxDialog(parent, wxID_ANY, wxString("PING Slicer"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE)
    , m_models(models)
    , m_first_run(first_run)
{
    // 分系列（照 bundle 裡第一次出現的順序），系列內照配置順序
    for (const PingPickerModel& m : m_models) {
        const std::string name = ping_picker_series_of(m.id);
        auto it = std::find_if(m_series.begin(), m_series.end(), [&](const Series& s) { return s.name == name; });
        if (it == m_series.end()) {
            Series s;
            s.name    = name;
            s.classic = is_classic_series(name);
            m_series.push_back(std::move(s));
            it = m_series.end() - 1;
        }
        it->models.push_back(m.id);
    }
    for (Series& s : m_series) {
        std::stable_sort(s.models.begin(), s.models.end(), [](const std::string& a, const std::string& b) {
            return suffix_order(suffix_of(a)) < suffix_order(suffix_of(b));
        });
        s.image = load_series_image(s.name);
    }

    if (!m_first_run)
        for (const PingPickerModel& m : m_models)   // 草稿照 models 的順序，不照傳進來的順序
            if (std::find(enabled.begin(), enabled.end(), m.id) != enabled.end())
                m_draft.push_back(m.id);
    m_initial_count = m_draft.size();

    /* 一開始選中的那一台：
       之後換機＝目前在用的 → 第一台已啟用的；
       第一次開啟＝FD300（Eric 2026-11-10 裁預選 FD300 雙料）——不看 active：帶著專案檔啟動時 active 會是專案裡的機型。 */
    if (!m_first_run && model_def(active) != nullptr)
        m_pick = active;
    else if (!m_draft.empty())
        m_pick = m_draft.front();
    else if (model_def("FD300") != nullptr)
        m_pick = "FD300";
    else if (!m_models.empty())
        m_pick = m_models.front().id;

    SetBackgroundColour(*wxWHITE);   // 白底為主（PING CIS）；深色模式由下面的 UpdateDlgDarkUI 接手
    build();

    const int idx = series_index_of(m_pick);
    m_classic = idx >= 0 && m_series[idx].classic;
    select_line(m_classic);
    select_series(idx, m_pick);
    rebuild_context_panel();
    refresh_footer();

    GetSizer()->SetSizeHints(this);
    // 系列名那一欄吃掉剩下的寬度，選取的底色才會鋪滿整列
    m_list->SetColumnWidth(0, std::max(FromDIP(120), m_list->GetClientSize().GetWidth() - m_list->GetColumnWidth(1)));
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void PingPrinterPickerDialog::build()
{
    const int pad = FromDIP(12);
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxStaticText* title = new wxStaticText(this, wxID_ANY, _L("Choose your printer"));
    title->SetFont(GetFont().Bold().Scaled(1.35f));
    root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    wxStaticText* lead = new wxStaticText(this, wxID_ANY,
        m_first_run ? _L("Pick one and you are ready to go. You can switch later from the printer list on the main screen.")
                    : _L("Pick one, then switch to it or add it to your printers."));
    root->Add(lead, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    // 「我的機器」／「目前使用」列（第一次開啟沒有這一列）
    m_ctx_panel = new wxPanel(this);
    m_ctx_panel->SetBackgroundColour(GetBackgroundColour());
    m_ctx_panel->SetSizer(new wxBoxSizer(wxVERTICAL));
    root->Add(m_ctx_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    m_ctx_panel->Show(!m_first_run);

    wxBoxSizer* cols = new wxBoxSizer(wxHORIZONTAL);

    // ── 左欄：Fast／Classic 頁籤＋系列清單（附機器圖）
    wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* tabs = new wxBoxSizer(wxHORIZONTAL);
    m_tab_fast    = new wxToggleButton(this, wxID_ANY, "Fast");
    m_tab_classic = new wxToggleButton(this, wxID_ANY, "Classic");
    tabs->Add(m_tab_fast, 1, wxEXPAND);
    tabs->Add(m_tab_classic, 1, wxEXPAND);
    left->Add(tabs, 0, wxEXPAND);

    const int thumb_w = FromDIP(33), thumb_h = FromDIP(44);
    m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(250), thumb_h * 8 + FromDIP(28)),
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    m_list_images = new wxImageList(thumb_w, thumb_h, false, int(m_series.size()));
    for (const Series& s : m_series)
        m_list_images->Add(fit_bitmap(s.image, thumb_w, thumb_h));
    m_list->AssignImageList(m_list_images, wxIMAGE_LIST_SMALL);
    m_list->SetFont(GetFont().Scaled(1.15f));
    m_list->InsertColumn(0, wxEmptyString, wxLIST_FORMAT_LEFT, FromDIP(160));
    m_list->InsertColumn(1, wxEmptyString, wxLIST_FORMAT_RIGHT, FromDIP(64));
    left->Add(m_list, 1, wxEXPAND | wxTOP, FromDIP(2));
    cols->Add(left, 0, wxEXPAND);

    // ── 右欄：機器圖＋系列名＋配置單選
    m_picture = new wxStaticBitmap(this, wxID_ANY, fit_bitmap(wxImage(), FromDIP(165), FromDIP(220)));
    cols->Add(m_picture, 0, wxLEFT | wxALIGN_TOP, pad);

    wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
    m_series_name = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_series_name->SetFont(GetFont().Bold().Scaled(1.6f));
    right->Add(m_series_name, 0, wxBOTTOM, FromDIP(6));
    right->Add(new wxStaticText(this, wxID_ANY, _L("Machine setup")), 0, wxBOTTOM, FromDIP(4));
    m_cfg_panel = new wxPanel(this);
    m_cfg_panel->SetBackgroundColour(GetBackgroundColour());
    m_cfg_panel->SetSizer(new wxBoxSizer(wxVERTICAL));
    right->Add(m_cfg_panel, 0, wxEXPAND);
    right->Add(new wxStaticLine(this), 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));
    m_nozzle_note = new wxStaticText(this, wxID_ANY, wxEmptyString);
    right->Add(m_nozzle_note, 0);
    // 右欄給固定的最小寬：換系列時視窗寬度不要跟著文字長短跳
    right->SetMinSize(wxSize(FromDIP(330), -1));
    cols->Add(right, 1, wxEXPAND | wxLEFT, pad);

    root->Add(cols, 1, wxEXPAND | wxALL, pad);
    root->Add(new wxStaticLine(this), 0, wxEXPAND);

    // ── 底列：已選摘要＋動作鈕
    wxBoxSizer* foot = new wxBoxSizer(wxHORIZONTAL);
    m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
    foot->Add(m_summary, 1, wxALIGN_CENTER_VERTICAL);
    m_btn_primary = new wxButton(this, wxID_OK, _L("OK"));
    m_btn_add     = new wxButton(this, wxID_ANY, _L("Add this one"));
    m_btn_cancel  = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    foot->Add(m_btn_primary, 0, wxLEFT, FromDIP(8));
    foot->Add(m_btn_add, 0, wxLEFT, FromDIP(8));
    foot->Add(m_btn_cancel, 0, wxLEFT, FromDIP(8));
    root->Add(foot, 0, wxEXPAND | wxALL, pad);
    m_btn_primary->SetDefault();

    SetSizer(root);

    m_tab_fast->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { select_line(false); });
    m_tab_classic->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { select_line(true); });
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& evt) {
        if (m_filling)
            return;
        const long row = evt.GetIndex();
        if (row >= 0 && row < long(m_list_series.size()))
            select_series(m_list_series[size_t(row)]);
    });
    m_btn_primary->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { finish(m_primary_action); });
    m_btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { finish(Action::Add); });
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_close(false); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { on_close(true); });
}

const Series* PingPrinterPickerDialog::series_of_pick() const
{
    const int idx = series_index_of(m_pick);
    return idx >= 0 ? &m_series[size_t(idx)] : nullptr;
}

int PingPrinterPickerDialog::series_index_of(const std::string& model) const
{
    const std::string name = ping_picker_series_of(model);
    for (size_t i = 0; i < m_series.size(); ++i)
        if (m_series[i].name == name)
            return int(i);
    return -1;
}

void PingPrinterPickerDialog::select_line(bool classic)
{
    m_classic = classic;
    m_tab_fast->SetValue(!classic);
    m_tab_classic->SetValue(classic);
    fill_series_list();
}

void PingPrinterPickerDialog::fill_series_list()
{
    m_filling = true;
    m_list->DeleteAllItems();
    m_list_series.clear();
    const std::string picked_series = ping_picker_series_of(m_pick);
    for (size_t i = 0; i < m_series.size(); ++i) {
        const Series& s = m_series[i];
        if (s.classic != m_classic)
            continue;
        const long row = m_list->InsertItem(long(m_list_series.size()), u8(s.name), int(i));
        m_list->SetItem(row, 1, base_label(s.name));
        m_list_series.push_back(int(i));
        if (s.name == picked_series) {
            m_list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                 wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            m_list->EnsureVisible(row);
        }
    }
    m_filling = false;
}

void PingPrinterPickerDialog::select_series(int series_index, const std::string& prefer_model)
{
    if (series_index < 0 || series_index >= int(m_series.size()))
        return;
    const Series& s = m_series[size_t(series_index)];
    if (s.models.empty())
        return;
    // 點系列＝選它的第一個配置（本體）；prefer_model 給「一開視窗就要落在某一台」用
    m_pick = (!prefer_model.empty() && std::find(s.models.begin(), s.models.end(), prefer_model) != s.models.end())
                 ? prefer_model : s.models.front();

    m_picture->SetBitmap(fit_bitmap(s.image, FromDIP(165), FromDIP(220)));
    m_series_name->SetLabel(u8(s.name));
    rebuild_config_panel();
    rebuild_context_panel();
    refresh_footer();
}

void PingPrinterPickerDialog::rebuild_config_panel()
{
    const Series* s = series_of_pick();
    if (s == nullptr)
        return;
    Freeze();
    m_cfg_panel->DestroyChildren();
    wxSizer* sizer = m_cfg_panel->GetSizer();
    sizer->Clear();
    const wxColour hint_colour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    bool first = true;
    for (const std::string& model : s->models) {
        wxRadioButton* rb = new wxRadioButton(m_cfg_panel, wxID_ANY, variant_label(model), wxDefaultPosition,
                                              wxDefaultSize, first ? wxRB_GROUP : 0);
        rb->SetFont(GetFont().Scaled(1.1f));
        rb->SetValue(model == m_pick);
        rb->Bind(wxEVT_RADIOBUTTON, [this, model](wxCommandEvent&) {
            m_pick = model;
            // 重畫「我的機器」列會動到版面，延到這個事件處理完再做
            CallAfter([this] {
                update_nozzle_note();
                rebuild_context_panel();
                refresh_footer();
            });
        });
        sizer->Add(rb, 0, wxTOP, first ? 0 : FromDIP(8));
        // 說明字縮排到與單選鈕的文字對齊
        wxStaticText* hint = new wxStaticText(m_cfg_panel, wxID_ANY, variant_hint(model));
        hint->SetForegroundColour(hint_colour);
        wxBoxSizer* indent = new wxBoxSizer(wxHORIZONTAL);
        indent->AddSpacer(FromDIP(20));
        indent->Add(hint, 1);
        sizer->Add(indent, 0, wxEXPAND | wxTOP, FromDIP(2));
        first = false;
    }
    update_nozzle_note();
    Layout();
    GetSizer()->SetSizeHints(this);
    Thaw();
}

void PingPrinterPickerDialog::update_nozzle_note()
{
    wxString nozzles;
    if (const PingPickerModel* def = model_def(m_pick))
        for (const std::string& v : def->variants)
            nozzles += (nozzles.empty() ? "" : " / ") + u8(v);
    // 口徑不在這裡選（Eric 2026-08-13 丙案）：啟用一台＝該機全部口徑一起啟用
    m_nozzle_note->SetLabel(wxString::Format(_L("Nozzle sizes %s mm are all installed; choose one when you slice."), nozzles));
}

void PingPrinterPickerDialog::rebuild_context_panel()
{
    if (m_first_run)
        return;
    Freeze();
    m_ctx_panel->DestroyChildren();
    wxSizer* sizer = m_ctx_panel->GetSizer();
    sizer->Clear();
    const wxColour hint_colour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);

    if (m_initial_count > 1) {
        // 啟用超過一台才出現「我的機器」列：可移除、至少留一台
        wxWrapSizer* chips = new wxWrapSizer(wxHORIZONTAL);
        chips->Add(new wxStaticText(m_ctx_panel, wxID_ANY, _L("My printers:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        for (const std::string& model : m_draft) {
            wxStaticText* name = new wxStaticText(m_ctx_panel, wxID_ANY, full_name(model));
            name->SetFont(GetFont().Bold());
            chips->Add(name, 0, wxALIGN_CENTER_VERTICAL);
            wxButton* remove = new wxButton(m_ctx_panel, wxID_ANY, u8("✕"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            remove->SetToolTip(wxString::Format(_L("Remove %s"), full_name(model)));
            remove->Enable(m_draft.size() > 1);
            remove->Bind(wxEVT_BUTTON, [this, model](wxCommandEvent&) {
                // 這顆鈕自己會在重畫時被銷毀 ⇒ 等事件處理完再動
                CallAfter([this, model] {
                    if (m_draft.size() <= 1)
                        return;
                    m_draft.erase(std::remove(m_draft.begin(), m_draft.end(), model), m_draft.end());
                    if (m_pick == model) {   // 把畫面上選中的那台移掉了＝改選留下來的第一台
                        m_pick = m_draft.front();
                        const int idx = series_index_of(m_pick);
                        select_line(idx >= 0 && m_series[size_t(idx)].classic);
                        select_series(idx, m_pick);
                        return;
                    }
                    rebuild_context_panel();
                    refresh_footer();
                });
            });
            chips->Add(remove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(2));
            chips->AddSpacer(FromDIP(14));
        }
        if (!in_draft(m_pick))
            chips->Add(new wxStaticText(m_ctx_panel, wxID_ANY, wxString::Format(_L("+ %s (not added yet)"), full_name(m_pick))),
                       0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(chips, 0, wxEXPAND);
        wxStaticText* note = new wxStaticText(m_ctx_panel, wxID_ANY,
            !in_draft(m_pick)    ? _L("\"Switch to this one\" keeps only this printer; \"Add this one\" keeps the others too.")
            : m_draft.size() == 1 ? _L("At least one printer must stay.")
                                  : _L("Use the X button to remove printers you no longer use. At least one must stay. Changes apply when you press OK."));
        note->SetForegroundColour(hint_colour);
        sizer->Add(note, 0, wxTOP, FromDIP(4));
    } else if (!m_draft.empty()) {
        sizer->Add(new wxStaticText(m_ctx_panel, wxID_ANY, wxString::Format(_L("Currently using: %s"), full_name(m_draft.front()))), 0);
    }
    wxGetApp().UpdateDarkUIWin(m_ctx_panel);
    Layout();
    GetSizer()->SetSizeHints(this);
    Thaw();
}

void PingPrinterPickerDialog::refresh_footer()
{
    m_summary->SetLabel(wxString::Format(_L("Selected: %s"), full_name(m_pick)));
    bool show_add = false;
    if (m_first_run) {
        m_primary_action = Action::First;
        m_btn_primary->SetLabel(_L("OK, start using"));
    } else if (in_draft(m_pick)) {
        m_primary_action = Action::Ok;
        m_btn_primary->SetLabel(_L("OK"));
    } else {
        m_primary_action = Action::Replace;
        m_btn_primary->SetLabel(_L("Switch to this one"));
        show_add = true;
    }
    m_btn_add->Show(show_add);
    m_btn_cancel->Show(!m_first_run);
    Layout();
}

void PingPrinterPickerDialog::finish(Action action)
{
    if (model_def(m_pick) == nullptr)
        return;
    std::vector<std::string> wanted;
    switch (action) {
    case Action::First:
    case Action::Replace: wanted = { m_pick }; break;
    case Action::Add:     wanted = m_draft; wanted.push_back(m_pick); break;
    case Action::Ok:      wanted = m_draft; break;
    }
    m_result.accepted = true;
    m_result.active   = m_pick;
    m_result.enabled.clear();
    for (const PingPickerModel& m : m_models)   // 輸出照 models 的順序
        if (std::find(wanted.begin(), wanted.end(), m.id) != wanted.end())
            m_result.enabled.push_back(m.id);
    EndModal(wxID_OK);
}

void PingPrinterPickerDialog::on_close(bool /*by_window_close*/)
{
    if (m_first_run) {
        // Eric 2026-11-10 裁 Q3：第一次開啟按 ✕＝照畫面上當時選的那一台直接進主畫面
        finish(Action::First);
        return;
    }
    m_result = PingPickerResult();
    EndModal(wxID_CANCEL);
}

} // namespace

std::string ping_picker_series_of(const std::string& model)
{
    const std::string suffix = suffix_of(model);
    if (suffix.empty())
        return model;
    std::string series = model.substr(0, model.size() - suffix.size());
    while (!series.empty() && series.back() == ' ')
        series.pop_back();
    return series;
}

PingPickerResult show_ping_printer_picker(wxWindow* parent, const std::vector<PingPickerModel>& models,
                                          const std::vector<std::string>& enabled, const std::string& active,
                                          bool first_run)
{
    if (models.empty())
        return PingPickerResult();
    PingPrinterPickerDialog dlg(parent, models, enabled, active, first_run);
    dlg.ShowModal();
    return dlg.result();
}

} // namespace GUI
} // namespace Slic3r
