#ifndef slic3r_WebViewDialog_hpp_
#define slic3r_WebViewDialog_hpp_


#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include <wx/webview.h>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>

#include <chrono>


namespace Slic3r {

class NetworkAgent;

namespace GUI {


class WebViewPanel : public wxPanel
{
public:
    WebViewPanel(wxWindow *parent);
    virtual ~WebViewPanel();

    void load_url(wxString& url);
    void ShowHomepage();
    void ShowPhotoTile(const wxString& image_path = wxEmptyString);
    void ShowStepRepair();
    bool IsStepRepairPage() const;

    void UpdateState();
    void OnIdle(wxIdleEvent& evt);
    void OnUrl(wxCommandEvent& evt);
    void OnBack(wxCommandEvent& evt);
    void OnForward(wxCommandEvent& evt);
    void OnStop(wxCommandEvent& evt);
    void OnReload(wxCommandEvent& evt);
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent &evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnScriptResponseMessage(wxCommandEvent& evt);
    void OnViewSourceRequest(wxCommandEvent& evt);
    void OnViewTextRequest(wxCommandEvent& evt);
    void OnToolsClicked(wxCommandEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnCut(wxCommandEvent& evt);
    void OnCopy(wxCommandEvent& evt);
    void OnPaste(wxCommandEvent& evt);
    void OnUndo(wxCommandEvent& evt);
    void OnRedo(wxCommandEvent& evt);
    void OnMode(wxCommandEvent& evt);
    void RunScript(const wxString& javascript);
    void OnRunScriptString(wxCommandEvent& evt);
    void OnRunScriptInteger(wxCommandEvent& evt);
    void OnRunScriptDouble(wxCommandEvent& evt);
    void OnRunScriptBool(wxCommandEvent& evt);
    void OnRunScriptObject(wxCommandEvent& evt);
    void OnRunScriptArray(wxCommandEvent& evt);
    void OnRunScriptDOM(wxCommandEvent& evt);
    void OnRunScriptUndefined(wxCommandEvent& evt);
    void OnRunScriptNull(wxCommandEvent& evt);
    void OnRunScriptDate(wxCommandEvent& evt);
    void OnRunScriptMessage(wxCommandEvent& evt);
    void OnRunScriptCustom(wxCommandEvent& evt);
    void OnAddUserScript(wxCommandEvent& evt);
    void OnSetCustomUserAgent(wxCommandEvent& evt);
    void OnClearSelection(wxCommandEvent& evt);
    void OnDeleteSelection(wxCommandEvent& evt);
    void OnSelectAll(wxCommandEvent& evt);
    void OnLoadScheme(wxCommandEvent& evt);
    void OnUseMemoryFS(wxCommandEvent& evt);
    void OnEnableContextMenu(wxCommandEvent& evt);
    void OnEnableDevTools(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    wxTimer * m_LoginUpdateTimer{nullptr};
    void OnFreshLoginStatus(wxTimerEvent &event);

public:
    void SendRecentList(int images);
    void SetLoginPanelVisibility(bool bshow);
    void SendDesignStaffpick(bool on);
    void OpenModelDetail(std::string id, NetworkAgent *agent);
    void SendLoginInfo();
    void ShowNetpluginTip();

    void get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback);
    int  get_model_mall_detail_url(std::string *url, std::string id);

    void update_mode();
private:
    /* PING #34：首頁內嵌網頁的建立／看門狗／重建（修正計畫 L1＋L2，Eric 2026-09-29 裁 D1 A／D2 A／D3 60 秒）。
       面板照常在開機時建；裡面的 WebView2 延到開機初始化做完才建（WebView::AllowCreation）。
       還沒建好之前，load_url() 只記下要去的頁，RunScript() 直接略過（首頁載好後自己會再要資料）。 */
    void create_browser(const char *why);
    void rebuild_browser(const char *why, bool automatic);
    bool is_current_browser(const wxEvent &evt) const;
    void mark_browser_alive(const char *how);
    void OnBrowserBackendReady(wxCommandEvent &evt);
    void OnBrowserProcessFailed(wxCommandEvent &evt);
    void OnHomeWatchdog(wxTimerEvent &evt);
    void show_failed_notice(bool show);

    wxString   m_target_url;                // 內嵌網頁應該顯示的頁（建立／重建時載入這一頁）
    wxTimer    m_home_watchdog;             // 從建立起算 60 秒頁面都沒活起來 ⇒ 重建
    std::chrono::steady_clock::time_point m_browser_created_at{};
    bool       m_browser_alive{false};      // 頁面送過訊息或導覽成功過（＝活著、使用者可能正在用）
    int        m_auto_rebuilds{0};          // 自動重建次數（上限 2）
    bool       m_rebuild_pending{false};
    bool       m_closing{false};
    wxPanel   *m_failed_notice{nullptr};    // D2：使用中失效時的一行提示＋重新載入

    wxWebView* m_browser{nullptr};
    wxBoxSizer *bSizer_toolbar;
    // These controls are not created in public Release builds. Keep them
    // explicitly null so shared navigation helpers can safely run there.
    wxButton *  m_button_back    {nullptr};
    wxButton *  m_button_forward {nullptr};
    wxButton *  m_button_stop    {nullptr};
    wxButton *  m_button_reload  {nullptr};
    wxTextCtrl *m_url            {nullptr};
    wxButton *  m_button_tools   {nullptr};

    wxMenu* m_tools_menu;
    wxMenuItem* m_tools_handle_navigation;
    wxMenuItem* m_tools_handle_new_window;
    wxMenuItem* m_edit_cut;
    wxMenuItem* m_edit_copy;
    wxMenuItem* m_edit_paste;
    wxMenuItem* m_edit_undo;
    wxMenuItem* m_edit_redo;
    wxMenuItem* m_edit_mode;
    wxMenuItem* m_scroll_line_up;
    wxMenuItem* m_scroll_line_down;
    wxMenuItem* m_scroll_page_up;
    wxMenuItem* m_scroll_page_down;
    wxMenuItem* m_script_string;
    wxMenuItem* m_script_integer;
    wxMenuItem* m_script_double;
    wxMenuItem* m_script_bool;
    wxMenuItem* m_script_object;
    wxMenuItem* m_script_array;
    wxMenuItem* m_script_dom;
    wxMenuItem* m_script_undefined;
    wxMenuItem* m_script_null;
    wxMenuItem* m_script_date;
    wxMenuItem* m_script_message;
    wxMenuItem* m_script_custom;
    wxMenuItem* m_selection_clear;
    wxMenuItem* m_selection_delete;
    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;

    wxInfoBar *m_info;
    wxStaticText* m_info_text;

    long m_zoomFactor;

    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;
    wxString m_pending_photo_tile_image;

    void SendPendingPhotoTileImage();

public:
    /* 2026-08-15：頁面主動要料數時由 GUI_App 呼叫（跨類別）⇒ 必須 public。
       其餘照片磚回推走的是類別內部路徑，維持 private。 */
    void SendPhotoTileMachineCapability();
    void SendPhotoTileAiAvailability();          // 丙案：只過一個 bool，明文不進網頁層

private:
    DECLARE_EVENT_TABLE()
};

class SourceViewDialog : public wxDialog
{
public:
    SourceViewDialog(wxWindow* parent, wxString source);
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
