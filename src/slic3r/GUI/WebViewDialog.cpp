#include "WebViewDialog.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PhotoTileCapability.hpp"
#include <sstream>
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r_version.h"
#include "../Utils/Http.hpp"
#include "../Utils/PingAiImage.hpp"   // 丙案：只用它的 available()（問有沒有金鑰，不取明文）

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/base64.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/url.h>

#include <algorithm>
#include <vector>

#include <slic3r/GUI/Widgets/WebView.hpp>

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {
    namespace {
    wxString homepage_url()
    {
        wxString url = wxString::Format("file://%s/web/homepage/index.html", from_u8(resources_dir()));
        const wxString language = wxGetApp().current_language_code_safe();
        if (!language.IsEmpty())
            url = wxString::Format("file://%s/web/homepage/index.html?lang=%s", from_u8(resources_dir()), language);
        return url;
    }

    wxString photo_tile_url()
    {
        return wxString::Format("file://%s/web/phototile/index.html?mode=vertical&embedded=1", from_u8(resources_dir()));
    }

    wxString step_repair_url()
    {
        /* 【2026-08-23・Eric 裁「甲」】改走虛擬主機，不再用 file://。
           理由：修補核心以 XHR 取 .wasm，file:// origin 會被 CORS 擋死（見 WebView.cpp 註解）。
           映射目標是 resources 根，所以路徑仍是 /web/step-repair/index.html
           ⇒ IsStepRepairPage() 與 OnNavigationRequest 的字串比對不受影響。
           照片磚與首頁**維持 file://**（它們只用 <script>、不需要 XHR），本次不動。 */
        return wxString::Format("https://%s/web/step-repair/index.html?embedded=1", WebView::virtual_host());
    }

    // PING #34：看門狗等多久（D3＝60 秒，從內嵌網頁開始建立起算；Eric 2026-09-29 照建議）與自動重建上限。
    constexpr int HOME_WATCHDOG_MS       = 60 * 1000;
    constexpr int HOME_MAX_AUTO_REBUILDS = 2;

    long long ms_since(std::chrono::steady_clock::time_point t)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
    }

    /* 【僅驗證用】環境變數 PING_TEST_HOME_BREAK=N：前 N 次建立的首頁內嵌網頁改載一個不存在的頁 ⇒ 頁面永遠活不起來，
       用來決定性地走到看門狗、自動重建與上限後的提示（同照片磚宿主 test_break_engine_url 的做法）。沒設＝不作用。 */
    wxString take_test_break_url()
    {
        static int remaining = [] {
            wxString v;
            return wxGetEnv("PING_TEST_HOME_BREAK", &v) ? wxAtoi(v) : 0;
        }();
        if (remaining <= 0)
            return wxEmptyString;
        --remaining;
        return wxString::Format("file://%s/web/homepage/__ping_test_missing__.html", from_u8(resources_dir()));
    }
    }

    wxDECLARE_EVENT(EVT_RESPONSE_MESSAGE, wxCommandEvent);

    wxDEFINE_EVENT(EVT_RESPONSE_MESSAGE, wxCommandEvent);

    #define LOGIN_INFO_UPDATE_TIMER_ID 10002

    BEGIN_EVENT_TABLE(WebViewPanel, wxPanel)
    EVT_TIMER(LOGIN_INFO_UPDATE_TIMER_ID, WebViewPanel::OnFreshLoginStatus)
    END_EVENT_TABLE()



WebViewPanel::WebViewPanel(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
        , m_home_watchdog(this)
 {
    m_target_url = homepage_url();

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
    
#if !BBL_RELEASE_TO_PUBLIC
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, _L("Back"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, _L("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_forward->Enable(false);
    bSizer_toolbar->Add(m_button_forward, 0, wxALL, 5);

    m_button_stop = new wxButton(this, wxID_ANY, _L("Stop"), wxDefaultPosition, wxDefaultSize, 0);

    bSizer_toolbar->Add(m_button_stop, 0, wxALL, 5);

    m_button_reload = new wxButton(this, wxID_ANY, _L("Reload"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_reload, 0, wxALL, 5);

    m_url = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    bSizer_toolbar->Add(m_url, 1, wxALL | wxEXPAND, 5);

    m_button_tools = new wxButton(this, wxID_ANY, _L("Tools"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_tools, 0, wxALL, 5);

    topsizer->Add(bSizer_toolbar, 0, wxEXPAND, 0);
    bSizer_toolbar->Show(false);

    // Create panel for find toolbar.
    wxPanel* panel = new wxPanel(this);
    topsizer->Add(panel, wxSizerFlags().Expand());

    // Create sizer for panel.
    wxBoxSizer* panel_sizer = new wxBoxSizer(wxVERTICAL);
    panel->SetSizer(panel_sizer);
#endif //BBL_RELEASE_TO_PUBLIC
    // Create the info panel
    m_info = new wxInfoBar(this);
    topsizer->Add(m_info, wxSizerFlags().Expand());
    // PING #34：內嵌網頁本身不在這裡建（見建構子最後的 CallWhenCreationAllowed 與 create_browser()）。
    SetSizer(topsizer);

    // Log backend information
    /* m_browser->GetUserAgent() may lead crash
    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("Backend: %s Version: %s", m_browser->GetClassInfo()->GetClassName(),
            wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("User Agent: %s", m_browser->GetUserAgent());
    }
    */

    // Create the Tools menu
    m_tools_menu = new wxMenu();
    wxMenuItem* viewSource = m_tools_menu->Append(wxID_ANY, _L("View Source"));
    wxMenuItem* viewText = m_tools_menu->Append(wxID_ANY, _L("View Text"));
    m_tools_menu->AppendSeparator();
    m_tools_handle_navigation = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Handle Navigation"));
    m_tools_handle_new_window = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Handle New Windows"));
    m_tools_menu->AppendSeparator();

    //Create an editing menu
    wxMenu* editmenu = new wxMenu();
    m_edit_cut = editmenu->Append(wxID_ANY, _L("Cut"));
    m_edit_copy = editmenu->Append(wxID_ANY, _L("Copy"));
    m_edit_paste = editmenu->Append(wxID_ANY, _L("Paste"));
    editmenu->AppendSeparator();
    m_edit_undo = editmenu->Append(wxID_ANY, _L("Undo"));
    m_edit_redo = editmenu->Append(wxID_ANY, _L("Redo"));
    editmenu->AppendSeparator();
    m_edit_mode = editmenu->AppendCheckItem(wxID_ANY, _L("Edit Mode"));
    m_tools_menu->AppendSubMenu(editmenu, "Edit");

    wxMenu* script_menu = new wxMenu;
    m_script_string = script_menu->Append(wxID_ANY, "Return String");
    m_script_integer = script_menu->Append(wxID_ANY, "Return integer");
    m_script_double = script_menu->Append(wxID_ANY, "Return double");
    m_script_bool = script_menu->Append(wxID_ANY, "Return bool");
    m_script_object = script_menu->Append(wxID_ANY, "Return JSON object");
    m_script_array = script_menu->Append(wxID_ANY, "Return array");
    m_script_dom = script_menu->Append(wxID_ANY, "Modify DOM");
    m_script_undefined = script_menu->Append(wxID_ANY, "Return undefined");
    m_script_null = script_menu->Append(wxID_ANY, "Return null");
    m_script_date = script_menu->Append(wxID_ANY, "Return Date");
    m_script_message = script_menu->Append(wxID_ANY, "Send script message");
    m_script_custom = script_menu->Append(wxID_ANY, "Custom script");
    m_tools_menu->AppendSubMenu(script_menu, _L("Run Script"));
    wxMenuItem* addUserScript = m_tools_menu->Append(wxID_ANY, _L("Add user script"));
    wxMenuItem* setCustomUserAgent = m_tools_menu->Append(wxID_ANY, _L("Set custom user agent"));

    //Selection menu
    wxMenu* selection = new wxMenu();
    m_selection_clear = selection->Append(wxID_ANY, _L("Clear Selection"));
    m_selection_delete = selection->Append(wxID_ANY, _L("Delete Selection"));
    wxMenuItem* selectall = selection->Append(wxID_ANY, _L("Select All"));

    editmenu->AppendSubMenu(selection, "Selection");

    wxMenuItem* loadscheme = m_tools_menu->Append(wxID_ANY, _L("Custom Scheme Example"));
    wxMenuItem* usememoryfs = m_tools_menu->Append(wxID_ANY, _L("Memory File System Example"));

    m_context_menu = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Enable Context Menu"));
    m_dev_tools = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Enable Dev Tools"));

    //By default we want to handle navigation and new windows
    m_tools_handle_navigation->Check();
    m_tools_handle_new_window->Check();

    //Zoom
    m_zoomFactor = 100;

    // Connect the button events
#if !BBL_RELEASE_TO_PUBLIC
    Bind(wxEVT_BUTTON, &WebViewPanel::OnBack, this, m_button_back->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnForward, this, m_button_forward->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnStop, this, m_button_stop->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnReload, this, m_button_reload->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnToolsClicked, this, m_button_tools->GetId());
    Bind(wxEVT_TEXT_ENTER, &WebViewPanel::OnUrl, this, m_url->GetId());

#endif //BBL_RELEASE_TO_PUBLIC

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::OnNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &WebViewPanel::OnNavigationComplete, this);
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewPanel::OnDocumentLoaded, this);
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &WebViewPanel::OnTitleChanged, this);
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewPanel::OnError, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &WebViewPanel::OnNewWindow, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::OnScriptMessage, this);
    Bind(EVT_RESPONSE_MESSAGE, &WebViewPanel::OnScriptResponseMessage, this);

    // Connect the menu events
    Bind(wxEVT_MENU, &WebViewPanel::OnViewSourceRequest, this, viewSource->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnViewTextRequest, this, viewText->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnCut, this, m_edit_cut->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnCopy, this, m_edit_copy->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnPaste, this, m_edit_paste->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnUndo, this, m_edit_undo->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRedo, this, m_edit_redo->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnMode, this, m_edit_mode->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptString, this, m_script_string->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptInteger, this, m_script_integer->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptDouble, this, m_script_double->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptBool, this, m_script_bool->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptObject, this, m_script_object->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptArray, this, m_script_array->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptDOM, this, m_script_dom->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptUndefined, this, m_script_undefined->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptNull, this, m_script_null->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptDate, this, m_script_date->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptMessage, this, m_script_message->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptCustom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnAddUserScript, this, addUserScript->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnSetCustomUserAgent, this, setCustomUserAgent->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnClearSelection, this, m_selection_clear->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnDeleteSelection, this, m_selection_delete->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnSelectAll, this, selectall->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnLoadScheme, this, loadscheme->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnUseMemoryFS, this, usememoryfs->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnEnableContextMenu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnEnableDevTools, this, m_dev_tools->GetId());

    //Connect the idle events
    Bind(wxEVT_IDLE, &WebViewPanel::OnIdle, this);
    Bind(wxEVT_CLOSE_WINDOW, &WebViewPanel::OnClose, this);

    // PING #34
    Bind(EVT_WEBVIEW_BACKEND_READY, &WebViewPanel::OnBrowserBackendReady, this);
    Bind(EVT_WEBVIEW_PROCESS_FAILED, &WebViewPanel::OnBrowserProcessFailed, this);
    Bind(wxEVT_TIMER, &WebViewPanel::OnHomeWatchdog, this, m_home_watchdog.GetId());

    m_LoginUpdateTimer = nullptr;

    // PING #34（L1）：開機初始化做完才建內嵌網頁；之後（例如切換語言重建主視窗）登記就是立刻建。
    WebView::CallWhenCreationAllowed(this, [this] { create_browser("startup"); });
 }

WebViewPanel::~WebViewPanel()
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " Start";
    m_closing = true;
    m_home_watchdog.Stop();
    SetEvtHandlerEnabled(false);
    
    delete m_tools_menu;

    if (m_LoginUpdateTimer != nullptr) {
        m_LoginUpdateTimer->Stop();
        delete m_LoginUpdateTimer;
        m_LoginUpdateTimer = NULL;
    }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " End";
}


void WebViewPanel::load_url(wxString& url)
{
    this->Show();
    this->Raise();
    if (m_url)
        m_url->SetLabelText(url);

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("Loading URL: %s", url);
    m_target_url = url;
    if (m_browser == nullptr) // PING #34：還沒建好（開機初始化還沒做完／重建中）⇒ 建好時就載這一頁
        return;
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

/* PING #34（L1）：真的建立內嵌網頁。開機時由 WebView::AllowCreation()（post_init 之後）呼叫，
   重建時由 rebuild_browser() 呼叫。建好之後 60 秒內頁面沒活起來（第一個訊息或導覽成功）就交給看門狗。 */
void WebViewPanel::create_browser(const char *why)
{
    if (m_browser != nullptr || m_closing || wxGetApp().is_closing())
        return;
    wxString url = m_target_url.IsEmpty() ? homepage_url() : m_target_url;
    const wxString broken = take_test_break_url();
    if (!broken.IsEmpty()) {
        BOOST_LOG_TRIVIAL(warning) << "WebViewPanel: PING_TEST_HOME_BREAK in effect, loading a missing page instead";
        url = broken;
    }
    m_browser_alive      = false;
    m_browser_created_at = std::chrono::steady_clock::now();
    m_browser            = WebView::CreateWebView(this, url);
    m_browser->Hide(); // 導覽完成才顯示（OnNavigationComplete），同原本
    GetSizer()->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
    Layout();
    BOOST_LOG_TRIVIAL(info) << "WebViewPanel: browser created (" << why << ")";
#ifdef __WIN32__
    m_home_watchdog.StartOnce(HOME_WATCHDOG_MS); // 只有 WebView2 有這個病；其他平台照舊
#endif
}

/* PING #34（L2）：把內嵌網頁拆掉重建。automatic＝看門狗／頁面還沒活起來就失效（有上限）；
   否則是使用者按了「重新載入」。重建一律排到下一輪事件迴圈，不在任何回呼裡做，
   舊內嵌網頁晚到的回呼就碰不到新物件。 */
void WebViewPanel::rebuild_browser(const char *why, bool automatic)
{
    if (m_closing || m_rebuild_pending)
        return;
    if (automatic && m_auto_rebuilds >= HOME_MAX_AUTO_REBUILDS) {
        BOOST_LOG_TRIVIAL(error) << "WebViewPanel: page still not alive after " << m_auto_rebuilds
                                 << " automatic rebuilds (" << why << "), showing the reload notice";
        show_failed_notice(true);
        return;
    }
    if (automatic)
        ++m_auto_rebuilds;
    m_rebuild_pending = true;
    m_home_watchdog.Stop();
    BOOST_LOG_TRIVIAL(warning) << "WebViewPanel: rebuilding the browser (" << why << "), "
                               << (automatic ? "automatic " + std::to_string(m_auto_rebuilds) + "/" + std::to_string(HOME_MAX_AUTO_REBUILDS) : std::string("by user"));
    CallAfter([this, why] {
        m_rebuild_pending = false;
        if (m_closing)
            return;
        if (wxWebView *old = m_browser) {
            m_browser = nullptr;
            GetSizer()->Detach(old);
            old->Hide();
            // wx 的建立回呼綁的是裸指標：後端還沒建好就刪掉，晚到的回呼會打到已釋放的物件。
            // 所以只刪「後端已建好」的；還沒建好的藏起來留著（它之後想載的頁會在 OnNavigationRequest 被擋掉）。
            if (old->GetNativeBackend() != nullptr)
                old->Destroy();
            else
                BOOST_LOG_TRIVIAL(warning) << "WebViewPanel: old browser never finished creating, kept hidden instead of deleted";
        }
        show_failed_notice(false);
        create_browser(why);
    });
}

bool WebViewPanel::is_current_browser(const wxEvent &evt) const
{
    return m_browser != nullptr && evt.GetId() == m_browser->GetId();
}

void WebViewPanel::mark_browser_alive(const char *how)
{
    if (m_browser_alive)
        return;
    m_browser_alive = true;
    m_auto_rebuilds = 0;
    m_home_watchdog.Stop();
    show_failed_notice(false);
    // 「內嵌網頁建好 → 活起來」的秒數：之後客戶回報首頁空白，看 log 就知道是不是這個病。
    BOOST_LOG_TRIVIAL(info) << "WebViewPanel: page alive (" << how << ") " << ms_since(m_browser_created_at)
                            << " ms after the browser was created";
}

void WebViewPanel::OnBrowserBackendReady(wxCommandEvent &evt)
{
    if (is_current_browser(evt))
        BOOST_LOG_TRIVIAL(info) << "WebViewPanel: browser backend ready " << ms_since(m_browser_created_at)
                                << " ms after the browser was created";
}

void WebViewPanel::OnBrowserProcessFailed(wxCommandEvent &evt)
{
    if (!is_current_browser(evt)) // 已經換掉的舊內嵌網頁
        return;
    const bool page_dead = evt.GetExtraLong() != 0;
    BOOST_LOG_TRIVIAL(warning) << "WebViewPanel: WebView2 process failed, kind " << evt.GetInt() << (page_dead ? " (page dead)" : " (recoverable)")
                               << ", page " << (m_browser_alive ? "in use" : "not alive yet");
    if (!page_dead)
        return;
    if (m_browser_alive)
        show_failed_notice(true); // D2 A：使用中失效不自動重建（會丟掉使用者正在做的東西），讓使用者自己按
    else
        rebuild_browser("process failed before the page came alive", true);
}

void WebViewPanel::OnHomeWatchdog(wxTimerEvent &WXUNUSED(evt))
{
    if (m_browser_alive || m_browser == nullptr)
        return;
    // 主視窗縮到最小時頁面可能暫停，不算失效；等它還原再看。
    if (auto *mainframe = wxGetApp().mainframe; mainframe != nullptr && mainframe->IsIconized()) {
        m_home_watchdog.StartOnce(HOME_WATCHDOG_MS);
        return;
    }
    rebuild_browser("watchdog: page not alive within 60 s", true);
}

/* PING #34（D2）：一行提示「⚠ 頁面已失效」＋「重新載入」。平常不存在；第一次要用時才建，放在內嵌網頁上方。
   顏色＝ping-cis 警示訊息（system-desktop：底 color.warning.bg #FFF6DF、字 color.warning.fg #946310；
   深色模式照面板底、字用 color.warning.fg.on-dark #FF9A52）；按鈕沿用 App 既有 Button 一般樣式。
   不擋操作（ping-ux FBK-07），頁面活起來就收掉（FBK-50）。 */
void WebViewPanel::show_failed_notice(bool show)
{
    if (show && m_failed_notice == nullptr) {
        const bool dark = wxGetApp().dark_mode();
        m_failed_notice = new wxPanel(this);
        if (!dark)
            m_failed_notice->SetBackgroundColour(wxColour("#FFF6DF"));
        auto *text = new wxStaticText(m_failed_notice, wxID_ANY, wxString::FromUTF8("\xE2\x9A\xA0 ") + _L("The page has stopped working."));
        text->SetForegroundColour(wxColour(dark ? "#FF9A52" : "#946310"));
        text->SetFont(Label::Body_14);
        auto *reload = new Button(m_failed_notice, _L("Reload"));
        reload->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
        reload->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { rebuild_browser("reload button", false); });
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
        row->Add(reload, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(8));
        m_failed_notice->SetSizer(row);
        m_failed_notice->Hide();
        // 放在內嵌網頁上方（內嵌網頁永遠是 sizer 的最後一項，見 create_browser）。
        wxSizer *sizer = GetSizer();
        const size_t at = (m_browser != nullptr && sizer->GetItem(m_browser) != nullptr) ? sizer->GetItemCount() - 1 : sizer->GetItemCount();
        sizer->Insert(at, m_failed_notice, wxSizerFlags().Expand());
    }
    if (m_failed_notice == nullptr || m_failed_notice->IsShown() == show)
        return;
    BOOST_LOG_TRIVIAL(info) << "WebViewPanel: reload notice " << (show ? "shown" : "hidden");
    m_failed_notice->Show(show);
    Layout();
}

void WebViewPanel::ShowHomepage()
{
    m_pending_photo_tile_image.clear();
    wxString url = homepage_url();
    load_url(url);
}

void WebViewPanel::ShowPhotoTile(const wxString& image_path)
{
    m_pending_photo_tile_image = image_path;
    wxString url = photo_tile_url();
    /* PING(2026-09-09，Eric「不要去清除它」)：頁面已經停在工作室、又沒有新圖 ⇒ **不重載**——
       圖／尺寸／色階／圈數原地保留（回列印板時頁面只是被藏起來）。只補送一次機型料數與 AI 可用性
       （使用者可能在列印板換了機器；頁面的 setMachineCapability 料數沒變時只重畫款式清單，不動使用者設定）。
       有新圖、或頁面在別處（首頁／STEP 修補）才真的載入。 */
    // ⚠ 不能拿字串相等比：WebView2 回報的 URL 是正規化過的（file:///D:/… 三斜線、正斜線），photo_tile_url() 組出來的是
    //   file://D:\…（反斜線）——第一版這樣比永遠不等 ⇒ 每次都重載（21:35 實測）。改用與 OnNavigationRequest 同法的 Contains。
    if (image_path.IsEmpty() && m_browser != nullptr &&
        m_browser->GetCurrentURL().Contains("/web/phototile/index.html")) {
        m_pending_photo_tile_image.clear();
        SendPhotoTileMachineCapability();
        SendPhotoTileAiAvailability();
        BOOST_LOG_TRIVIAL(info) << "PhotoTile: studio page kept alive (no reload)";
        return;
    }
    load_url(url);
}

void WebViewPanel::ShowStepRepair()
{
    m_pending_photo_tile_image.clear();
    wxString url = step_repair_url();
    load_url(url);
}

bool WebViewPanel::IsStepRepairPage() const
{
    /* 【2026-08-23・Codex 反審修正】必須是**精確來源比對**，不能用子字串。
       原本 Contains("/web/step-repair/index.html") 在 file:// 時代還說得過去，
       改走 https 虛擬主機之後就不行了——任何 URL 只要路徑含這串就會被當成修補頁，
       而這個判斷正是「可以叫 C++ 寫檔」的信任邊界。比對時去掉 query，
       只認 scheme+host+path 與 step_repair_url() 完全相等。 */
    if (m_browser == nullptr)
        return false;
    const wxString current  = m_browser->GetCurrentURL().BeforeFirst('?');
    const wxString expected = step_repair_url().BeforeFirst('?');
    return !expected.IsEmpty() && current.IsSameAs(expected, false);
}

/* 【2026-08-15・Eric 裁 A 案；判準已於 2026-09-07 被 #99 Q3 甲取代，見下】
   把「照片磚機有沒有這個模式」告訴工作室頁面，讓四色按鈕在做不到四料時擋得住。
   頁面端先擋，使用者才不會白做一輪（匯出時才擋＝做完之後才擋）。
   fail-open：這支沒被呼叫（舊頁面）⇒ 頁面 machineCap 維持 null ⇒ 不擋、行為照舊。

   🔴 **2026-09-08 修回歸**：原本這裡跟 resolver 一樣用 `is_visible`（＝使用者已加入）。
   #99 Q3 甲把照片磚機從「選擇 3D 列印機」整批移除、改成匯出時才按需安裝之後，
   **進工作室當下一台都還沒安裝** ⇒ `hasQuad` 恆為 false ⇒ **四色永遠鎖著**。
   實錄：Eric 2026-09-08 選了 FF600 同進進工作室，四色是鎖的；查他的 conf，
   已安裝機型只有 6 台、照片磚機 0 台——舊版是「進門整組自動裝」才讓它看起來一直是解鎖的。
   ⇒ 判準改成 `is_system`，與 `ping_resolve_photo_tile_printer()` 同步
   （那支已在 0907 改掉，**這一處是同一個假設的第二個落點、當時漏掉**）。
   語意也跟著正確了：這個閘門要回答的是「這台機做不做得到四色」，
   不是「使用者有沒有把它加進清單」——後者在新設計裡已經不是使用者的事。 */
void WebViewPanel::SendPhotoTileMachineCapability()
{
    bool has_dual = false, has_quad = false;
    if (PresetBundle* bundle = wxGetApp().preset_bundle) {
        for (const Preset& preset : bundle->printers) {
            if (!preset.is_system)          // 見上：不再要求「已加入」，照片磚機是按需安裝的
                continue;
            const PhotoTileCapability cap = photo_tile_capability_of(preset);
            if (!cap.is_photo_tile)
                continue;
            if (cap.mode == "dual")
                has_dual = true;
            else if (cap.mode == "quad")
                has_quad = true;
        }
    }
    /* 2026-09-08（Eric：「點四料的同進進到工作室時，應該只讓我選擇四色；雙料的同進只讓我選雙料，或不需要讓我選擇，
       下方的模板也跟著改變」→ 裁甲案＝不讓選）：多送「進工作室當下選中的機器」的模式與型號，
       頁面照它定料數、把雙料／四色切換鈕換成固定標籤、款式只列該料數。
       非同進機（mode 空）送 null ⇒ 頁面維持切換鈕（fail-open，與 hasDual／hasQuad 同一精神；舊頁面也不受影響）。
       判定走 photo_tile_capability_of_selected_printer()（與匯出時 ping_resolve_photo_tile_printer 同一支），不自己比字串。 */
    const PhotoTileCapability cur = photo_tile_capability_of_selected_printer();
    auto json_str = [](const std::string& v) {
        std::string out = "\"";
        for (char ch : v) {
            if (ch == '\n' || ch == '\r') continue;
            if (ch == '"' || ch == '\\') out += '\\';
            out += ch;
        }
        return out + "\"";
    };
    const std::string cur_mode_json  = cur.mode.empty() ? std::string("null") : json_str(cur.mode);
    const std::string cur_model_json = cur.mode.empty() ? std::string("null")
                                     : json_str(cur.printer_model.empty() ? cur.preset_name : cur.printer_model);
    /* 2026-09-08（Eric：「四料照片磚的口徑可否依照機器選擇的口徑自動變化？」）：一併送進工作室當下機器的噴嘴。
       為什麼要跟：匯出時 ping_resolve_photo_tile_printer() 用「printer_variant == 頁面口徑」挑照片磚機，
       頁面口徑沒跟機器走 ⇒ 0.6 的機會被切成 0.4 的照片磚機。nozzle_mm 讀 nozzle_diameter[0]（非同進機也有值）。 */
    std::string cur_nozzle_json = "null";
    if (cur.nozzle_mm > 0.0) { std::ostringstream os; os << cur.nozzle_mm; cur_nozzle_json = os.str(); }
    /* 段 B（車 2，牌 c-0923-ACC-21）：機上裝了哪幾支線材（專案的線材順序＝頁面的料槽順序：料 N＝擠出機 N）。
       🆕 T061（牌 c-0924-ACC-27）：材料身分改成「料種＋顏色名」（R6-14 子題 1 附款）之後，頁面只拿它的 color
       當校正片的**名目色**（新的料還沒量過時排 S 清單方向用），不再拿它當身分、也不再有「認領」框。
       🔴 只是**清單**：頁面不拿它去猜「校正片的哪一端是哪一支」——那要人指定（猜錯＝整筆資料掛錯身分，而且沒人會發現）。
       舊頁面不認得這個欄位＝照舊運作（additive）。 */
    std::string filaments_json = "[";
    if (PresetBundle* bundle = wxGetApp().preset_bundle) {
        const ConfigOptionStrings* colours = bundle->project_config.option<ConfigOptionStrings>("filament_colour");
        for (size_t i = 0; i < bundle->filament_presets.size(); ++i) {
            const std::string& name   = bundle->filament_presets[i];
            const Preset*      preset = bundle->filaments.find_preset(name, false);
            const std::string  colour = (colours != nullptr && i < colours->values.size()) ? colours->values[i] : std::string();
            if (i > 0)
                filaments_json += ",";
            filaments_json += "{\"id\":" + json_str(preset != nullptr ? preset->filament_id : std::string())
                            + ",\"name\":" + json_str(name) + ",\"color\":" + json_str(colour) + "}";
        }
    }
    filaments_json += "]";
    /* T061（牌 c-0924-ACC-27；R6-14 子題 1 附款、發包單〈九〉Q1 甲）：照片磚印得出來的**料種**清單。
       頁面「指定材料」的料種下拉只列這一份（今天只有 PLA；將來有照片磚用的 PETG 參數會自動出現）。
       存料種、不綁某一支線材參數——同一捲料在 FD 與 FF 照片磚機上的 filament_id 不同（〈九〉9-3）。
       判準＝系統線材裡**明寫了相容條件**、而且條件對得上任一台照片磚機的那幾支，取 filament_type。
       🔴 只認「明寫」：Orca 內建線材庫那一批完全沒寫相容條件（＝對所有機器都相容），算進來會把 ABS／PETG／TPU…
          整批列進去——那不是替照片磚寫的參數，照片磚也沒有替它們調過（0924 實數：PING 34 支裡只有 3 支照片磚 PLA 會中）。
       舊頁面不認得這個欄位＝照舊運作（additive）；頁面沒收到（瀏覽器直開）時自己退回只列 PLA。 */
    std::string types_json = "[";
    if (PresetBundle* bundle = wxGetApp().preset_bundle) {
        std::vector<const Preset*> tile_printers;
        for (const Preset& p : bundle->printers)
            if (p.is_system && photo_tile_capability_of(p).is_photo_tile)
                tile_printers.push_back(&p);
        std::vector<std::string> types;
        for (const Preset& f : bundle->filaments) {
            if (!f.is_system || f.is_default)
                continue;
            const ConfigOptionStrings* list = f.config.option<ConfigOptionStrings>("compatible_printers");
            if ((list == nullptr || list->values.empty()) && f.compatible_printers_condition().empty())
                continue;                          // 沒寫相容條件＝對所有機器都相容，不是替照片磚寫的（見上）
            const ConfigOptionStrings* ft = f.config.option<ConfigOptionStrings>("filament_type");
            if (ft == nullptr || ft->values.empty() || ft->values.front().empty())
                continue;
            const std::string& type = ft->values.front();
            if (std::find(types.begin(), types.end(), type) != types.end())
                continue;
            for (const Preset* p : tile_printers)
                if (is_compatible_with_printer(PresetWithVendorProfile(f, f.vendor), PresetWithVendorProfile(*p, p->vendor))) {
                    types.push_back(type);
                    break;
                }
        }
        std::sort(types.begin(), types.end());
        for (size_t i = 0; i < types.size(); ++i)
            types_json += (i > 0 ? "," : "") + json_str(types[i]);
    }
    types_json += "]";
    const std::string json = std::string("{\"hasDual\":") + (has_dual ? "true" : "false")
                           + ",\"hasQuad\":" + (has_quad ? "true" : "false")
                           + ",\"current\":" + cur_mode_json
                           + ",\"currentModel\":" + cur_model_json
                           + ",\"currentNozzle\":" + cur_nozzle_json
                           + ",\"filaments\":" + filaments_json
                           + ",\"materialTypes\":" + types_json + "}";
    BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：bundle 內可用的照片磚模式 " << json;
    RunScript(wxString("window.PINGPhotoTile && window.PINGPhotoTile.setMachineCapability(")
              + from_u8(json) + ");");
}

/* 丙案（P3「丑」）：告訴頁面「這台機器現在生不生得了圖」＝有沒有存金鑰。
   🔴 過去的只有一個 bool，**明文一個位元組都不過去**（Eric 2026-08-16 裁 C 案；
      閘門 C3 也會擋：`resources/web/` 出現任何金鑰識別字就變紅）。
   ⚠ 與 machineCap 的 fail-open 不同，這裡維持 fail-closed：舊版宿主不呼叫 ⇒ 頁面
      的 ptAiReady() 維持 false ⇒ AI 款式鎖著。功能真的不存在時，標得出來比裝得出來重要。 */
void WebViewPanel::SendPhotoTileAiAvailability()
{
    const bool ready = PingAiImage::available();
    BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：AI 生圖可用＝" << (ready ? "是" : "否");
    RunScript(wxString("window.PINGPhotoTile && window.PINGPhotoTile.setAiAvailable(")
              + (ready ? "true" : "false") + ");");
}

void WebViewPanel::SendPendingPhotoTileImage()
{
    const wxString image_path = m_pending_photo_tile_image;
    m_pending_photo_tile_image.clear();
    if (image_path.IsEmpty())
        return;

    boost::nowide::ifstream input(into_u8(image_path), std::ios::binary | std::ios::ate);
    if (!input) {
        BOOST_LOG_TRIVIAL(warning) << "Unable to open dropped photo tile image: " << into_u8(image_path);
        RunScript("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片無法讀取，請按「開啟圖片」重試。');");
        return;
    }

    const std::streamoff file_size = input.tellg();
    constexpr std::streamoff max_image_bytes = 64LL * 1024LL * 1024LL;
    if (file_size <= 0 || file_size > max_image_bytes) {
        BOOST_LOG_TRIVIAL(warning) << "Dropped photo tile image has unsupported size: " << file_size;
        RunScript("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片檔案過大（上限 64 MB），請先縮小圖片再試。');");
        return;
    }

    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<size_t>(file_size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), file_size)) {
        BOOST_LOG_TRIVIAL(warning) << "Unable to read dropped photo tile image: " << into_u8(image_path);
        RunScript("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片讀取失敗，請按「開啟圖片」重試。');");
        return;
    }

    const wxString extension = wxFileName(image_path).GetExt().Lower();
    wxString mime = "image/png";
    if (extension == "jpg" || extension == "jpeg") mime = "image/jpeg";
    else if (extension == "webp") mime = "image/webp";
    else if (extension == "bmp") mime = "image/bmp";
    else if (extension == "gif") mime = "image/gif";     // 🟡覆審：三份 mime 表同步
    else if (extension == "avif") mime = "image/avif";   //（GUI_App 落檔表／宿主讀檔表）

    pt::ptree metadata;
    metadata.put("name", into_u8(wxFileName(image_path).GetFullName()));
    metadata.put("mime", into_u8(mime));
    metadata.put("size", bytes.size());
    std::ostringstream json_stream;
    pt::write_json(json_stream, metadata, false);
    std::string metadata_json = json_stream.str();
    metadata_json.erase(std::remove(metadata_json.begin(), metadata_json.end(), '\n'), metadata_json.end());
    RunScript(wxString("window.PINGPhotoTile && window.PINGPhotoTile.beginImage(") + from_u8(metadata_json) + ");");

    const wxString encoded = wxBase64Encode(bytes.data(), bytes.size());
    constexpr size_t chunk_chars = 192 * 1024;
    for (size_t offset = 0; offset < encoded.length(); offset += chunk_chars) {
        const wxString chunk = encoded.Mid(offset, chunk_chars);
        RunScript(wxString("window.PINGPhotoTile && window.PINGPhotoTile.appendImageChunk(\"") + chunk + "\");");
    }
    RunScript("window.PINGPhotoTile && window.PINGPhotoTile.finishImage();");
}

/**
    * Method that retrieves the current state from the web control and updates the GUI
    * the reflect this current state.
    */
void WebViewPanel::UpdateState()
{
    if (m_browser == nullptr)
        return;
#if !BBL_RELEASE_TO_PUBLIC
    if (m_browser->CanGoBack()) {
        m_button_back->Enable(true);
    }
    else {
        m_button_back->Enable(false);
    }
    if (m_browser->CanGoForward()) {
        m_button_forward->Enable(true);
    }
    else {
        m_button_forward->Enable(false);
    }
    if (m_browser->IsBusy())
    {
        m_button_stop->Enable(true);
    }
    else
    {
        m_button_stop->Enable(false);
    }

    //SetTitle(m_browser->GetCurrentTitle());
    m_url->SetValue(m_browser->GetCurrentURL());
#endif //BBL_RELEASE_TO_PUBLIC
}

void WebViewPanel::OnIdle(wxIdleEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
#if !BBL_RELEASE_TO_PUBLIC
    if (m_browser->IsBusy())
    {
        wxSetCursor(wxCURSOR_ARROWWAIT);
        m_button_stop->Enable(true);
    }
    else
    {
        wxSetCursor(wxNullCursor);
        m_button_stop->Enable(false);
    }
#endif //BBL_RELEASE_TO_PUBLIC
}

/**
    * Callback invoked when user entered an URL and pressed enter
    */
void WebViewPanel::OnUrl(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(m_url->GetValue());
    m_browser->SetFocus();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "back" button
    */
void WebViewPanel::OnBack(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    m_browser->GoBack();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "forward" button
    */
void WebViewPanel::OnForward(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    m_browser->GoForward();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "stop" button
    */
void WebViewPanel::OnStop(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    m_browser->Stop();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "reload" button
    */
void WebViewPanel::OnReload(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    m_browser->Reload();
    UpdateState();
}

void WebViewPanel::OnCut(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->Cut();
}

void WebViewPanel::OnCopy(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->Copy();
}

void WebViewPanel::OnPaste(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->Paste();
}

void WebViewPanel::OnUndo(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->Undo();
}

void WebViewPanel::OnRedo(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->Redo();
}

void WebViewPanel::OnMode(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->SetEditable(m_edit_mode->IsChecked());
}

void WebViewPanel::OnLoadScheme(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    wxPathList pathlist;
    pathlist.Add(".");
    pathlist.Add("..");
    pathlist.Add("../help");
    pathlist.Add("../../../samples/help");

    wxFileName helpfile(pathlist.FindValidPath("doc.zip"));
    helpfile.MakeAbsolute();
    wxString path = helpfile.GetFullPath();
    //Under MSW we need to flip the slashes
    path.Replace("\\", "/");
    path = "wxfs:///" + path + ";protocol=zip/doc.htm";
    m_browser->LoadURL(path);
}

void WebViewPanel::OnUseMemoryFS(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->LoadURL("memory:page1.htm");
}

void WebViewPanel::OnEnableContextMenu(wxCommandEvent& evt)
{
    if (m_browser)
        m_browser->EnableContextMenu(evt.IsChecked());
}

void WebViewPanel::OnEnableDevTools(wxCommandEvent& evt)
{
    if (m_browser)
        m_browser->EnableAccessToDevTools(evt.IsChecked());
}

void WebViewPanel::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void WebViewPanel::OnFreshLoginStatus(wxTimerEvent &event)
{
    auto mainframe = Slic3r::GUI::wxGetApp().mainframe;
    if (mainframe && mainframe->m_webview == this)
        Slic3r::GUI::wxGetApp().get_login_info();
}

void WebViewPanel::SetLoginPanelVisibility(bool bshow)
{
    wxString strJS = wxString::Format("SetLoginPanelVisibility(%s)", bshow ? "true" : "false");
    RunScript(strJS);
}
void WebViewPanel::SendRecentList(int images)
{
    boost::property_tree::wptree req;
    boost::property_tree::wptree data;
    wxGetApp().mainframe->get_recent_projects(data, images);
    req.put(L"sequence_id", "");
    req.put(L"command", L"get_recent_projects");
    req.put_child(L"response", data);
    std::wostringstream oss;
    pt::write_json(oss, req, false);
    RunScript(wxString::Format("window.postMessage(%s)", oss.str()));
}

void WebViewPanel::SendDesignStaffpick(bool on)
{
    // if (on) {
    //     get_design_staffpick(0, 60, [this](std::string body) {
    //         if (body.empty() || body.front() != '{') {
    //             BOOST_LOG_TRIVIAL(warning) << "get_design_staffpick failed " + body;
    //             return;
    //         }
    //         CallAfter([this, body] {
    //             auto body2 = from_u8(body);
    //             body2.insert(1, "\"command\": \"modelmall_model_advise_get\", ");
    //             RunScript(wxString::Format("window.postMessage(%s)", body2));
    //         });
    //     });
    // } else {
    //     std::string body2 = "{\"total\":0, \"hits\":[]}";
    //     body2.insert(1, "\"command\": \"modelmall_model_advise_get\", ");
    //     RunScript(wxString::Format("window.postMessage(%s)", body2));
    // }
}

void WebViewPanel::OpenModelDetail(std::string id, NetworkAgent *agent)
{
    std::string url;
    if ((agent ? agent->get_model_mall_detail_url(&url, id) : get_model_mall_detail_url(&url, id)) == 0) 
    {
        if (url.find("?") != std::string::npos) 
        { 
            url += "&from=orcaslicer";
        } else {
            url += "?from=orcaslicer";
        }
        
        wxLaunchDefaultBrowser(url); 
    }
}

void WebViewPanel::SendLoginInfo()
{
    if (wxGetApp().getAgent()) {
        std::string login_info = wxGetApp().getAgent()->build_login_info();
        wxString strJS = wxString::Format("window.postMessage(%s)", login_info);
        RunScript(strJS);
    }
}

void WebViewPanel::ShowNetpluginTip()
{
    // Install Network Plugin
    const auto bblnetwork_enabled =wxGetApp().app_config->get_bool("installed_networking");
    if(!bblnetwork_enabled) {
        return;
    }
    bool        bValid       = wxGetApp().is_compatibility_version();

    int nShow = 0;
    if (!bValid) nShow = 1;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": bValid=%1%, nShow=%2%")%bValid %nShow;

    json m_Res           = json::object();
    m_Res["command"]     = "network_plugin_installtip";
    m_Res["sequence_id"] = "10001";
    m_Res["show"]        = nShow;

    wxString strJS = wxString::Format("window.postMessage(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

    RunScript(strJS);
}

void WebViewPanel::get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback)
{
    // auto host = wxGetApp().get_http_url(wxGetApp().app_config->get_country_code(), "v1/design-service/design/staffpick");
    // std::string url = (boost::format("%1%/?offset=%2%&limit=%3%") % host % offset % limit).str();

    // Http http = Http::get(url);
    // http.header("accept", "application/json")
    //     .header("Content-Type", "application/json")
    //     .on_complete([this, callback](std::string body, unsigned status) { callback(body); })
    //     .on_error([this, callback](std::string body, std::string error, unsigned status) {
    //         callback(body);
    //     })
    //     .perform();
}

int WebViewPanel::get_model_mall_detail_url(std::string *url, std::string id)
{
    // https://makerhub-qa.bambu-lab.com/en/models/2077
    std::string h = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());
    auto l = wxGetApp().current_language_code_safe();
    if (auto n = l.find('_'); n != std::string::npos)
        l = l.substr(0, n);
    *url = (boost::format("%1%%2%/models/%3%") % h % l % id).str();
    return 0;
}

void WebViewPanel::update_mode()
{
    GetSizer()->Show(size_t(0), wxGetApp().app_config->get("internal_developer_mode") == "true");
    GetSizer()->Layout();
}

/**
    * Callback invoked when there is a request to load a new page (for instance
    * when the user clicks a link)
    */
void WebViewPanel::OnNavigationRequest(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    if (!is_current_browser(evt)) {
        // PING #34：重建時沒刪掉的舊內嵌網頁（後端還沒建好的那種，見 rebuild_browser）不准載任何頁。
        evt.Veto();
        return;
    }
    const wxString &url = evt.GetURL();
    if (url.StartsWith("File://") || url.StartsWith("file://")) {
        if (!url.Contains("/web/homepage/index.html") &&
            !url.Contains("/web/phototile/index.html") &&
            !url.Contains("/web/step-repair/index.html")) {
            auto file = wxURL::Unescape(wxURL(url).GetPath());
#ifdef _WIN32
            if (file.StartsWith('/'))
                file = file.Mid(1);
            else
                file = "//" + file; // When file from network location
#endif
            wxGetApp().plater()->load_files(wxArrayString{1, &file});
            evt.Veto();
            return;
        }
    }

    if (m_info->IsShown())
    {
        m_info->Dismiss();
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "' (target='" +
            evt.GetTarget() + "')");

    //If we don't want to handle navigation then veto the event and navigation
    //will not take place, we also need to stop the loading animation
    if (!m_tools_handle_navigation->IsChecked())
    {
        evt.Veto();
        if (m_button_stop)
            m_button_stop->Enable(false);
    }
    else
    {
        UpdateState();
    }
}

/**
    * Callback invoked when a navigation request was accepted
    */
void WebViewPanel::OnNavigationComplete(wxWebViewEvent& evt)
{
    if (!is_current_browser(evt))
        return;
    // PING #34：導覽成功也算活著——STEP 破面檢查頁載入時不會主動送訊息。
    mark_browser_alive("navigation completed");
    m_browser->Show();
    Layout();
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
    UpdateState();
    ShowNetpluginTip();
}

/**
    * Callback invoked when a page is finished loading
    */
void WebViewPanel::OnDocumentLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    if (!is_current_browser(evt))
        return;
    // Only notify if the document is the main frame, not a subframe
    if (evt.GetURL() == m_browser->GetCurrentURL())
    {
        if (wxGetApp().get_mode() == comDevelop)
            wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
    if (evt.GetURL().Contains("/web/phototile/index.html")) {
        SendPhotoTileMachineCapability();   // 先告知料數，再送圖
        SendPhotoTileAiAvailability();      // 丙案：再告知 AI 生圖可不可用
        SendPendingPhotoTileImage();
    }
    UpdateState();
}

void WebViewPanel::OnTitleChanged(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // wxGetApp().CallAfter([this] { SendRecentList(); });
}

/**
    * On new window, we veto to stop extra windows appearing
    */
void WebViewPanel::OnNewWindow(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER)
    {
        flag = " (user)";
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    //If we handle new window events then just load them in this window as we
    //are a single window browser
    if (m_tools_handle_new_window->IsChecked() && is_current_browser(evt))
        m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void WebViewPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    if (!is_current_browser(evt))
        return;
    mark_browser_alive("first script message"); // PING #34
    // update login status
    if (m_LoginUpdateTimer == nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Create Timer";
        m_LoginUpdateTimer = new wxTimer(this, LOGIN_INFO_UPDATE_TIMER_ID);
        m_LoginUpdateTimer->Start(2000);
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());
    std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
    if (response.empty()) return;

    /* remove \n in response string */
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    if (!response.empty()) {
        m_response_js = wxString::Format("window.postMessage('%s')", response);
        wxCommandEvent* event = new wxCommandEvent(EVT_RESPONSE_MESSAGE, this->GetId());
        wxQueueEvent(this, event);
    }
    else {
        m_response_js.clear();
    }
}

void WebViewPanel::OnScriptResponseMessage(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_response_js.empty()) {
        RunScript(m_response_js);
    }
}

/**
    * Invoked when user selects the "View Source" menu item
    */
void WebViewPanel::OnViewSourceRequest(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    SourceViewDialog dlg(this, m_browser->GetPageSource());
    dlg.ShowModal();
}

/**
    * Invoked when user selects the "View Text" menu item
    */
void WebViewPanel::OnViewTextRequest(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr)
        return;
    wxDialog textViewDialog(this, wxID_ANY, "Page Text",
        wxDefaultPosition, wxSize(700, 500),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, m_browser->GetPageText(),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE |
        wxTE_RICH |
        wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
    textViewDialog.ShowModal();
}

/**
    * Invoked when user selects the "Menu" item
    */
void WebViewPanel::OnToolsClicked(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser == nullptr || m_browser->GetCurrentURL() == "")
        return;

    m_edit_cut->Enable(m_browser->CanCut());
    m_edit_copy->Enable(m_browser->CanCopy());
    m_edit_paste->Enable(m_browser->CanPaste());

    m_edit_undo->Enable(m_browser->CanUndo());
    m_edit_redo->Enable(m_browser->CanRedo());

    m_selection_clear->Enable(m_browser->HasSelection());
    m_selection_delete->Enable(m_browser->HasSelection());

    m_context_menu->Check(m_browser->IsContextMenuEnabled());
    m_dev_tools->Check(m_browser->IsAccessToDevToolsEnabled());

    wxPoint position = ScreenToClient(wxGetMousePosition());
    PopupMenu(m_tools_menu, position.x, position.y);
}

void WebViewPanel::RunScript(const wxString& javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}

void WebViewPanel::OnRunScriptString(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("setCount(345);");
}

void WebViewPanel::OnRunScriptInteger(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(123);");
}

void WebViewPanel::OnRunScriptDouble(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(2.34);");
}

void WebViewPanel::OnRunScriptBool(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(false);");
}

void WebViewPanel::OnRunScriptObject(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
}

void WebViewPanel::OnRunScriptArray(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
}

void WebViewPanel::OnRunScriptDOM(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("document.write(\"Hello World!\");");
}

void WebViewPanel::OnRunScriptUndefined(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var person = new Object();}f();");
}

void WebViewPanel::OnRunScriptNull(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){return null;}f();");
}

void WebViewPanel::OnRunScriptDate(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; \
    return new Date(d.getTime() - tzoffset);}f();");
}

void WebViewPanel::OnRunScriptMessage(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("window.wx.postMessage('This is a web message');");
}

void WebViewPanel::OnRunScriptCustom(wxCommandEvent& WXUNUSED(evt))
{
    wxTextEntryDialog dialog
    (
        this,
        "Please enter JavaScript code to execute",
        wxGetTextFromUserPromptStr,
        m_javascript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    RunScript(dialog.GetValue());
}

void WebViewPanel::OnAddUserScript(wxCommandEvent& WXUNUSED(evt))
{
    wxString userScript = "window.wx_test_var = 'wxWidgets webview sample';";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the JavaScript code to run as the initialization script that runs before any script in the HTML document.",
        wxGetTextFromUserPromptStr,
        userScript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK || m_browser == nullptr)
        return;

    if (!m_browser->AddUserScript(dialog.GetValue()))
        wxLogError("Could not add user script");
}

void WebViewPanel::OnSetCustomUserAgent(wxCommandEvent& WXUNUSED(evt))
{
    wxString customUserAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 13_1_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/13.0.1 Mobile/15E148 Safari/604.1";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the custom user agent string you would like to use.",
        wxGetTextFromUserPromptStr,
        customUserAgent,
        wxOK | wxCANCEL | wxCENTRE
    );
    if (dialog.ShowModal() != wxID_OK || m_browser == nullptr)
        return;

    if (!m_browser->SetUserAgent(customUserAgent))
        wxLogError("Could not set custom user agent");
}

void WebViewPanel::OnClearSelection(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->ClearSelection();
}

void WebViewPanel::OnDeleteSelection(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->DeleteSelection();
}

void WebViewPanel::OnSelectAll(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser)
        m_browser->SelectAll();
}

/**
    * Callback invoked when a loading error occurs
    */
void WebViewPanel::OnError(wxWebViewEvent& evt)
{
    if (!is_current_browser(evt))
        return;
#define WX_ERROR_CASE(type) \
    case type: \
    category = #type; \
    break;

    wxString category;
    switch (evt.GetInt())
    {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": [" << category << "] " << evt.GetString().ToUTF8().data();

    if (wxGetApp().get_mode() == comDevelop) 
    {
        wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" + category + " (" + evt.GetString() + ")'");

        // Show the info bar with an error
        m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() + "\n" + "'" + category + "'", wxICON_ERROR);
    }

    UpdateState();
}


SourceViewDialog::SourceViewDialog(wxWindow* parent, wxString source) :
                  wxDialog(parent, wxID_ANY, "Source Code",
                           wxDefaultPosition, wxSize(700,500),
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, source,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE |
                                      wxTE_RICH |
                                      wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
}


} // GUI
} // Slic3r
