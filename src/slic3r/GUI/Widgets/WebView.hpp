#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/webview.h>

#include <functional>

// PING #34：內嵌網頁的後端（ICoreWebView2）建好了／WebView2 程序失效。
// 由內嵌網頁本身排入佇列（事件 ID＝那個 wxWebView 的 ID），往上傳給所在的面板；
// PROCESS_FAILED 的 GetInt()＝COREWEBVIEW2_PROCESS_FAILED_KIND，GetExtraLong()＝1 表示頁面已經死了
// （瀏覽器程序或頁面程序結束），0＝Chromium 會自己恢復的那幾種。只有 Windows 會送。
wxDECLARE_EVENT(EVT_WEBVIEW_BACKEND_READY, wxCommandEvent);
wxDECLARE_EVENT(EVT_WEBVIEW_PROCESS_FAILED, wxCommandEvent);

class WebView
{
public:
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url);

    /* PING #34（L1）：開機初始化（post_init）做完之前不建內嵌網頁。
       WebView2 建好內嵌頁之後，主程式大約一分鐘內沒處理它的回呼，三個內嵌頁會一起被瀏覽器收掉，
       而 wx 與 PING 都沒有重建機制 ⇒ 首頁永遠白（實驗：間隔 ≤49 秒 2/2 正常、≥60 秒 7/7 空白）。
       所以開機時只登記「要建」，等 GUI_App 在 post_init 之後呼叫 AllowCreation() 才真的建。
       owner 被刪掉的登記會自動略過。已經放行之後登記的，立刻執行。 */
    static bool CreationAllowed();
    static void AllowCreation();
    static void CallWhenCreationAllowed(wxWindow *owner, std::function<void()> fn);
#if wxUSE_WEBVIEW_EDGE
    static bool CheckWebViewRuntime();
    static bool DownloadAndInstallWebViewRuntime();
#endif
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    static void RecreateAll();

    /* 【2026-08-23】resources 目錄的虛擬主機名（單一真實來源；WebViewDialog 組 URL 時共用）。
       `.invalid` 是 RFC 2606 保留字尾，保證永遠不會解析到真的網路主機。 */
    static wxString virtual_host() { return "ping-resources.invalid"; }
};

#endif // !slic3r_GUI_WebView_hpp_
