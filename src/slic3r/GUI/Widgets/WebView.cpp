#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/MacDarkMode.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/log/trivial.hpp>

#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#if wxUSE_WEBVIEW_EDGE
#include <wx/msw/webview_edge.h>
#elif defined(__WXMAC__)
#include <wx/osx/webview_webkit.h>
#endif
#include <wx/uri.h>
#include <wx/weakref.h>
#if defined(__WIN32__) || defined(__WXMAC__)
#include "wx/private/jsscriptwrapper.h"
#endif

#ifdef __WIN32__
#include <WebView2.h>
#include <wrl.h>
#include <wrl/event.h>
#include <Shellapi.h>
#include <slic3r/Utils/Http.hpp>
#elif defined __linux__
#include <gtk/gtk.h>
#define WEBKIT_API
struct WebKitWebView;
struct WebKitJavascriptResult;
extern "C" {
WEBKIT_API void
webkit_web_view_run_javascript                       (WebKitWebView             *web_view,
                                                      const gchar               *script,
                                                      GCancellable              *cancellable,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data);
WEBKIT_API WebKitJavascriptResult *
webkit_web_view_run_javascript_finish                (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
						      GError                    **error);
WEBKIT_API void
webkit_javascript_result_unref              (WebKitJavascriptResult *js_result);
}
#endif

#ifdef __WIN32__
// Run Download and Install in another thread so we don't block the UI thread
DWORD DownloadAndInstallWV2RT() {

  int returnCode = 2; // Download failed
  // Use fwlink to download WebView2 Bootstrapper at runtime and invoke installation
  // Broken/Invalid Https Certificate will fail to download
  // Use of the download link below is governed by the below terms. You may acquire the link
  // for your use at https://developer.microsoft.com/microsoft-edge/webview2/. Microsoft owns
  // all legal right, title, and interest in and to the WebView2 Runtime Bootstrapper
  // ("Software") and related documentation, including any intellectual property in the
  // Software. You must acquire all code, including any code obtained from a Microsoft URL,
  // under a separate license directly from Microsoft, including a Microsoft download site
  // (e.g., https://developer.microsoft.com/microsoft-edge/webview2/).
  // HRESULT hr = URLDownloadToFileW(NULL, L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
  //                               L".\\plugin\\MicrosoftEdgeWebview2Setup.exe", 0, 0);
  fs::path target_file_path = (fs::temp_directory_path() / "MicrosoftEdgeWebview2Setup.exe");
  bool downloaded = false;
  Slic3r::Http::get("https://go.microsoft.com/fwlink/p/?LinkId=2124703")
      .on_error([](std::string body, std::string error, unsigned http_status) {

      })
      .on_complete([&downloaded, target_file_path](std::string body, unsigned http_status) {
        fs::fstream file(target_file_path, std::ios::out | std::ios::binary | std::ios::trunc);
        file.write(body.c_str(), body.size());
        file.flush();
        file.close();

        downloaded = true;
      })
      .perform_sync();
  // Sleep for 1 second to wait for the buffer writen into disk
  std::this_thread::sleep_for(1000ms);
  if (downloaded) {
    // Either Package the WebView2 Bootstrapper with your app or download it using fwlink
    // Then invoke install at Runtime.
    SHELLEXECUTEINFOW shExInfo = {0};
    shExInfo.cbSize = sizeof(shExInfo);
    shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shExInfo.hwnd = 0;
    shExInfo.lpVerb = L"runas";
    shExInfo.lpFile = target_file_path.generic_wstring().c_str();
    shExInfo.lpParameters = L" /install";
    shExInfo.lpDirectory = 0;
    shExInfo.nShow = 0;
    shExInfo.hInstApp = 0;

    if (ShellExecuteExW(&shExInfo)) {
      WaitForSingleObject(shExInfo.hProcess, INFINITE);
      returnCode = 0; // Install successfull
    } else {
      returnCode = 1; // Install failed
    }
  }
  return returnCode;
}

class WebViewEdge : public wxWebViewEdge
{
public:
    ~WebViewEdge() override
    {
        // PING #34：拆掉自己註冊的 ProcessFailed（wx 的 impl 解構時只拆它自己註冊的那幾個）。
        if (m_processFailedToken.value != 0)
            if (auto webView2 = (ICoreWebView2 *) GetNativeBackend())
                webView2->remove_ProcessFailed(m_processFailedToken);
        if (m_permissionToken.value != 0)
            if (auto webView2 = (ICoreWebView2 *) GetNativeBackend())
                webView2->remove_PermissionRequested(m_permissionToken);
    }

    bool SetUserAgent(const wxString &userAgent)
    {
        bool dark = userAgent.Contains("dark");
        SetColorScheme(dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);

        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            // PING #34（Codex 一輪）：原本成功時沒放掉 settings；get_Settings 失敗時對未初始化的指標 Release——
            // 後端失效後切深色模式（RecreateAll）就會走到那條路。
            bool                   ok       = false;
            ICoreWebView2Settings *settings = nullptr;
            if (webView2->get_Settings(&settings) == S_OK && settings) {
                ICoreWebView2Settings2 *settings2 = nullptr;
                if (settings->QueryInterface(&settings2) == S_OK && settings2) {
                    settings2->put_UserAgent(userAgent.wc_str());
                    settings2->Release();
                    ok = true;
                }
                settings->Release();
            }
            return ok;
        }
        pendingUserAgent = userAgent;
        return true;
    }

    /* 【2026-08-23・Eric 裁「甲」】把 resources 目錄映成虛擬主機。
       為什麼需要：STEP 修補工具的 Emscripten 核心是用 XHR 抓 10.8 MB 的 .wasm，
       而 file:// origin 的 XHR 會被瀏覽器核心以 CORS 擋死（實錄：net::ERR_FAILED
       → failed to asynchronously prepare wasm → Aborted）。1.0.0／1.0.2 A/B 同樣壞
       ⇒ 不是版本問題，是載入方式與 file:// 不相容。
       映到 resources **根**（不是單一子夾），URL 仍長成 /web/xxx/index.html，
       既有的 IsStepRepairPage() 與導航白名單字串比對一字不用改。
       暴露面（2026-08-23 Codex 反審修正）：用 **DENY_CORS** 不是 ALLOW——
       ALLOW 會讓「載在同一個 WebView 裡的其他來源頁面」也能跨來源讀這批資源，
       那才是真的擴大可讀範圍；DENY_CORS 只放行虛擬主機自己那個來源，
       正好夠這張頁面用 XHR 抓自己的 wasm。 */
    bool SetVirtualHostMapping(const wxString &host, const wxString &folder)
    {
        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            ICoreWebView2_3 *webView2_3;
            HRESULT          hr = webView2->QueryInterface(&webView2_3);
            if (hr == S_OK) {
                hr = webView2_3->SetVirtualHostNameToFolderMapping(
                    host.wc_str(), folder.wc_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                webView2_3->Release();
                return hr == S_OK;
            }
            return false;
        }
        pendingVirtualHost   = host;
        pendingVirtualFolder = folder;
        return true;
    }

    bool SetColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME colorScheme)
    {
        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            // PING #34（Codex 一輪）：原本成功時沒放掉 webView2_13。
            bool              ok          = false;
            ICoreWebView2_13 *webView2_13 = nullptr;
            if (webView2->QueryInterface(&webView2_13) == S_OK && webView2_13) {
                ICoreWebView2Profile *profile = nullptr;
                if (webView2_13->get_Profile(&profile) == S_OK && profile) {
                    profile->put_PreferredColorScheme(colorScheme);
                    profile->Release();
                    ok = true;
                }
                webView2_13->Release();
            }
            return ok;
        }
        pendingColorScheme = colorScheme;
        return true;
    }

    void DoGetClientSize(int *x, int *y) const override
    {
        if (!pendingUserAgent.empty()) {
            auto thiz = const_cast<WebViewEdge *>(this);
            auto userAgent = std::move(thiz->pendingUserAgent);
            thiz->pendingUserAgent.clear();
            thiz->SetUserAgent(userAgent);
        }
        if (!pendingVirtualHost.empty()) {
            auto thiz   = const_cast<WebViewEdge *>(this);
            auto host   = std::move(thiz->pendingVirtualHost);
            auto folder = std::move(thiz->pendingVirtualFolder);
            thiz->pendingVirtualHost.clear();
            thiz->pendingVirtualFolder.clear();
            thiz->SetVirtualHostMapping(host, folder);
        }
        if (pendingColorScheme) {
            auto thiz      = const_cast<WebViewEdge *>(this);
            auto colorScheme = pendingColorScheme;
            thiz->pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
            thiz->SetColorScheme(colorScheme);
        }
        if (!m_backendSeen)
            if (auto webView2 = (ICoreWebView2 *) GetNativeBackend())
                const_cast<WebViewEdge *>(this)->OnBackendAvailable(webView2);
        wxWebViewEdge::DoGetClientSize(x, y);
    };
private:
    /* PING #34：這版 wx 沒有「後端建好了」的事件，也沒註冊 ProcessFailed。後端第一次出現的地方＝wx 的
       「控制器建好」回呼裡、導覽之前的 UpdateBounds()（它會走到上面的 DoGetClientSize）。
       在回呼裡只做兩件輕的事：註冊 ProcessFailed、排一個「後端就緒」事件；怎麼處理都交給面板在回呼外做。 */
    void OnBackendAvailable(ICoreWebView2 *webView2)
    {
        m_backendSeen = true;
        wxWeakRef<wxWindow> self(this);
        HRESULT             hr = webView2->add_ProcessFailed(
            Microsoft::WRL::Callback<ICoreWebView2ProcessFailedEventHandler>(
                [self](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
                    try { // 例外不可穿越 COM 回呼（SOP_WebView2 §一）
                        COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                        if (args)
                            args->get_ProcessFailedKind(&kind);
                        // 只有瀏覽器程序或主頁面程序結束才算頁面死了；GPU／子框架／未回應等 Chromium 會自己恢復（SOP_WebView2 §四）。
                        const bool page_dead = kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                                               kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED;
                        if (wxWindow *w = self.get()) {
                            auto *evt = new wxCommandEvent(EVT_WEBVIEW_PROCESS_FAILED, w->GetId());
                            evt->SetEventObject(w);
                            evt->SetInt(int(kind));
                            evt->SetExtraLong(page_dead ? 1 : 0);
                            wxQueueEvent(w, evt);
                        }
                    } catch (...) {}
                    return S_OK;
                }).Get(),
            &m_processFailedToken);
        if (FAILED(hr))
            BOOST_LOG_TRIVIAL(warning) << "WebView: add_ProcessFailed failed, hr=" << hr;
        /* AIP 第二班（開發中清單 #22）：照片磚工作室的〔貼上圖片〕用 navigator.clipboard.read() 讀剪貼簿裡的圖；
           WebView2 預設會跳詢問或拒絕 ⇒ 只放行「照片磚那一頁」的讀剪貼簿，其他頁、其他權限一律照預設（不碰）。
           這裡掛在每個內嵌網頁上（首頁、登入、外部印表機頁…都走這裡）⇒ 一定要用網址過濾。
           Ctrl+V（網頁的 paste 事件）不需要這個權限——這條不通時頁面會請使用者直接按 Ctrl+V。
           判斷看「這個 WebView 現在開的頁」（get_Source），不看 args 的 Uri：Uri 是發出請求的來源（origin），
           file:// 頁的 origin 只剩 "file:///"，永遠比不上照片磚頁的完整路徑（2026-10-06 真 App 驗收：一次都沒放行）。
           Uri 只用來擋外部網頁（http／https）——照片磚頁裡只有本機檔。 */
        hr = webView2->add_PermissionRequested(
            Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2 *sender, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                    try { // 例外不可穿越 COM 回呼（SOP_WebView2 §一）
                        COREWEBVIEW2_PERMISSION_KIND kind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
                        if (sender == nullptr || args == nullptr || FAILED(args->get_PermissionKind(&kind)) ||
                            kind != COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ)
                            return S_OK;
                        LPWSTR source = nullptr, uri = nullptr;
                        if (FAILED(sender->get_Source(&source)) || source == nullptr)
                            return S_OK;
                        const wxString page(source);
                        CoTaskMemFree(source);
                        wxString origin;
                        if (SUCCEEDED(args->get_Uri(&uri)) && uri != nullptr) {
                            origin = uri;
                            CoTaskMemFree(uri);
                        }
                        if (WebView::IsPhotoTileUrl(page) && !origin.Lower().StartsWith("http")) {
                            args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
                            BOOST_LOG_TRIVIAL(info) << "PhotoTile: clipboard read allowed for the photo-tile page (origin=" << origin.ToUTF8().data() << ")";
                        } else
                            BOOST_LOG_TRIVIAL(info) << "WebView: clipboard read left to the default, origin=" << origin.ToUTF8().data()
                                                    << ", page=" << page.ToUTF8().data();
                    } catch (...) {}
                    return S_OK;
                }).Get(),
            &m_permissionToken);
        if (FAILED(hr))
            BOOST_LOG_TRIVIAL(warning) << "WebView: add_PermissionRequested failed, hr=" << hr;
        auto *evt = new wxCommandEvent(EVT_WEBVIEW_BACKEND_READY, GetId());
        evt->SetEventObject(this);
        wxQueueEvent(this, evt);
    }

    wxString pendingUserAgent;
    wxString pendingVirtualHost;
    wxString pendingVirtualFolder;
    COREWEBVIEW2_PREFERRED_COLOR_SCHEME pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
    bool                   m_backendSeen = false;
    EventRegistrationToken m_processFailedToken{};
    EventRegistrationToken m_permissionToken{};
};

#elif defined __WXOSX__

class WebViewWebKit : public wxWebViewWebKit
{
    ~WebViewWebKit() override
    {
        RemoveScriptMessageHandler("wx");
    }
};

#endif

class FakeWebView : public wxWebView
{
    virtual bool Create(wxWindow* parent, wxWindowID id, const wxString& url, const wxPoint& pos, const wxSize& size, long style, const wxString& name) override { return false; }
    virtual wxString GetCurrentTitle() const override { return wxString(); }
    virtual wxString GetCurrentURL() const override { return wxString(); }
    virtual bool IsBusy() const override { return false; }
    virtual bool IsEditable() const override { return false; }
    virtual void LoadURL(const wxString& url) override { }
    virtual void Print() override { }
    virtual void RegisterHandler(wxSharedPtr<wxWebViewHandler> handler) override { }
    virtual void Reload(wxWebViewReloadFlags flags = wxWEBVIEW_RELOAD_DEFAULT) override { }
    virtual bool RunScript(const wxString& javascript, wxString* output = NULL) const override { return false; }
    virtual void SetEditable(bool enable = true) override { }
    virtual void Stop() override { }
    virtual bool CanGoBack() const override { return false; }
    virtual bool CanGoForward() const override { return false; }
    virtual void GoBack() override { }
    virtual void GoForward() override { }
    virtual void ClearHistory() override { }
    virtual void EnableHistory(bool enable = true) override { }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetBackwardHistory() override { return {}; }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetForwardHistory() override { return {}; }
    virtual void LoadHistoryItem(wxSharedPtr<wxWebViewHistoryItem> item) override { }
    virtual bool CanSetZoomType(wxWebViewZoomType type) const override { return false; }
    virtual float GetZoomFactor() const override { return 0.0f; }
    virtual wxWebViewZoomType GetZoomType() const override { return wxWebViewZoomType(); }
    virtual void SetZoomFactor(float zoom) override { }
    virtual void SetZoomType(wxWebViewZoomType zoomType) override { }
    virtual bool CanUndo() const override { return false; }
    virtual bool CanRedo() const override { return false; }
    virtual void Undo() override { }
    virtual void Redo() override { }
    virtual void* GetNativeBackend() const override { return nullptr; }
    virtual void DoSetPage(const wxString& html, const wxString& baseUrl) override { }
};

wxDEFINE_EVENT(EVT_WEBVIEW_RECREATED, wxCommandEvent);
wxDEFINE_EVENT(EVT_WEBVIEW_BACKEND_READY, wxCommandEvent);
wxDEFINE_EVENT(EVT_WEBVIEW_PROCESS_FAILED, wxCommandEvent);

static std::vector<wxWebView*> g_webviews;
static std::vector<wxWebView*> g_delay_webviews;

/* PING #34（L1）的建立閘門（說明見 WebView.hpp）。只有 Windows（WebView2）有這個病，
   其他平台一開始就放行＝照舊在建構時建立。 */
#ifdef __WIN32__
static bool g_creation_allowed = false;
#else
static bool g_creation_allowed = true;
#endif
static std::vector<std::pair<wxWeakRef<wxWindow>, std::function<void()>>> g_creation_waiting;

bool WebView::CreationAllowed() { return g_creation_allowed; }

void WebView::AllowCreation()
{
    if (g_creation_allowed)
        return;
    g_creation_allowed = true;
    auto waiting       = std::move(g_creation_waiting);
    g_creation_waiting.clear();
    BOOST_LOG_TRIVIAL(info) << "WebView: creation allowed after post_init, " << waiting.size() << " waiting";
    for (auto &w : waiting)
        if (w.first.get() != nullptr)
            w.second();
}

void WebView::CallWhenCreationAllowed(wxWindow *owner, std::function<void()> fn)
{
    if (g_creation_allowed)
        fn();
    else
        g_creation_waiting.emplace_back(wxWeakRef<wxWindow>(owner), std::move(fn));
}

class WebViewRef : public wxObjectRefData
{
public:
    WebViewRef(wxWebView *webView) : m_webView(webView) {}
    ~WebViewRef() {
        auto iter = std::find(g_webviews.begin(), g_webviews.end(), m_webView);
        assert(iter != g_webviews.end());
        if (iter != g_webviews.end())
            g_webviews.erase(iter);
    }
    wxWebView *m_webView;
};

wxWebView* WebView::CreateWebView(wxWindow * parent, wxString const & url)
{
#if wxUSE_WEBVIEW_EDGE
    // Check if a fixed version of edge is present in
    // $executable_path/edge_fixed and use it
    wxFileName edgeFixedDir(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir.SetFullName("");
    edgeFixedDir.AppendDir("edge_fixed");
    if (edgeFixedDir.DirExists()) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }
#endif
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << url2.ToUTF8();

#ifdef __WIN32__
    WebViewEdge* webViewEdge = new WebViewEdge;
    /* 【2026-08-23】resources → 虛擬主機（理由見 SetVirtualHostMapping 註解）。
       此時原生後端通常還沒建好，走 pending、由 DoGetClientSize 勾子補套用。 */
    webViewEdge->SetVirtualHostMapping(WebView::virtual_host(),
                                       wxString::FromUTF8(Slic3r::resources_dir().c_str()));
    wxWebView* webView = webViewEdge;
#elif defined(__WXOSX__)
    wxWebView *webView = new WebViewWebKit;
#else
    auto webView = wxWebView::New();
#endif
    if (webView) {
        webView->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

        wxString language_code = Slic3r::GUI::wxGetApp().current_language_code().BeforeFirst('_');
        language_code          = language_code.ToStdString();
#ifdef __WIN32__
        webView->SetUserAgent(wxString::Format("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                               "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/107.0.0.0 Safari/537.36 Edg/107.0.1418.52 BBL-Slicer/v%s (%s) BBL-Language/%s",
                                               SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light", language_code.mb_str()));
        // PING #34（Codex 一輪）：原本不看回傳值；失敗（沒有執行階段／環境建立呼叫失敗）至少要留紀錄。
        if (!webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE))
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": wxWebViewEdge::Create failed";
        // We register the wxfs:// protocol for testing purposes
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
        // With WKWebView handlers need to be registered before creation.
        // On Linux (WebKit2GTK), URI schemes are registered globally and can only
        // be registered once, so guard against multiple registrations.
        static bool s_schemes_registered = false;
        if (!s_schemes_registered) {
            webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
            webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
            s_schemes_registered = true;
        }
        webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        webView->SetUserAgent(wxString::Format("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) BBL-Slicer/v%s (%s) BBL-Language/%s",
                                               SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light", language_code.mb_str()));
#endif
#ifdef __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_setTransparentBackground(wkWebView);
#endif
        auto addScriptMessageHandler = [] (wxWebView *webView) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": begin to add script message handler for wx.";
            Slic3r::GUI::wxGetApp().set_adding_script_handler(true);
            if (!webView->AddScriptMessageHandler("wx"))
                wxLogError("Could not add script message handler");
            Slic3r::GUI::wxGetApp().set_adding_script_handler(false);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": finished add script message handler for wx.";
        };
#ifndef __WIN32__
        webView->CallAfter([webView, addScriptMessageHandler] {
#endif
            if (Slic3r::GUI::wxGetApp().is_adding_script_handler()) {
                g_delay_webviews.push_back(webView);
            } else {
                addScriptMessageHandler(webView);
                while (!g_delay_webviews.empty()) {
                    auto views = std::move(g_delay_webviews);
                    for (auto wv : views)
                        addScriptMessageHandler(wv);
                }
            }
#ifndef __WIN32__
        });
#endif
        webView->EnableContextMenu(true);
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": failed. Use fake web view.";
        webView = new FakeWebView;
    }
    webView->SetRefData(new WebViewRef(webView));
    g_webviews.push_back(webView);
    return webView;
}
#if wxUSE_WEBVIEW_EDGE
bool WebView::CheckWebViewRuntime()
{
    wxWebViewFactoryEdge factory;
    auto wxVersion = factory.GetVersionInfo();
    return wxVersion.GetMajor() != 0;
}

bool WebView::DownloadAndInstallWebViewRuntime()
{
    return DownloadAndInstallWV2RT() == 0;
}
#endif
void WebView::LoadUrl(wxWebView * webView, wxString const &url)
{
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << url2.ToUTF8();
    webView->LoadURL(url2);
}

bool WebView::IsPhotoTileUrl(wxString const &url)
{
    // 去掉 query／fragment、解開 %xx（WebView2 會把空白與中文編碼）、反斜線換正斜線、去掉開頭的斜線（file:// 與 file:/// 都有人報）
    auto strip = [](wxString s) {
        s.Replace("\\", "/");
        while (s.StartsWith("/"))
            s.Remove(0, 1);
        return s;
    };
    const wxString page = wxURI::Unescape(url.BeforeFirst('#').BeforeFirst('?'));
    if (!page.Lower().StartsWith("file:"))
        return false;
    if (Slic3r::resources_dir().empty())
        return false;
    const wxString expected = strip(wxString::FromUTF8(Slic3r::resources_dir()) + "/web/phototile/index.html");
    return strip(page.Mid(5)).IsSameAs(expected, false);   // 不分大小寫：Windows 路徑
}

bool WebView::RunScript(wxWebView *webView, wxString const &javascript)
{
    if (Slic3r::GUI::wxGetApp().app_config->get("internal_developer_mode") == "true"
            && javascript.find("studio_userlogin") == wxString::npos)
        wxLogMessage("Running JavaScript:\n%s\n", javascript);

    try {
#ifdef __WIN32__
        ICoreWebView2 *   webView2 = (ICoreWebView2 *) webView->GetNativeBackend();
        if (webView2 == nullptr)
            return false;
        return webView2->ExecuteScript(javascript, NULL) == 0;
#elif defined __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_evaluateJavaScript(wkWebView, javascript, nullptr);
        return true;
#else
        WebKitWebView *wkWebView = (WebKitWebView *) webView->GetNativeBackend();
        webkit_web_view_run_javascript(
            wkWebView, javascript.utf8_str(), NULL,
            [](GObject *wkWebView, GAsyncResult *res, void *) {
                GError * error = NULL;
                auto result = webkit_web_view_run_javascript_finish((WebKitWebView*)wkWebView, res, &error);
                if (!result)
                    g_error_free (error);
                else
                    webkit_javascript_result_unref (result);
        }, NULL);
        return true;
#endif
    } catch (std::exception &) {
        return false;
    }
}

void WebView::RecreateAll()
{
    auto dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxString language_code = Slic3r::GUI::wxGetApp().current_language_code().BeforeFirst('_');
    language_code          = language_code.ToStdString();
    for (auto webView : g_webviews) {
        webView->SetUserAgent(wxString::Format("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) BBL-Slicer/v%s (%s) BBL-Language/%s",
                                               SLIC3R_VERSION, dark ? "dark" : "light", language_code.mb_str()));
        webView->Reload();
    }
}
