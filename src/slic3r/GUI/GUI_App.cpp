#include "libslic3r/Technologies.hpp"
#include "GUI_App.hpp"
#include "GUI_Init.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "slic3r/GUI/UserManager.hpp"
#include "slic3r/GUI/TaskManager.hpp"
#include "format.hpp"
#include "libslic3r_version.h"
#include "Downloader.hpp"

// Localization headers: include libslic3r version first so everything in this file
// uses the slic3r/GUI version (the macros will take precedence over the functions).
// Also, there is a check that the former is not included from slic3r module.
// This is the only place where we want to allow that, so define an override macro.
#define SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "libslic3r/I18N.hpp"
#undef SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <iterator>
#include <set>          // ping_install_photo_tile_printers()：機型去重（不賭傳遞包含）
#include <exception>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <thread>
#include <string_view>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/file.h>
#include <wx/datetime.h>
#include <wx/progdlg.h>
#include <wx/busyinfo.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/richmsgdlg.h>
#include <wx/log.h>
#include <wx/intl.h>

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/splash.h>
#include <wx/graphics.h>   // PING: splash per-pixel 合成
#include "SplashLayered.hpp" // PING: splash 去背(layered window)
#include <wx/fontutil.h>
#include <wx/glcanvas.h>
#include <wx/utils.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Color.hpp"

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "3DScene.hpp"
#include "MainFrame.hpp"
#include "PartPlate.hpp"                       // PING c-0909-TH-01：列印板縮圖
#include "libslic3r/Format/bbs_3mf.hpp"      // PING c-0909-TH-01：bbs_3mf_add_plate_thumbnail
#include "Plater.hpp"
#include <wx/timer.h>               // C-2 第 3 項：閒置預熱 timer（原本靠傳遞式引入，改成顯式）
#include "PhotoTileCapability.hpp"
#include "PhotoTileEngineHost.hpp"   // C-2：工作室生成改走隱形宿主（unique_ptr 成員的完整型別也在這）
#include "libslic3r/PingPhotoStylize.hpp"   // 甲案（c-0822-PT-06）：本地風格化（OpenCV，已是本 repo 相依）
#include "slic3r/Utils/PingAiImage.hpp"     // 丙案（P3「丑」）：AI 生圖。⚠ 金鑰明文只在那個檔裡，本檔不碰
#include "PhotoTileGateJson.hpp"     // C-2 第 2 項：env 快照組字（#14 真 JSON writer——中文機名/反斜線路徑都要正確跳脫）

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "GLCanvas3D.hpp"
#include "EncodedFilament.hpp"
#include "GeneratedConfig.hpp"

#include "DeviceCore/DevManager.h"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Process.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/Http.hpp"
#include "../Utils/InstanceID.hpp"
#include "../Utils/UndoRedo.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "SysInfoDialog.hpp"
#include "UpdateDialogs.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "NotificationManager.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "NetworkPluginDialog.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "SendSystemInfoDialog.hpp"
#include "ParamsDialog.hpp"
#include "KBShortcutsDialog.hpp"
#include "DownloadProgressDialog.hpp"

#include "BitmapCache.hpp"
#include "Notebook.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressDialog.hpp"

//BBS: DailyTip and UserGuide Dialog
#include "WebDownPluginDlg.hpp"
#include "WebGuideDialog.hpp"
#include "ReleaseNote.hpp"
#include "PrivacyUpdateDialog.hpp"
#include "ModelMall.hpp"
#include "HintNotification.hpp"

#include "slic3r/Utils/NetworkAgentFactory.hpp"
#include "slic3r/Utils/BBLNetworkPlugin.hpp"
#include "slic3r/Utils/bambu_networking.hpp"

//#ifdef WIN32
//#include "BaseException.h"
//#endif


#ifdef __WXMSW__
#include <dbt.h>
#include <shlobj.h>

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#include "wx/headerctrl.h"
#include "wx/msw/headerctrl.h"

typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS2)(
    HANDLE hProcess,
    USHORT *pProcessMachine,
    USHORT *pNativeMachine
);

#endif // _MSW_DARK_MODE
#endif // __WINDOWS__

#endif
#ifdef _WIN32
#include <boost/dll/runtime_symbol_info.hpp>
#endif

#ifdef WIN32
#include "dev-utils/BaseException.h"
#endif

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
#include <boost/beast/core/detail/base64.hpp>
#include <boost/nowide/fstream.hpp>
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

// Needed for forcing menu icons back under gtk2 and gtk3
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <gtk/gtk.h>
#endif

using namespace std::literals;
namespace pt = boost::property_tree;

struct StaticBambuLib
{
    static void reset();
    static void release();
};

namespace Slic3r {
namespace GUI {

class MainFrame;

/* 【2026-08-24・牌 c-0824-VIBE-02】STEP 修補核心的版本釘住，改由 vendor manifest 決定。

   為什麼要改：原本這裡把期望版本硬編成字串（`core_version != "1.0.2"`）。
   那表示 VibeCAD 每出一個 patch 版，ORCA 都得改 C++ 並重新編譯一次整包才吃得到——
   0823 那棒就是栽在這裡：升到 1.0.2 之後，被釘死的 "1.0.0" 把另存整個 fail-closed 擋掉。
   VibeCAD 已於 2026-08-24 正式同意改讀 manifest（契約第三題），本函式即契約 §3 的六步驗證。

   🔴 fail closed：讀不到、解析不了、欄位缺失、版本不符、依賴旗標不對 —— 一律不放行匯出，
      而且**不自動降版**去遷就頁面回報的版本。 */
namespace {

struct StepRepairPin
{
    bool        valid = false;
    std::string core_version;
    int         protocol_version = 0;
};

const StepRepairPin& step_repair_pin()
{
    namespace bfs = boost::filesystem;
    // 固定 release 在執行期不會變，讀一次就好；讀失敗也要記住失敗（不要每次重試出不同結果）。
    static const StepRepairPin pin = [&]() -> StepRepairPin {
        StepRepairPin   out;
        const bfs::path vendor_dir    = bfs::path(resources_dir()) / "web" / "step-repair";
        const bfs::path manifest_path = vendor_dir / "vendor-manifest.json";
        try {
            boost::nowide::ifstream ifs(manifest_path.string());
            if (!ifs.good()) {
                BOOST_LOG_TRIVIAL(error) << "STEP repair vendor manifest not readable: " << manifest_path.string();
                return out;
            }
            json j;
            ifs >> j;

            // ①～③ 契約 §3：只讀自己 vendor 目錄的 manifest，並確認 schema／component／兩個依賴旗標。
            if (j.value("schemaVersion", 0) != 1 ||
                j.value("component", std::string()) != "@linkin-factory/step-repair-core" ||
                j.value("runtimeDependencyOnSourceCheckout", true) != false ||
                j.value("buildDependencyOnSourceCheckout", true) != false) {
                BOOST_LOG_TRIVIAL(error) << "STEP repair vendor manifest failed contract checks";
                return out;
            }

            // ⑤ integrityManifest 必須指向同一個 vendor 目錄（不得指到工作樹或別處）。
            const std::string integrity = j.value("integrityManifest", std::string());
            if (integrity.empty() || integrity.find('/') != std::string::npos ||
                integrity.find('\\') != std::string::npos ||
                !bfs::exists(vendor_dir / integrity)) {
                BOOST_LOG_TRIVIAL(error) << "STEP repair integrityManifest missing or outside the vendor dir: " << integrity;
                return out;
            }

            out.core_version     = j.value("coreVersion", std::string());
            out.protocol_version = j.value("protocolVersion", 0);
            if (out.core_version.empty() || out.protocol_version <= 0) {
                BOOST_LOG_TRIVIAL(error) << "STEP repair vendor manifest has no usable version fields";
                return out;
            }
            out.valid = true;
            BOOST_LOG_TRIVIAL(info) << "STEP repair pinned by manifest: core=" << out.core_version
                                    << ", protocol=" << out.protocol_version;
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "STEP repair vendor manifest unreadable: " << e.what();
            return StepRepairPin{};
        }
        return out;
    }();
    return pin;
}

} // anonymous namespace

void start_ping_test()
{
    return;
    wxArrayString output;
    wxExecute("ping www.amazon.com", output, wxEXEC_NODISABLE);

    wxString output_i;
    std::string output_temp;

    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping amazon:" << output_temp;

    }
    wxExecute("ping www.apple.com", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping www.apple.com:" << output_temp;
    }
    wxExecute("ping www.bambulab.com", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping bambulab:" << output_temp;
    }
    //Get GateWay IP
    wxExecute("ping 192.168.0.1", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping 192.168.0.1:" << output_temp;
    }
}

std::string VersionInfo::convert_full_version(std::string short_version)
{
    std::string result = "";
    std::vector<std::string> items;
    boost::split(items, short_version, boost::is_any_of("."));
    if (items.size() == VERSION_LEN) {
        for (int i = 0; i < VERSION_LEN; i++) {
            std::stringstream ss;
            ss << std::setw(2) << std::setfill('0') << items[i];
            result += ss.str();
            if (i != VERSION_LEN - 1)
                result += ".";
        }
        return result;
    }
    return result;
}

std::string VersionInfo::convert_short_version(std::string full_version)
{
    full_version.erase(std::remove(full_version.begin(), full_version.end(), '0'), full_version.end());
    return full_version;
}

static std::string convert_studio_language_to_api(std::string lang_code)
{
    boost::replace_all(lang_code, "_", "-");
    return lang_code;

    /*if (lang_code == "zh_CN")
        return "zh-hans";
    else if (lang_code == "zh_TW")
        return "zh-hant";
    else
        return "en";*/
}

#ifdef _WIN32
bool is_associate_files(std::wstring extend)
{
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_id             = L" Orca.Slicer.1";
    std::wstring reg_base            = L"Software\\Classes";
    std::wstring reg_extension       = reg_base + L"\\." + extend;

    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(HKEY_CURRENT_USER, reg_extension.c_str(), nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if (!bDidntExist && ::wcscmp(szValueCurrent, prog_id.c_str()) == 0)
        return true;

    return false;
}
#endif

class SplashScreen : public wxSplashScreen
{
public:
    SplashScreen(const wxBitmap& bitmap, long splashStyle, int milliseconds, wxPoint pos = wxDefaultPosition)
        : wxSplashScreen(bitmap, splashStyle, milliseconds, static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, wxDefaultPosition, wxDefaultSize,
#ifdef __APPLE__
            wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#else
            wxBORDER_NONE | wxFRAME_NO_TASKBAR
#endif // !__APPLE__
        )
    {
        int init_dpi = get_dpi_for_window(this);
        this->SetPosition(pos);
        this->CenterOnScreen();
        int new_dpi = get_dpi_for_window(this);

        m_scale = (float)(new_dpi) / (float)(init_dpi);

        m_main_bitmap = bitmap;

        scale_bitmap(m_main_bitmap, m_scale);

        // init constant texts and scale fonts
        m_constant_text.init(Label::Body_16);

		// ORCA scale all fonts with monitor scale
        scale_font(m_constant_text.version_font,	m_scale * 2);
        scale_font(m_constant_text.based_on_font,	m_scale * 1.5f);
        scale_font(m_constant_text.credits_font,	m_scale * 2);

        // this font will be used for the action string
        m_action_font = m_constant_text.credits_font;

        // draw logo and constant info text
        Decorate(m_main_bitmap);
        wxGetApp().UpdateFrameDarkUI(this);

#ifdef __WXMSW__
        // PING: 啟動畫面去背——以 per-pixel alpha 重繪，桌面透出（取代白底畫布）
        render_layered();
#endif
    }

    void SetText(const wxString& text)
    {
        m_action_text = text;   // PING: 記住載入文字，供去背模式重繪用
#ifdef __WXMSW__
        // PING: 去背模式——重新合成 (logo + 版本字 + 載入字) 並更新 layered window
        render_layered();
#else
        set_bitmap(m_main_bitmap);
        if (!text.empty()) {
            wxBitmap bitmap(m_main_bitmap);

            wxMemoryDC memDC;
            memDC.SelectObject(bitmap);
            memDC.SetFont(m_action_font);
            memDC.SetTextForeground(StateColor::darkModeColorFor(wxColour(144, 144, 144)));
            int width = bitmap.GetWidth();
            int text_height = memDC.GetTextExtent(text).GetHeight();
            int text_width = memDC.GetTextExtent(text).GetWidth();
            wxRect text_rect(wxPoint(0, m_action_line_y_position), wxPoint(width, m_action_line_y_position + text_height));
            memDC.DrawLabel(text, text_rect, wxALIGN_CENTER);

            memDC.SelectObject(wxNullBitmap);
            set_bitmap(bitmap);
#ifdef __WXOSX__
            // without this code splash screen wouldn't be updated under OSX
            wxYield();
#endif
        }
#endif // __WXMSW__
    }

    void Decorate(wxBitmap& bmp)
    {
        if (!bmp.IsOk())
            return;

		bool is_dark = wxGetApp().app_config->get("dark_color_mode") == "1";

        // use a memory DC to draw directly onto the bitmap
        wxMemoryDC memDc(bmp);
        
        int width = bmp.GetWidth();
		int height = bmp.GetHeight();

		// Logo
        BitmapCache bmp_cache;
        wxBitmap logo_bmp = *bmp_cache.load_png(is_dark ? "splash_logo_dark" : "splash_logo", width, height);  // PING: branded PNG splash
        memDc.DrawBitmap(logo_bmp, 0, 0, true);

        // PING: 版本號（實際三碼）白字，置於圖片「Pro Slicer」右側（像素分析定位 x=22.4%、垂直中心52.7%）
        wxFont ver_font = m_constant_text.version_font;
        ver_font.SetPixelSize(wxSize(0, int(height * 0.026)));  // 字級 3.1%→2.6%（Eric 2026-07-22 裁縮小；三碼變長字縮小恰平衡）
        memDc.SetFont(ver_font);
        memDc.SetTextForeground(wxColour(255, 255, 255));       // 白字
        wxSize ver_ext = memDc.GetTextExtent(m_constant_text.version);
        memDc.DrawText(m_constant_text.version,
                       int(width * 0.224),
                       int(height * 0.527) - ver_ext.GetHeight() / 2);

        /* PING 2026-08-16（Eric 令「加一條『開發線試用包』」）：試用包要一眼看得出來。
           起因＝試用包與正式版視窗長得一模一樣、資料夾名只差 `-dev`，Eric 0816 連續開錯兩次
           （找不到只有開發線才有的「產生報價包」）。版本治理本來就要求廠內測試版雙標。
           判準＝**exe 旁邊有沒有 data_dir 資料夾**——那是試用包獨有的獨立資料根
           （SOP_開發線Portable試用包 §4；正式版與安裝版都走 %APPDATA%）。
           ⚠ 這裡刻意自己重算而不讀 data_dir()：splash 早於 set_data_dir() 執行。 */
        {
            namespace fs = boost::filesystem;
            const fs::path exe_dir =
                fs::path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data()).parent_path();
            if (fs::is_directory(exe_dir / "data_dir")) {
                wxFont tag_font = m_constant_text.version_font;
                tag_font.SetPixelSize(wxSize(0, int(height * 0.030)));
                memDc.SetFont(tag_font);
                memDc.SetTextForeground(wxColour(0xEA, 0x4E, 0x16));   // Raised Orange（深色底上的 accent）
                const wxString tag = wxString::FromUTF8("開發線試用包");
                memDc.DrawText(tag,
                               int(width * 0.224),
                               int(height * 0.527) + ver_ext.GetHeight() / 2 + int(height * 0.008));
            }
        }

        // Dynamic Text — 載入狀態文字（Loading configuration...），保留於底部
        m_action_line_y_position = int(height * 0.90);
    }

    static wxBitmap MakeBitmap()
    {
        int width = FromDIP(700, nullptr);   // PING: landscape to fit branded splash PNG (1400x900 = 2x DIP，200% 縮放內皆縮小取樣)
        int height = FromDIP(450, nullptr);

        wxImage image(width, height);
        wxBitmap new_bmp(image);

        wxMemoryDC memDC;
        memDC.SelectObject(new_bmp);
        memDC.SetBrush(StateColor::darkModeColorFor(*wxWHITE));
        memDC.DrawRectangle(-1, -1, width + 2, height + 2);
        memDC.DrawBitmap(new_bmp, 0, 0, true);
        return new_bmp;
    }

    void set_bitmap(wxBitmap& bmp)
    {
        m_window->SetBitmap(bmp);
        m_window->Refresh();
        m_window->Update();
    }

    void scale_bitmap(wxBitmap& bmp, float scale)
    {
        if (scale == 1.0)
            return;

        wxImage image = bmp.ConvertToImage();
        if (!image.IsOk() || image.GetWidth() == 0 || image.GetHeight() == 0)
            return;

        int width   = int(scale * image.GetWidth());
        int height  = int(scale * image.GetHeight());
        image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);

        bmp = wxBitmap(std::move(image));
    }

    void scale_font(wxFont& font, float scale)
    {
#ifdef __WXMSW__
        // Workaround for the font scaling in respect to the current active display,
        // not for the primary display, as it's implemented in Font.cpp
        // See https://github.com/wxWidgets/wxWidgets/blob/master/src/msw/font.cpp
        // void wxNativeFontInfo::SetFractionalPointSize(float pointSizeNew)
        wxNativeFontInfo nfi= *font.GetNativeFontInfo();
        float pointSizeNew  = scale * font.GetPointSize();
        nfi.lf.lfHeight     = nfi.GetLogFontHeightAtPPI(pointSizeNew, get_dpi_for_window(this));
        nfi.pointSize       = pointSizeNew;
        font = wxFont(nfi);
#else
        font.Scale(scale);
#endif //__WXMSW__
    }

#ifdef __WXMSW__
    // PING: 啟動畫面去背渲染——以 wxGraphicsContext 在「透明」點陣圖上合成
    // (splash_logo + 版本字 + 載入字)，再交給 update_splash_layered() 以 per-pixel alpha
    // 貼到視窗（桌面透出，取代原本 MakeBitmap 的白底畫布）。
    void render_layered()
    {
        const int width  = m_main_bitmap.GetWidth();
        const int height = m_main_bitmap.GetHeight();
        if (width <= 0 || height <= 0)
            return;

        // v2(2026-06-10)：全程 wxImage 合成。v1 用 wxBitmap+wxGraphicsContext 合成後
        // ConvertToImage——MSW 下 MemoryDC 來源的 bitmap 轉回 image 會丟 alpha →
        // 透明處變不透明黑（黑底矩形）。改法：PNG 以 wxImage 載入（自帶 alpha、Scale 保留），
        // 文字以「不透明貼片」蓋在圖中深色面板上（取樣既有底色），alpha 全程不經 DC/GC。
        const bool is_dark = wxGetApp().app_config->get("dark_color_mode") == "1";
        wxImage img(from_u8(Slic3r::var(is_dark ? "splash_logo_dark.png" : "splash_logo.png")), wxBITMAP_TYPE_PNG);
        if (!img.IsOk())
            return;
        if (img.GetWidth() != width || img.GetHeight() != height)
            img = img.Scale(width, height, wxIMAGE_QUALITY_HIGH);
        if (!img.HasAlpha())
            img.InitAlpha();

        auto blit_text = [&img](const wxString &text, const wxFont &font, const wxColour &fg,
                                int x /* -1=水平置中 */, int y, bool y_is_center) {
            if (text.empty())
                return;
            wxBitmap   measure_bmp(8, 8);
            wxMemoryDC mdc(measure_bmp);
            mdc.SetFont(font);
            const wxSize ext = mdc.GetTextExtent(text);
            mdc.SelectObject(wxNullBitmap);
            if (ext.x <= 0 || ext.y <= 0)
                return;
            if (x < 0)
                x = (img.GetWidth() - ext.x) / 2;
            if (y_is_center)
                y -= ext.y / 2;
            if (x < 0 || y < 0 || x + ext.x > img.GetWidth() || y + ext.y > img.GetHeight())
                return;
            // 底色取貼片中心既有像素（文字落在圖中不透明深色面板區）
            const int      sx = x + ext.x / 2, sy = y + ext.y / 2;
            const wxColour bg(img.GetRed(sx, sy), img.GetGreen(sx, sy), img.GetBlue(sx, sy));
            wxBitmap   patch(ext.x, ext.y, 24);
            wxMemoryDC pdc(patch);
            pdc.SetBackground(wxBrush(bg));
            pdc.Clear();
            pdc.SetFont(font);
            pdc.SetTextForeground(fg);
            pdc.DrawText(text, 0, 0);
            pdc.SelectObject(wxNullBitmap);
            const wxImage pimg = patch.ConvertToImage();   // 24bpp RGB，無 alpha 需求
            for (int dy = 0; dy < ext.y; ++dy)
                for (int dx = 0; dx < ext.x; ++dx) {
                    img.SetRGB(x + dx, y + dy, pimg.GetRed(dx, dy), pimg.GetGreen(dx, dy), pimg.GetBlue(dx, dy));
                    img.SetAlpha(x + dx, y + dy, 255);
                }
        };

        // 版本字（實際三碼）白字（位置同 Decorate：x=22.4%、垂直中心 52.7%）；字級與 Decorate 同步 2.6%
        wxFont layered_ver_font = m_constant_text.version_font;
        layered_ver_font.SetPixelSize(wxSize(0, int(height * 0.026)));
        blit_text(m_constant_text.version, layered_ver_font, *wxWHITE,
                  int(width * 0.224), int(height * 0.527), true);
        // 載入狀態字（灰，水平置中，y=頂端 m_action_line_y_position）
        blit_text(m_action_text, m_action_font, wxColour(144, 144, 144),
                  -1, m_action_line_y_position, false);

        update_splash_layered(this, img);
    }
#endif // __WXMSW__

private:
    wxStaticText* m_staticText_slicer_name;
    wxStaticText* m_staticText_slicer_version;
    wxStaticBitmap* m_bitmap;
    wxStaticText* m_staticText_loading;

    wxBitmap    m_main_bitmap;
    wxFont      m_action_font;
    wxString    m_action_text;            // PING: 目前載入文字（去背重繪用）
    int         m_action_line_y_position;
    float       m_scale {1.0};

    struct ConstantText
    {
        wxString title;
        wxString version;
        wxString credits;

        wxFont   title_font;
        wxFont   version_font;
        wxFont   credits_font;
        wxFont   based_on_font;

        void init(wxFont init_font)
        {
            // title
            //title = wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME;

            // PING: 啟動畫面版號跟實際三碼（Eric 2026-07-22 裁，取代舊品牌短版號 V3.5）；
            // 與關於頁同源 SoftFever_VERSION，日後升版免改此處
            version = "V" + wxString::FromUTF8(GUI_App::format_display_version());

            // credits infornation
            credits = "";

            //title_font    = Label::Head_16;
            version_font  = Label::Body_13;
            based_on_font = Label::Body_8;
            credits_font  = Label::Body_8;
        }
    }
    m_constant_text;
};

#ifdef __linux__
static void migrate_flatpak_legacy_datadir(const boost::filesystem::path &data_dir_path)
{
    if(!boost::filesystem::exists("/.flatpak-info"))
        return; // Not running as a Flatpak, nothing to migrate.
    
    namespace fs = boost::filesystem;

    if (fs::exists(data_dir_path)){
        std::cerr << "New Flatpak data dir: " << data_dir_path << std::endl;
        return;
    }
    std::cerr << "Migrating Flatpak data dir: " << data_dir_path << std::endl;

    std::string legacy_data_dir_str = data_dir_path.string();
    boost::replace_first(legacy_data_dir_str, "com.orcaslicer.OrcaSlicer", "io.github.orcaslicer.OrcaSlicer");
    const fs::path legacy_data_dir(legacy_data_dir_str);

    std::cerr << "Legacy Flatpak data dir: " << legacy_data_dir << std::endl;

    if ( ! fs::exists(legacy_data_dir) || ! fs::is_directory(legacy_data_dir))
        return;
    std::cerr << "Legacy Flatpak data dir exists: " << legacy_data_dir << std::endl;

    try {
        std::cerr << "Migrating Flatpak data dir from " << legacy_data_dir << " to " << data_dir_path << std::endl;
        copy_directory_recursively(legacy_data_dir, data_dir_path);
    } catch (const std::exception &ex) {
        std::cerr << "Failed to migrate Flatpak data dir from " << legacy_data_dir << " to " << data_dir_path << ": " << ex.what() << std::endl;
    }
}

bool static check_old_linux_datadir(const wxString& app_name) {
    // If we are on Linux and the datadir does not exist yet, look into the old
    // location where the datadir was before version 2.3. If we find it there,
    // tell the user that he might wanna migrate to the new location.
    // (https://github.com/prusa3d/PrusaSlicer/issues/2911)
    // To be precise, the datadir should exist, it is created when single instance
    // lock happens. Instead of checking for existence, check the contents.

    namespace fs = boost::filesystem;

    std::string new_path = Slic3r::data_dir();

    wxString dir;
    if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
        dir = wxFileName::GetHomeDir() + wxS("/.config");
    std::string default_path = (dir + "/" + app_name).ToUTF8().data();

    if (new_path != default_path) {
        // This happens when the user specifies a custom --datadir.
        // Do not show anything in that case.
        return true;
    }

    fs::path data_dir = fs::path(new_path);
    if (! fs::is_directory(data_dir))
        return true; // This should not happen.

    int file_count = std::distance(fs::directory_iterator(data_dir), fs::directory_iterator());

    if (file_count <= 1) { // just cache dir with an instance lock
        // BBS
    } else {
        // If the new directory exists, be silent. The user likely already saw the message.
    }
    return true;
}
#endif

struct FileWildcards {
    const char*                 title_id;
    std::vector<std::string_view> file_extensions;
};

static const FileWildcards file_wildcards_by_type[FT_SIZE] = {
    /* FT_STEP */    { L("STEP files"),      { ".stp"sv, ".step"sv } },
    /* FT_STL */     { L("STL files"),       { ".stl"sv } },
    /* FT_OBJ */     { L("OBJ files"),       { ".obj"sv } },
    /* FT_AMF */     { L("AMF files"),       { ".amf"sv, ".zip.amf"sv, ".xml"sv } },
    /* FT_3MF */     { L("3MF files"),       { ".3mf"sv } },
    /* FT_GCODE_3MF */ {L("Gcode 3MF files"), {".gcode.3mf"sv}},
    /* FT_GCODE */   { L("G-code files"),    { ".gcode"sv} },
#ifdef __APPLE__
    /* FT_MODEL */
    {L("Supported files"), {".3mf"sv, ".stl"sv, ".oltp"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv, ".usd"sv, ".usda"sv, ".usdc"sv, ".usdz"sv, ".abc"sv, ".ply"sv, ".drc"sv}},
#else
    /* FT_MODEL */
    {L("Supported files"), {".3mf"sv, ".stl"sv, ".oltp"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv, ".drc"sv}},
#endif
    /* FT_ZIP */     { L("ZIP files"),       { ".zip"sv } },
    /* FT_PROJECT */ { L("Project files"),   { ".3mf"sv} },
    /* FT_GALLERY */ { L("Known files"),     { ".stl"sv, ".obj"sv } },

    /* FT_INI */     { L("INI files"),       { ".ini"sv } },
    /* FT_SVG */     { L("SVG files"),       { ".svg"sv } },
    /* FT_TEX */     { L("Texture"),         { ".png"sv, ".svg"sv } },
    /* FT_SL1 */     { L("Masked SLA files"), { ".sl1"sv, ".sl1s"sv } },
    /* FT_DRC */     { L("Draco files"),     { ".drc"sv } },
};

// This function produces a Win32 file dialog file template mask to be consumed by wxWidgets on all platforms.
// The function accepts a custom extension parameter. If the parameter is provided, the custom extension
// will be added as a fist to the list. This is important for a "file save" dialog on OSX, which strips
// an extension from the provided initial file name and substitutes it with the default extension (the first one in the template).
wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    const FileWildcards& data = file_wildcards_by_type[file_type];
    std::string title;
    std::string mask;
    std::string custom_ext_lower;

    if (! custom_extension.empty()) {
        // Generate an extension into the title mask and into the list of extensions.
        custom_ext_lower = boost::to_lower_copy(custom_extension);
        const std::string custom_ext_upper = boost::to_upper_copy(custom_extension);
        if (custom_ext_lower == custom_extension) {
            // Add a lower case version.
            title = std::string("*") + custom_ext_lower;
            mask = title;
            // Add an upper case version.
            mask  += ";*";
            mask  += custom_ext_upper;
        } else if (custom_ext_upper == custom_extension) {
            // Add an upper case version.
            title = std::string("*") + custom_ext_upper;
            mask = title;
            // Add a lower case version.
            mask += ";*";
            mask += custom_ext_lower;
        } else {
            // Add the mixed case version only.
            title = std::string("*") + custom_extension;
            mask = title;
        }
    }

    for (const std::string_view &ext : data.file_extensions)
        // Only add an extension if it was not added first as the custom extension.
        if (ext != custom_ext_lower) {
            if (title.empty()) {
                title = "*";
                title += ext;
                mask  = title;
            } else {
                title += ", *";
                title += ext;
                mask  += ";*";
                mask  += ext;
            }
            mask += ";*";
            mask += boost::to_upper_copy(std::string(ext));
        }
    const wxString translated_title = Slic3r::GUI::I18N::translate(data.title_id);
    return GUI::format_wxstr("%s (%s)|%s", translated_title, title, mask);
}

static std::string libslic3r_translate_callback(const char *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data(); }

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
static void register_win32_dpi_event()
{
    enum { WM_DPICHANGED_ = 0x02e0 };

    wxWindow::MSWRegisterMessageHandler(WM_DPICHANGED_, [](wxWindow *win, WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
        const int dpi = wParam & 0xffff;
        const auto rect = reinterpret_cast<PRECT>(lParam);
        const wxRect wxrect(wxPoint(rect->top, rect->left), wxPoint(rect->bottom, rect->right));

        DpiChangedEvent evt(EVT_DPI_CHANGED_SLICER, dpi, wxrect);
        win->GetEventHandler()->AddPendingEvent(evt);

        return true;
    });
}
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

static GUID GUID_DEVINTERFACE_HID = { 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };

static void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(WM_DEVICECHANGE, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		        plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME) {
//					printf("DBT_DEVICEARRIVAL %d - Media has arrived: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
			        plater->GetEventHandler()->AddPendingEvent(HIDDeviceAttachedEvent(EVT_HID_DEVICE_ATTACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
            break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME)
//					printf("DBT_DEVICEARRIVAL %d - Media was removed: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        			plater->GetEventHandler()->AddPendingEvent(HIDDeviceDetachedEvent(EVT_HID_DEVICE_DETACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
			break;
        default:
            break;
        }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(MainFrame::WM_USER_MEDIACHANGED, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
        wchar_t sPath[MAX_PATH];
        if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
            struct _ITEMIDLIST* pidl = *reinterpret_cast<struct _ITEMIDLIST**>(wParam);
            if (! SHGetPathFromIDList(pidl, sPath)) {
                BOOST_LOG_TRIVIAL(error) << "MediaInserted: SHGetPathFromIDList failed";
                return false;
            }
        }
        switch (lParam) {
        case SHCNE_MEDIAINSERTED:
        {
            //printf("SHCNE_MEDIAINSERTED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
            break;
        }
        case SHCNE_MEDIAREMOVED:
        {
            //printf("SHCNE_MEDIAREMOVED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
            break;
        }
	    default:
//          printf("Unknown\n");
            break;
	    }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(WM_INPUT, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(Slic3r::GUI::find_toplevel_parent(win));
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
//        if (wParam == RIM_INPUTSINK && plater != nullptr && main_frame->IsActive()) {
        if (wParam == RIM_INPUT && plater != nullptr && main_frame->IsActive()) {
        RAWINPUT raw;
			UINT rawSize = sizeof(RAWINPUT);
			::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
			if (raw.header.dwType == RIM_TYPEHID && plater->get_mouse3d_controller().handle_raw_input_win32(raw.data.hid.bRawData, raw.data.hid.dwSizeHid))
				return true;
		}
        return false;
    });

	wxWindow::MSWRegisterMessageHandler(WM_COPYDATA, [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
		COPYDATASTRUCT* copy_data_structure = { 0 };
		copy_data_structure = (COPYDATASTRUCT*)lParam;
		if (copy_data_structure->dwData == 1) {
			LPCWSTR arguments = (LPCWSTR)copy_data_structure->lpData;
			Slic3r::GUI::wxGetApp().other_instance_message_handler()->handle_message(boost::nowide::narrow(arguments));
		}
		return true;
		});
}
#endif // WIN32

static void generic_exception_handle()
{
    // Note: Some wxWidgets APIs use wxLogError() to report errors, eg. wxImage
    // - see https://docs.wxwidgets.org/3.1/classwx_image.html#aa249e657259fe6518d68a5208b9043d0
    //
    // wxLogError typically goes around exception handling and display an error dialog some time
    // after an error is logged even if exception handling and OnExceptionInMainLoop() take place.
    // This is why we use wxLogError() here as well instead of a custom dialog, because it accumulates
    // errors if multiple have been collected and displays just one error message for all of them.
    // Otherwise we would get multiple error messages for one missing png, for example.
    //
    // If a custom error message window (or some other solution) were to be used, it would be necessary
    // to turn off wxLogError() usage in wx APIs, most notably in wxImage
    // - see https://docs.wxwidgets.org/trunk/classwx_image.html#aa32e5d3507cc0f8c3330135bc0befc6a
/*#ifdef WIN32
    //LPEXCEPTION_POINTERS exception_pointers = nullptr;
    __try {
        throw;
    }
    __except (CBaseException::UnhandledExceptionFilter2(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {
    //__except (exception_pointers = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
    //    if (exception_pointers) {
    //        CBaseException::UnhandledExceptionFilter(exception_pointers);
    //    }
    //    else
            throw;
    }
#else*/
    try {
        throw;
    } catch (const std::bad_alloc& ex) {
        // bad_alloc in main thread is most likely fatal. Report immediately to the user (wxLogError would be delayed)
        // and terminate the app so it is at least certain to happen now.
        BOOST_LOG_TRIVIAL(error) << boost::format("std::bad_alloc exception: %1%") % ex.what();
        flush_logs();
        wxString errmsg = wxString::Format(_L("PING Slicer will terminate because of running out of memory. "
                                              "It may be a bug. It will be appreciated if you report the issue to our team."));
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Fatal error"), wxOK | wxICON_ERROR);

        std::terminate();
        //throw;
     } catch (const boost::io::bad_format_string& ex) {
     	BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        	flush_logs();
        wxString errmsg = _L("PING Slicer will terminate because of a localization error. "
                             "It will be appreciated if you report the specific scenario this issue happened.");
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Critical error"), wxOK | wxICON_ERROR);
        std::terminate();
        //throw;
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        flush_logs();
        wxLogError(format_wxstr(_L("PING Slicer got an unhandled exception: %1%"), ex.what()));
        throw;
    }
//#endif
}

void GUI_App::toggle_show_gcode_window()
{
    m_show_gcode_window = !m_show_gcode_window;
    app_config->set_bool("show_gcode_window", m_show_gcode_window);
}

std::vector<std::string> GUI_App::split_str(std::string src, std::string separator)
{
    std::string::size_type pos;
    std::vector<std::string> result;
    src += separator;
    int size = src.size();

    for (int i = 0; i < size; i++)
    {
        pos = src.find(separator, i);
        if (pos < size)
        {
            std::string s = src.substr(i, pos - i);
            result.push_back(s);
            i = pos + separator.size() - 1;
        }
    }
    return result;
}

void GUI_App::post_init()
{
    assert(initialized());
    if (! this->initialized())
        throw Slic3r::RuntimeError("Calling post_init() while not yet initialized");

    m_open_method = "double_click";
    bool switch_to_3d = false;

    if (!this->init_params->input_files.empty()) {

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", init with input files, size %1%, input_gcode %2%")
            %this->init_params->input_files.size() %this->init_params->input_gcode;

        switch_to_3d = true;

        const auto first_url = this->init_params->input_files.front();
        if (this->init_params->input_files.size() == 1 && is_supported_open_protocol(first_url)) {
            start_download(first_url);
            m_open_method = "url";
        } else {
            if (this->init_params->input_gcode) {
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                plater_->select_view_3D("3D");
                this->plater()->load_gcode(from_u8(this->init_params->input_files.front()));
                m_open_method = "gcode";
            } else {
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                plater_->select_view_3D("3D");
                wxArrayString input_files;
                for (auto& file : this->init_params->input_files) {
                    input_files.push_back(wxString::FromUTF8(file));
                }
                this->plater()->set_project_filename(_L("Untitled"));
                this->plater()->load_files(input_files);
                try {
                    if (!input_files.empty()) {
                        std::string           file_path = input_files.front().ToStdString();
                        std::filesystem::path path(file_path);
                        m_open_method = "file_" + path.extension().string();
                    }
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", file path exception!";
                    m_open_method = "file";
                }
            }
        }
    }

//#if BBL_HAS_FIRST_PAGE
    bool slow_bootup = false;
    if (app_config->get("slow_bootup") == "true") {
        slow_bootup = true;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", slow bootup, won't render gl here.";
    }
    if (!switch_to_3d) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", begin load_gl_resources";
#ifndef __linux__
        mainframe->Freeze();
#endif
        plater_->canvas3D()->enable_render(false);
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
        plater_->select_view_3D("3D");
        //BBS init the opengl resource here
//#ifdef __linux__
        if (plater_->canvas3D()->get_wxglcanvas()->IsShownOnScreen()&&plater_->canvas3D()->make_current_for_postinit()) {
//#endif
            Size canvas_size = plater_->canvas3D()->get_canvas_size();
            wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to init opengl";
            wxGetApp().init_opengl();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init opengl";
            plater_->canvas3D()->init();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init canvas3D";
            wxGetApp().imgui()->new_frame();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init imgui frame";
            plater_->canvas3D()->enable_render(true);

            if (!slow_bootup) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to render a first frame for test";
                plater_->canvas3D()->render(false);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished rendering a first frame for test";
            }
//#ifdef __linux__
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "Found glcontext not ready, postpone the init";
        }
//#endif
        if (is_editor())
            mainframe->select_tab(size_t(0));
        if (app_config->get("default_page") == "1")
            mainframe->select_tab(size_t(1));
#ifndef __linux__
        mainframe->Thaw();
#endif
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", end load_gl_resources";
    }

    plater_->trigger_restore_project(1);
    //#endif

    //BBS: remove GCodeViewer as seperate APP logic
    /*if (this->init_params->start_as_gcodeviewer) {
        if (! this->init_params->input_files.empty())
            this->plater()->load_gcode(wxString::FromUTF8(this->init_params->input_files[0].c_str()));
    }
    else
    {
        if (! this->init_params->preset_substitutions.empty())
            show_substitutions_info(this->init_params->preset_substitutions);

#if 0
        // Load the cummulative config over the currently active profiles.
        //FIXME if multiple configs are loaded, only the last one will have an effect.
        // We need to decide what to do about loading of separate presets (just print preset, just filament preset etc).
        // As of now only the full configs are supported here.
        if (!m_print_config.empty())
            this->gui->mainframe->load_config(m_print_config);
#endif
        if (! this->init_params->load_configs.empty())
            // Load the last config to give it a name at the UI. The name of the preset may be later
            // changed by loading an AMF or 3MF.
            //FIXME this is not strictly correct, as one may pass a print/filament/printer profile here instead of a full config.
            this->mainframe->load_config_file(this->init_params->load_configs.back());
        // If loading a 3MF file, the config is loaded from the last one.
        if (!this->init_params->input_files.empty()) {
            const std::vector<size_t> res = this->plater()->load_files(this->init_params->input_files);
            if (!res.empty() && this->init_params->input_files.size() == 1) {
                // Update application titlebar when opening a project file
                const std::string& filename = this->init_params->input_files.front();
                //BBS: remove amf logic as project
                if (boost::algorithm::iends_with(filename, ".3mf"))
                    this->plater()->set_project_filename(filename);
            }
        }
        if (! this->init_params->extra_config.empty())
            this->mainframe->load_config(this->init_params->extra_config);
    }*/

    // BBS: to be checked
#if 1
    // show "Did you know" notification
    if (app_config->get("show_hints") == "true" && !is_gcode_viewer()) {
        plater_->get_notification_manager()->push_hint_notification(false);
    }
#endif

    hms_query = new HMSQuery();

    m_show_gcode_window = app_config->get_bool("show_gcode_window");
    if (m_networking_need_update) {
        show_network_plugin_download_dialog(false);
    }

    // Start preset sync after project opened, otherwise we could have preset change during project opening which could cause crash 
    if (app_config->get("sync_user_preset") == "true") {
        // BBS loading user preset
        // Always async, not such startup step
        // BOOST_LOG_TRIVIAL(info) << "Loading user presets...";
        // scrn->SetText(_L("Loading user presets..."));
        if (m_agent) {
            start_sync_user_preset();
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: true";
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: false";
    }

    // The extra CallAfter() is needed because of Mac, where this is the only way
    // to popup a modal dialog on start without screwing combo boxes.
    // This is ugly but I honestly found no better way to do it.
    // Neither wxShowEvent nor wxWindowCreateEvent work reliably.
    if (this->preset_updater) { // G-Code Viewer does not initialize preset_updater.
        CallAfter([this] {
            bool cw_showed = this->config_wizard_startup();

            std::string http_url = get_http_url(app_config->get_country_code());
            std::string language = GUI::into_u8(current_language_code());
            std::string network_ver = Slic3r::NetworkAgent::get_version();
            bool        sys_preset  = app_config->get("sync_system_preset") == "true";
            this->preset_updater->sync(http_url, language, network_ver, sys_preset ? preset_bundle : nullptr);

            this->check_new_version_sf();
            if (is_user_login() && !app_config->get_stealth_mode()) {
              // this->check_privacy_version(0);
              request_user_handle(0);
            }
        });
    }

    if(!m_networking_need_update && m_agent) {
        m_agent->set_on_ssdp_msg_fn(
            [this](std::string json_str) {
                if (is_closing()) {
                    return;
                }
                GUI::wxGetApp().CallAfter([this, json_str] {
                    if (m_device_manager) {
                        m_device_manager->on_machine_alive(json_str);
                    }
                    });
            }
        );
        m_agent->set_on_http_error_fn([this](unsigned int status, std::string body) {
            this->handle_http_error(status, body);
        });
        m_agent->start_discovery(true, false);
    }

    //update the plugin tips
    CallAfter([this] {
            mainframe->refresh_plugin_tips();
        });

    // remove old log files over LOG_FILES_MAX_NUM
    std::string log_addr = data_dir();
    if (!log_addr.empty()) {
        auto log_folder = boost::filesystem::path(log_addr) / "log";
        if (boost::filesystem::exists(log_folder)) {
           std::vector<std::pair<time_t, std::string>> files_vec;
           for (auto& it : boost::filesystem::directory_iterator(log_folder)) {
               auto temp_path = it.path();
               try {
                   if (it.status().type() == boost::filesystem::regular_file) {
                       std::time_t lw_t = boost::filesystem::last_write_time(temp_path) ;
                       files_vec.push_back({ lw_t, temp_path.filename().string() });
                   }
               } catch (const std::exception &) {
               }
           }
           std::sort(files_vec.begin(), files_vec.end(), [](
               std::pair<time_t, std::string> &a, std::pair<time_t, std::string> &b) {
               return a.first > b.first;
           });

           while (files_vec.size() > LOG_FILES_MAX_NUM) {
               auto full_path = log_folder / boost::filesystem::path(files_vec[files_vec.size() - 1].second);
               BOOST_LOG_TRIVIAL(info) << "delete log file over " << LOG_FILES_MAX_NUM << ", filename: "<< files_vec[files_vec.size() - 1].second;
               try {
                   boost::filesystem::remove(full_path);
               }
               catch (const std::exception& ex) {
                   BOOST_LOG_TRIVIAL(error) << "failed to delete log file: "<< files_vec[files_vec.size() - 1].second << ". Error: " << ex.what();
               }
               files_vec.pop_back();
           }
        }
    }
    /* C-2 第 3 項 W1：閒置預熱排程。放在 post_init 尾端＝app 該做的都做完了，
       再等一段閒置才判定點火（判定含「選中的是不是照片磚機」，見 photo_tile_schedule_warmup）。 */
    photo_tile_schedule_warmup();   // 延遲＝預設 15 秒（可用 PING_PHOTOTILE_WARMUP_DELAY_MS 覆寫，驗證用）

    BOOST_LOG_TRIVIAL(info) << "finished post_init";
//BBS: remove the single instance currently
#ifdef _WIN32
    // Sets window property to mainframe so other instances can indentify it.
    OtherInstanceMessageHandler::init_windows_properties(mainframe, m_instance_hash_int);
#endif //WIN32

    /* PING #34（L1）：開機初始化做完了，現在才建內嵌網頁（首頁、開著的自訂攝影機）。
       排到下一輪事件迴圈＝主程式已經在處理訊息，WebView2 的建立回呼不會再被開機的長工作擋住。 */
    CallAfter([this] {
        if (!is_closing())
            WebView::AllowCreation();
    });
}

wxDEFINE_EVENT(EVT_ENTER_FORCE_UPGRADE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_NO_NEW_VERSION, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_DIALOG, wxCommandEvent);
wxDEFINE_EVENT(EVT_CONNECT_LAN_MODE_PRINT, wxCommandEvent);
IMPLEMENT_APP(GUI_App)

//BBS: remove GCodeViewer as seperate APP logic
//GUI_App::GUI_App(EAppMode mode)
GUI_App::GUI_App()
    : wxApp()
    //, m_app_mode(mode)
    , m_app_mode(EAppMode::Editor)
    , m_em_unit(10)
    , m_imgui(new ImGuiWrapper())
	, m_removable_drive_manager(std::make_unique<RemovableDriveManager>())
    , m_downloader(std::make_unique<Downloader>())
	, m_other_instance_message_handler(std::make_unique<OtherInstanceMessageHandler>())
{
	//app config initializes early becasuse it is used in instance checking in OrcaSlicer.cpp
    this->init_app_config();
    this->init_download_path();
#if wxUSE_WEBVIEW_EDGE
    this->init_webview_runtime();
#endif

    reset_to_active();
}

void GUI_App::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown enter";

	if (m_removable_drive_manager) {
		removable_drive_manager()->shutdown();
	}

    // destroy login dialog
    if (login_dlg != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy login dialog");
        delete login_dlg;
        login_dlg = nullptr;
    }

    if (m_is_recreating_gui) return;
    set_closing(true);
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown exit";
}


std::string GUI_App::get_http_url(std::string country_code, std::string path)
{
    std::string url;
    if (country_code == "US") {
        url = "https://api.bambulab.com/";
    }
    else if (country_code == "CN") {
        url = "https://api.bambulab.cn/";
    }
    else if (country_code == "ENV_CN_DEV") {
        url = "https://api-dev.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_QA") {
        url = "https://api-qa.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_PRE") {
        url = "https://api-pre.bambu-lab.com/";
    }
    else {
        url = "https://api.bambulab.com/";
    }

    url += path.empty() ? "v1/iot-service/api/slicer/resource" : path;
    return url;
}

std::string GUI_App::get_model_http_url(std::string country_code)
{
    std::string url;
    if (country_code == "US") {
        url = "https://makerworld.com/";
    }
    else if (country_code == "CN") {
        url = "https://makerworld.com/";
    }
    else if (country_code == "ENV_CN_DEV") {
        url = "https://makerhub-dev.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_QA") {
        url = "https://makerhub-qa.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_PRE") {
        url = "https://makerhub-pre.bambu-lab.com/";
    }
    else {
        url = "https://makerworld.com/";
    }

    return url;
}


std::string GUI_App::get_plugin_url(std::string name, std::string country_code)
{
    std::string url = get_http_url(country_code);

    std::string curr_version;
    if (NetworkAgent::use_legacy_network) {
        curr_version = BAMBU_NETWORK_AGENT_VERSION_LEGACY;
    } else if (name == "plugins" && app_config) {
        std::string user_version = app_config->get_network_plugin_version();
        curr_version = user_version.empty() ? get_latest_network_version() : user_version;
    } else {
        curr_version = get_latest_network_version();
    }

    std::string using_version = curr_version.substr(0, 9) + "00";
    if (name == "cameratools")
        using_version = curr_version.substr(0, 6) + "00.00";
    url += (boost::format("?slicer/%1%/cloud=%2%") % name % using_version).str();
    return url;
}

static std::string decode(std::string const& extra, std::string const& path = {}) {
    char const* p = extra.data();
    char const* e = p + extra.length();
    while (p + 4 < e) {
        boost::uint16_t len = ((boost::uint16_t)p[2]) | ((boost::uint16_t)p[3] << 8);
        if (p[0] == '\x75' && p[1] == '\x70' && len >= 5 && p + 4 + len < e && p[4] == '\x01') {
            return std::string(p + 9, p + 4 + len);
        }
        else {
            p += 4 + len;
        }
    }
    return Slic3r::decode_path(path.c_str());
}

int GUI_App::download_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    int result = 0;
    json j;
    std::string err_msg;

    // get country_code
    AppConfig* app_config = wxGetApp().app_config;
    if (!app_config) {
        j["result"] = "failed";
        j["error_msg"] = "app_config is nullptr";
        return -1;
    }

    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: enter";
    m_networking_cancel_update = false;
    // get temp path
    fs::path target_file_path = (fs::temp_directory_path() / package_name);
    fs::path tmp_path = target_file_path;
    tmp_path += format(".%1%%2%", get_current_pid(), ".tmp");

#if defined(__WINDOWS__)
    if (is_running_on_arm64() && !NetworkAgent::use_legacy_network) {
        //set to arm64 for plugins
        std::map<std::string, std::string> current_headers = Slic3r::Http::get_extra_headers();
        current_headers["X-BBL-OS-Type"] = "windows_arm";

        Slic3r::Http::set_extra_headers(current_headers);
        BOOST_LOG_TRIVIAL(info) << boost::format("download_plugin: set X-BBL-OS-Type to windows_arm");
    }
#endif

    // get_url
    std::string  url = get_plugin_url(name, app_config->get_country_code());
    std::string download_url;
    Slic3r::Http http_url = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: check the plugin from " << url;
    http_url.timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete(
        [&download_url](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            Semver version;
                            std::string url;
                            std::string type;
                            std::string vendor;
                            std::string description;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    type = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: get version of settings's type, " << sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "version")) {
                                    version = *(Semver::parse(sub_iter.value()));
                                }
                                else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: get type " << type << ", version " << version.to_string() << ", url " << url;
                            download_url = url;
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: get version of plugin failed, body=" << body;
                }
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[download_plugin 1]: catch unknown exception";
                ;
            }
        }).on_error(
            [&result, &err_msg](std::string body, std::string error, unsigned int status) {
                BOOST_LOG_TRIVIAL(error) << "[download_plugin 1] on_error: " << error<<", body = " << body;
                err_msg += "[download_plugin 1] on_error: " + error + ", body = " + body;
                result = -1;
        }).perform_sync();

#if defined(__WINDOWS__)
    if (is_running_on_arm64() && !NetworkAgent::use_legacy_network) {
        //set back
        std::map<std::string, std::string> current_headers = Slic3r::Http::get_extra_headers();
        current_headers["X-BBL-OS-Type"] = "windows";

        Slic3r::Http::set_extra_headers(current_headers);
        BOOST_LOG_TRIVIAL(info) << boost::format("download_plugin: set X-BBL-OS-Type back to windows");
    }
#endif

    bool cancel = false;
    if (result < 0) {
        j["result"] = "failed";
        j["error_msg"] = err_msg;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return result;
    }


    if (download_url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: no available plugin found for this app version: " << SLIC3R_VERSION;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        j["result"] = "failed";
        j["error_msg"] = "[download_plugin 1]: no available plugin found for this app version: " + std::string(SLIC3R_VERSION);
        return -1;
    }
    else if (pro_fn) {
        pro_fn(InstallStatusNormal, 5, cancel);
    }

    if (m_networking_cancel_update || cancel) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[download_plugin 1]: %1%, cancelled by user") % __LINE__;
        j["result"] = "failed";
        j["error_msg"] = (boost::format("[download_plugin 1]: %1%, cancelled by user") % __LINE__).str();
        return -1;
    }
    BOOST_LOG_TRIVIAL(info) << "[download_plugin] get_url = " << download_url;

    // download
    Slic3r::Http http = Slic3r::Http::get(download_url);
    int reported_percent = 0;
    http.on_progress(
        [this, &pro_fn, cancel_fn, &result, &reported_percent, &err_msg](Slic3r::Http::Progress progress, bool& cancel) {
            int percent = 0;
            if (progress.dltotal != 0)
                percent = progress.dlnow * 50 / progress.dltotal;
            bool was_cancel = false;
            if (pro_fn && ((percent - reported_percent) >= 10)) {
                pro_fn(InstallStatusNormal, percent, was_cancel);
                reported_percent = percent;
                BOOST_LOG_TRIVIAL(info) << "[download_plugin 2] progress: " << reported_percent;
            }
            cancel = m_networking_cancel_update || was_cancel;
            if (cancel_fn)
                if (cancel_fn())
                    cancel = true;

            if (cancel) {
                err_msg += "[download_plugin] cancel";
                result = -1;
            }
        })
        .on_complete([&pro_fn, tmp_path, target_file_path](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "[download_plugin 2] completed";
            bool cancel = false;
            int percent = 0;
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_file_path);
            if (pro_fn) pro_fn(InstallStatusDownloadCompleted, 80, cancel);
            })
        .on_error([&pro_fn, &result, &err_msg](std::string body, std::string error, unsigned int status) {
            bool cancel = false;
            if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
            BOOST_LOG_TRIVIAL(error) << "[download_plugin 2] on_error: " << error<<", body = " << body;
            err_msg += "[download_plugin 2] on_error: " + error + ", body = " + body;
            result = -1;
        });
    http.perform_sync();
    j["result"] = result < 0 ? "failed" : "success";
    j["error_msg"] = err_msg;
    return result;
}

int GUI_App::install_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    bool cancel = false;
    std::string target_file_path = (fs::temp_directory_path() / package_name).string();

    BOOST_LOG_TRIVIAL(info) << "[install_plugin] enter";
    // get plugin folder
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / name;
    //auto plugin_folder = boost::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data()) / "plugins";
    auto backup_folder = plugin_folder/"backup";
    if (!boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << "[install_plugin] will create directory "<<plugin_folder.string();
        boost::filesystem::create_directory(plugin_folder);
    }
    if (!boost::filesystem::exists(backup_folder)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", will create directory %1%")%backup_folder.string();
        boost::filesystem::create_directory(backup_folder);
    }

    if (m_networking_cancel_update) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[install_plugin]: %1%, cancelled by user")%__LINE__;
        return -1;
    }
    if (pro_fn) {
        pro_fn(InstallStatusNormal, 50, cancel);
    }
    // unzip
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_reader(&archive, target_file_path)) {
        BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, open zip file failed")%__LINE__;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return InstallStatusUnzipFailed;
    }

    boost::filesystem::path legacy_lib_path, legacy_lib_backup;
    bool had_existing_legacy = false;
    if (name == "plugins") {
#if defined(_MSC_VER) || defined(_WIN32)
        legacy_lib_path = plugin_folder / (std::string(BAMBU_NETWORK_LIBRARY) + ".dll");
#elif defined(__WXMAC__)
        legacy_lib_path = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".dylib");
#else
        legacy_lib_path = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".so");
#endif
        legacy_lib_backup = legacy_lib_path;
        legacy_lib_backup += ".backup";

        if (boost::filesystem::exists(legacy_lib_path)) {
            had_existing_legacy = true;
            boost::system::error_code ec;
            boost::filesystem::rename(legacy_lib_path, legacy_lib_backup, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(warning) << "[install_plugin] failed to backup existing legacy library: " << ec.message();
                had_existing_legacy = false;
            } else {
                BOOST_LOG_TRIVIAL(info) << "[install_plugin] backed up existing legacy library";
            }
        }
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);
    mz_zip_archive_file_stat stat;
    BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, got %2% files")%__LINE__ %num_entries;
    for (mz_uint i = 0; i < num_entries; i++) {
        if (m_networking_cancel_update || cancel) {
            BOOST_LOG_TRIVIAL(info) << boost::format("[install_plugin]: %1%, cancelled by user")%__LINE__;
            return -1;
        }
        if (mz_zip_reader_file_stat(&archive, i, &stat)) {
            if (stat.m_uncomp_size > 0) {
                std::string dest_file;
                if (stat.m_is_utf8) {
                    dest_file = stat.m_filename;
                }
                else {
                    std::string extra(1024, 0);
                    size_t n = mz_zip_reader_get_extra(&archive, stat.m_file_index, extra.data(), extra.size());
                    dest_file = decode(extra.substr(0, n), stat.m_filename);
                }
                auto dest_path = plugin_folder / dest_file;
                boost::filesystem::create_directories(dest_path.parent_path());
                std::string dest_zip_file = encode_path(dest_path.string().c_str());
                try {
                    if (fs::exists(dest_path))
                        fs::remove(dest_path);
                    mz_bool res = 0;
#ifndef WIN32
                    if (S_ISLNK(stat.m_external_attr >> 16)) {
                        std::string link(stat.m_uncomp_size + 1, 0);
                        res = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, link.data(), stat.m_uncomp_size, 0);
                        try {
                            boost::filesystem::create_symlink(link, dest_path);
                        } catch (const std::exception &e) {
                            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " create_symlink:" << e.what();
                        }
                    } else {
#endif
                        res = mz_zip_reader_extract_to_file(&archive, stat.m_file_index, dest_zip_file.c_str(), 0);
#ifndef WIN32
                    }
#endif
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", extract  %1% from plugin zip %2%\n") % dest_file % stat.m_filename;
                    if (res == 0) {
#ifdef WIN32
                        std::wstring new_dest_zip_file = boost::locale::conv::utf_to_utf<wchar_t>(dest_path.generic_string());
                        res                            = mz_zip_reader_extract_to_file_w(&archive, stat.m_file_index, new_dest_zip_file.c_str(), 0);
#endif
                        if (res == 0) {
                            mz_zip_error zip_error = mz_zip_get_last_error(&archive);
                            BOOST_LOG_TRIVIAL(error) << "[install_plugin]Archive read error:" << mz_zip_get_error_string(zip_error) << std::endl;
                            close_zip_reader(&archive);
                            if (pro_fn) { pro_fn(InstallStatusUnzipFailed, 0, cancel); }
                            return InstallStatusUnzipFailed;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    // ensure the zip archive is closed and rethrow the exception
                    close_zip_reader(&archive);
                    BOOST_LOG_TRIVIAL(error) << "[install_plugin]Archive read exception:"<<e.what();
                    if (pro_fn) {
                        pro_fn(InstallStatusUnzipFailed, 0, cancel);
                    }
                    return InstallStatusUnzipFailed;
                }
            }
        }
        else {
            BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, mz_zip_reader_file_stat for file %2% failed")%__LINE__%i;
        }
    }

    close_zip_reader(&archive);

    if (name == "plugins") {
        std::string config_version = app_config->get_network_plugin_version();
        if (config_version.empty()) {
            config_version = get_latest_network_version();
            BOOST_LOG_TRIVIAL(info) << "[install_plugin] config_version was empty, using latest: " << config_version;
            app_config->set_network_plugin_version(config_version);
            GUI::wxGetApp().CallAfter([this] {
                if (app_config)
                    app_config->save();
            });
        }
        if (!config_version.empty() && boost::filesystem::exists(legacy_lib_path)) {
#if defined(_MSC_VER) || defined(_WIN32)
            auto versioned_lib = plugin_folder / (std::string(BAMBU_NETWORK_LIBRARY) + "_" + config_version + ".dll");
#elif defined(__WXMAC__)
            auto versioned_lib = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + "_" + config_version + ".dylib");
#else
            auto versioned_lib = plugin_folder / (std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + "_" + config_version + ".so");
#endif
            BOOST_LOG_TRIVIAL(info) << "[install_plugin] renaming newly extracted " << legacy_lib_path.string() << " to " << versioned_lib.string();
            boost::system::error_code ec;
            if (boost::filesystem::exists(versioned_lib)) {
                boost::filesystem::remove(versioned_lib, ec);
            }
            boost::filesystem::rename(legacy_lib_path, versioned_lib, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(error) << "[install_plugin] failed to rename to versioned: " << ec.message();
            }
        }

        if (had_existing_legacy && boost::filesystem::exists(legacy_lib_backup)) {
            BOOST_LOG_TRIVIAL(info) << "[install_plugin] restoring backed up legacy library";
            boost::system::error_code ec;
            boost::filesystem::rename(legacy_lib_backup, legacy_lib_path, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(warning) << "[install_plugin] failed to restore legacy library backup: " << ec.message();
            }
        }
    }

    {
        fs::path dir_path(plugin_folder);
        if (fs::exists(dir_path) && fs::is_directory(dir_path)) {
            int file_count = 0, file_index = 0;
            for (fs::directory_iterator it(dir_path); it != fs::directory_iterator(); ++it) {
                if (fs::is_regular_file(it->status())) { ++file_count; }
            }
            for (fs::directory_iterator it(dir_path); it != fs::directory_iterator(); ++it) {
                BOOST_LOG_TRIVIAL(info) << " current path:" << it->path().string();
                if (it->path().string() == backup_folder) {
                    continue;
                }
                auto dest_path = backup_folder.string() + "/" + it->path().filename().string();
                if (fs::is_regular_file(it->status())) {
                    BOOST_LOG_TRIVIAL(info) << " copy file:" << it->path().string() << "," << it->path().filename();
                    try {
                        if (pro_fn) { pro_fn(InstallStatusNormal, 50 + file_index / file_count, cancel); }
                        file_index++;
                        if (fs::exists(dest_path)) { fs::remove(dest_path); }
                        std::string    error_message;
                        CopyFileResult cfr = copy_file(it->path().string(), dest_path, error_message, false);
                        if (cfr != CopyFileResult::SUCCESS) { BOOST_LOG_TRIVIAL(error) << "Copying to backup failed(" << cfr << "): " << error_message; }
                    } catch (const std::exception &e) {
                        BOOST_LOG_TRIVIAL(error) << "Copying to backup failed: " << e.what();
                    }
                } else {
                    BOOST_LOG_TRIVIAL(info) << " copy framework:" << it->path().string() << "," << it->path().filename();
                    copy_framework(it->path().string(), dest_path);
                }
            }
        }
    }


    if (pro_fn)
        pro_fn(InstallStatusInstallCompleted, 100, cancel);
    if (name == "plugins")
        app_config->set_bool("installed_networking", true);
    BOOST_LOG_TRIVIAL(info) << "[install_plugin] success";
    return 0;
}

void GUI_App::restart_networking()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" enter, mainframe %1%")%mainframe;
    on_init_network(true);
    StaticBambuLib::reset();
    if(m_agent) {
        init_networking_callbacks();
        m_agent->set_on_ssdp_msg_fn(
            [this](std::string json_str) {
                if (is_closing()) {
                    return;
                }
                GUI::wxGetApp().CallAfter([this, json_str] {
                    if (m_device_manager) {
                        m_device_manager->on_machine_alive(json_str);
                    }
                    });
            }
        );
        m_agent->set_on_http_error_fn([this](unsigned int status, std::string body) {
            this->handle_http_error(status, body);
        });
        m_agent->start_discovery(true, false);
        if (mainframe)
            mainframe->refresh_plugin_tips();
        if (plater_)
            plater_->get_notification_manager()->bbl_close_plugin_install_notification();

        if (m_agent->is_user_login()) {
            remove_user_presets();
            enable_user_preset_folder(true);
            preset_bundle->load_user_presets(m_agent->get_user_id(), ForwardCompatibilitySubstitutionRule::Enable);
            mainframe->update_side_preset_ui();
        }

        if (app_config->get("sync_user_preset") == "true") {
            start_sync_user_preset();
        }
        // if (mainframe && this->app_config->get("staff_pick_switch") == "true") {
        //     if (mainframe->m_webview) { mainframe->m_webview->SendDesignStaffpick(has_model_mall()); }
        // }
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" exit, m_agent=%1%")%m_agent;
}

// Network plugin hot reload timeout constants (in milliseconds)
namespace {
    constexpr int CALLBACK_DRAIN_TIMEOUT_MS   = 200;  // Time to drain pending CallAfter callbacks
    constexpr int NETWORK_IDLE_TIMEOUT_MS     = 500;  // Max wait for network operations to complete
    constexpr int FINAL_DRAIN_TIMEOUT_MS      = 100;  // Final event processing before destruction
    constexpr int POLL_INTERVAL_MS            = 50;   // Polling interval for state checks
    constexpr int MAX_YIELD_ITERATIONS        = 20;   // Maximum wxYield calls per drain cycle
}

// Process pending wx events with bounded iteration count
void GUI_App::drain_pending_events(int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    int yield_count = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        // Process pending events
        if (wxTheApp) {
            wxTheApp->ProcessPendingEvents();
        }

        // Bounded wxYield to prevent infinite loops
        if (yield_count < MAX_YIELD_ITERATIONS) {
            wxYield();
            ++yield_count;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

// Wait for network operations to complete with state verification
bool GUI_App::wait_for_network_idle(int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (!m_agent) {
            return true;  // Agent already gone
        }

        // Verify all operations completed
        bool server_disconnected = !m_agent->is_server_connected();

        if (server_disconnected) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": network is idle";
            return true;
        }

        // Process events while waiting
        if (wxTheApp) {
            wxTheApp->ProcessPendingEvents();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": timeout after " << timeout_ms
                                << "ms, server_connected=" << (m_agent ? m_agent->is_server_connected() : false);
    return false;
}

bool GUI_App::hot_reload_network_plugin()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": starting hot reload";

    wxBusyCursor busy;
    wxBusyInfo info(_L("Reloading network plug-in..."), mainframe);
    wxYield();
    wxWindowDisabler disabler;

    if (mainframe) {
        int current_tab = mainframe->m_tabpanel->GetSelection();
        if (current_tab == MainFrame::TabPosition::tpMonitor) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": navigating away from Monitor tab before unload";
            mainframe->m_tabpanel->SetSelection(MainFrame::TabPosition::tp3DEditor);
        }
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": stopping sync thread before unload";
    if (m_user_sync_token) {
        m_user_sync_token.reset();
    }
    if (m_sync_update_thread.joinable()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": waiting for sync thread to finish";
        m_sync_update_thread.join();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": sync thread finished";
    }

    if (m_agent) {
        // Phase 1: Clear all callbacks (stops new invocations)
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 1 - clearing callbacks";
        m_agent->set_on_ssdp_msg_fn(nullptr);
        m_agent->set_on_user_login_fn(nullptr);
        m_agent->set_on_printer_connected_fn(nullptr);
        m_agent->set_on_server_connected_fn(nullptr);
        m_agent->set_on_http_error_fn(nullptr);
        m_agent->set_on_subscribe_failure_fn(nullptr);
        m_agent->set_on_message_fn(nullptr);
        m_agent->set_on_user_message_fn(nullptr);
        m_agent->set_on_local_connect_fn(nullptr);
        m_agent->set_on_local_message_fn(nullptr);
        m_agent->set_queue_on_main_fn(nullptr);

        // Phase 2: Drain pending CallAfter callbacks (bounded)
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 2 - draining callbacks";
        drain_pending_events(CALLBACK_DRAIN_TIMEOUT_MS);

        // Phase 3: Stop operations and verify return values
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 3 - stopping operations";
        bool discovery_stopped = m_agent->start_discovery(false, false);
        int disconnect_result = m_agent->disconnect_printer();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": discovery_stopped=" << discovery_stopped
                                << ", disconnect_result=" << disconnect_result;

        // Phase 4: Wait for idle with state verification
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 4 - waiting for idle";
        bool became_idle = wait_for_network_idle(NETWORK_IDLE_TIMEOUT_MS);
        if (!became_idle) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": proceeding despite timeout";
        }

        // Phase 5: Final bounded drain before destruction
        drain_pending_events(FINAL_DRAIN_TIMEOUT_MS);

        // Phase 6: Destroy agent
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 6 - destroying agent";
        delete m_agent;
        m_agent = nullptr;
    }

    // Phase 7: Unload module
    if (Slic3r::NetworkAgent::is_network_module_loaded()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Phase 7 - unloading module";
        drain_pending_events(FINAL_DRAIN_TIMEOUT_MS);
        int unload_result = Slic3r::NetworkAgent::unload_network_module();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": unload_result=" << unload_result;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": calling restart_networking";
    restart_networking();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": restart_networking returned";

    std::string loaded_version = Slic3r::NetworkAgent::get_version();
    bool success = m_agent != nullptr && !loaded_version.empty() && loaded_version != "00.00.00.00";
    bool user_logged_in = m_agent && m_agent->is_user_login();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": after restart_networking, is_user_login = " << user_logged_in
                            << ", m_agent = " << (m_agent ? "valid" : "null")
                            << ", version = " << loaded_version;

    if (success && m_agent && m_device_manager) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": connecting to cloud server";
        m_agent->connect_server();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": re-subscribing to cloud printers";
        m_device_manager->add_user_subscribe();
    }

    if (mainframe && mainframe->m_monitor) {
        mainframe->m_monitor->update_network_version_footer();
        mainframe->m_monitor->set_default();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": reset monitor panel";
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": hot reload " << (success ? "successful" : "failed");
    return success;
}

std::string GUI_App::get_latest_network_version() const
{
    return Slic3r::get_latest_network_version();
}

bool GUI_App::has_network_update_available() const
{
    std::string current = Slic3r::NetworkAgent::get_version();
    std::string latest = get_latest_network_version();

    if (current.empty() || current == "00.00.00.00")
        return false;

    return current.substr(0, 8) != latest.substr(0, 8);
}

void GUI_App::show_network_plugin_download_dialog(bool is_update)
{
    // PING: 停用 Bambu 網路外掛下載/提示（不打包閉源 BambuNetwork DLL，AGPL 合規、不連 Bambu 雲端）
    (void)is_update;
    return;
    auto load_error = Slic3r::NetworkAgent::get_load_error();

    NetworkPluginDownloadDialog::Mode mode;
    if (load_error.has_error) {
        mode = NetworkPluginDownloadDialog::Mode::CorruptedPlugin;
    } else if (is_update) {
        mode = NetworkPluginDownloadDialog::Mode::UpdateAvailable;
    } else {
        mode = NetworkPluginDownloadDialog::Mode::MissingPlugin;
    }

    std::string current_version = Slic3r::NetworkAgent::get_version();

    NetworkPluginDownloadDialog dlg(mainframe, mode, current_version,
        load_error.message, load_error.technical_details);

    int result = dlg.ShowModal();

    switch (result) {
    case NetworkPluginDownloadDialog::RESULT_DOWNLOAD:
        {
            std::string selected = dlg.get_selected_version();
            app_config->set_network_plugin_version(selected);
            app_config->save();

            DownloadProgressDialog download_dlg(_L("Downloading Network Plug-in"));
            download_dlg.ShowModal();
        }
        break;

    case NetworkPluginDownloadDialog::RESULT_REMIND_LATER:
        app_config->set_remind_network_update_later(true);
        app_config->save();
        break;

    case NetworkPluginDownloadDialog::RESULT_SKIP_VERSION:
        {
            std::string latest = get_latest_network_version();
            app_config->add_skipped_network_version(latest);
            app_config->save();
        }
        break;

    case NetworkPluginDownloadDialog::RESULT_DONT_ASK:
        app_config->set_network_update_prompt_disabled(true);
        app_config->save();
        break;

    case NetworkPluginDownloadDialog::RESULT_SKIP:
    default:
        break;
    }
}

void GUI_App::remove_old_networking_plugins()
{
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    //auto plugin_folder = boost::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data()) / "plugins";
    if (boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the directory "<<plugin_folder.string();
        try {
            fs::remove_all(plugin_folder);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins directory " << plugin_folder.string();
        }
    }
}

int GUI_App::updating_bambu_networking()
{
    DownloadProgressDialog dlg(_L("Downloading Bambu Network Plug-in"));
    dlg.ShowModal();
    return 0;
}

bool GUI_App::check_networking_version()
{
    std::string network_ver = Slic3r::NetworkAgent::get_version();
    if (!network_ver.empty()) {
        BOOST_LOG_TRIVIAL(info) << "get_network_agent_version=" << network_ver;
    }

    std::string studio_ver;
    if (NetworkAgent::use_legacy_network) {
        studio_ver = BAMBU_NETWORK_AGENT_VERSION_LEGACY;
    } else if (app_config) {
        std::string user_version = app_config->get_network_plugin_version();
        studio_ver = user_version.empty() ? get_latest_network_version() : user_version;
    } else {
        studio_ver = get_latest_network_version();
    }

    BOOST_LOG_TRIVIAL(info) << "check_networking_version: network_ver=" << network_ver << ", expected=" << studio_ver;

    if (network_ver.length() >= 8 && studio_ver.length() >= 8) {
        if (network_ver.substr(0,8) == studio_ver.substr(0,8)) {
            m_networking_compatible = true;
            return true;
        }
    }

    m_networking_compatible = false;
    return false;
}

bool GUI_App::is_compatibility_version()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": m_networking_compatible=%1%")%m_networking_compatible;
    return m_networking_compatible;
}

void GUI_App::cancel_networking_install()
{
    m_networking_cancel_update = true;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": plugin install cancelled!");
}

void GUI_App::init_networking_callbacks()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter, m_agent=%1%")%m_agent;
    if (m_agent) {
        //set callbacks
        //m_agent->set_on_user_login_fn([this](int online_login, bool login) {
        //    GUI::wxGetApp().request_user_handle(online_login);
        //    });

        m_agent->set_server_callback([](std::string url, int status) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": server_callback, url=%1%, status=%2%") % url % status;
            //CallAfter([this]() {
            //    if (!m_server_error_dialog) {
            //        /*m_server_error_dialog->EndModal(wxCLOSE);
            //        m_server_error_dialog->Destroy();
            //        m_server_error_dialog = nullptr;*/
            //        m_server_error_dialog = new NetworkErrorDialog(mainframe);
            //    }
            //
            //    if(plater()->get_select_machine_dialog() && plater()->get_select_machine_dialog()->IsShown()){
            //        return;
            //    }
            //
            //    if (m_server_error_dialog->m_show_again) {
            //        return;
            //    }
            //
            //    if (m_server_error_dialog->IsShown()) {
            //        return;
            //    }
            //
            //    m_server_error_dialog->ShowModal();
            //});
        });


        m_agent->set_on_server_connected_fn([this](int return_code, int reason_code) {
            if (is_closing()) {
            return;
            }
            if (return_code == 5) {
                GUI::wxGetApp().CallAfter([this] {
                    this->request_user_logout();
                    MessageDialog msg_dlg(nullptr, _L("Login information expired. Please login again."), "", wxAPPLY | wxOK);
                    if (msg_dlg.ShowModal() == wxOK) {
                        return;
                    }
                });
                return;
            }
            GUI::wxGetApp().CallAfter([this] {
                if (is_closing())
                    return;
                BOOST_LOG_TRIVIAL(trace) << "static: server connected";
                m_agent->set_user_selected_machine(m_agent->get_user_selected_machine());
                    if (this->is_enable_multi_machine()) {
                        auto evt = new wxCommandEvent(EVT_UPDATE_MACHINE_LIST);
                        wxQueueEvent(this, evt);
                    }
                    m_agent->set_user_selected_machine(m_agent->get_user_selected_machine());
                    //subscribe device
                    if (m_agent->is_user_login()) {

                        /*disconnect lan*/
                        DeviceManager* dev = this->getDeviceManager();
                        if (!dev) return;

                        MachineObject *obj = dev->get_selected_machine();
                        if (!obj) return;

                        /* resubscribe the cache dev list */
                        if (this->is_enable_multi_machine()) {

                            if (!dev->subscribe_list_cache.empty()) {
                                dev->subscribe_device_list(dev->subscribe_list_cache);
                            }
                        }
                    }
                });
            });

        m_agent->set_on_printer_connected_fn([this](std::string dev_id) {
            if (is_closing()) {
                return;
            }
            GUI::wxGetApp().CallAfter([this, dev_id] {
                if (is_closing())
                    return;
                bool tunnel = boost::algorithm::starts_with(dev_id, "tunnel/");
                /* request_pushing */
                MachineObject* obj = m_device_manager->get_my_machine(tunnel ? dev_id.substr(7) : dev_id);
                if (obj) {
                    obj->is_tunnel_mqtt = tunnel;
                    obj->command_request_push_all(true);
                    obj->command_get_version();
                    obj->erase_user_access_code();
                    obj->command_get_access_code();
                    if (m_agent)
                        m_agent->install_device_cert(obj->get_dev_id(), obj->is_lan_mode_printer());
                }
                });
            });

        m_agent->set_get_country_code_fn([this]() {
            if (app_config)
                return app_config->get_country_code();
            return std::string();
            }
        );

        m_agent->set_on_subscribe_failure_fn([this](std::string dev_id) {
            CallAfter([this, dev_id] {
                on_start_subscribe_again(dev_id);
            });
        });

        m_agent->set_on_local_connect_fn(
            [this](int state, std::string dev_id, std::string msg) {
                if (is_closing()) {
                    return;
                }
                CallAfter([this, state, dev_id, msg] {
                    if (is_closing()) {
                        return;
                    }
                    /* request_pushing */
                    MachineObject* obj = m_device_manager->get_my_machine(dev_id);
                    wxCommandEvent event(EVT_CONNECT_LAN_MODE_PRINT);

                    if (obj) {

                        if (obj->is_lan_mode_printer()) {
                            if (state == ConnectStatus::ConnectStatusOk) {
                                obj->command_request_push_all(true);
                                obj->command_get_version();
                                event.SetInt(0);
                                event.SetString(obj->get_dev_id());
                            } else if (state == ConnectStatus::ConnectStatusFailed) {
                                // Orca: only update status if same device id
                                if (m_device_manager->selected_machine != dev_id) return;

                                m_device_manager->set_selected_machine("");
                                wxString text;
                                if (msg == "5") {
                                    obj->set_access_code("");
                                    obj->erase_user_access_code();
                                    text = wxString::Format(_L("Incorrect password"));
                                    wxGetApp().show_dialog(text);
                                } else {
                                text = wxString::Format(_L("Connect %s failed! [SN:%s, code=%s]"), from_u8(obj->get_dev_name()), obj->get_dev_id(), msg);
                                    wxGetApp().show_dialog(text);
                                }
                                event.SetInt(-1);
                            } else if (state == ConnectStatus::ConnectStatusLost) {
                                m_device_manager->set_selected_machine("");
                                event.SetInt(-1);
                                BOOST_LOG_TRIVIAL(info) << "set_on_local_connect_fn: state = lost";
                            } else {
                                event.SetInt(-1);
                                BOOST_LOG_TRIVIAL(info) << "set_on_local_connect_fn: state = " << state;
                            }

                            obj->set_lan_mode_connection_state(false);
                        }
                        else {
                            if (state == ConnectStatus::ConnectStatusOk) {
                                event.SetInt(1);
                                event.SetString(obj->get_dev_id());
                            }
                            else if(msg == "5") {
                                event.SetInt(5);
                                event.SetString(obj->get_dev_id());
                            }
                            else {
                                event.SetInt(-2);
                                event.SetString(obj->get_dev_id());
                            }
                        }
                    }
                    if (wxGetApp().plater()->get_select_machine_dialog()) {
                        wxPostEvent(wxGetApp().plater()->get_select_machine_dialog(), event);
                    }
                });
            }
        );

        auto message_arrive_fn = [this](std::string dev_id, std::string msg) {
            if (is_closing()) {
                return;
            }
            CallAfter([this, dev_id, msg] {
                if (is_closing())
                    return;

                if (process_network_msg(dev_id, msg)) {
                    return;
                }

                if (MachineObject* obj = this->m_device_manager->get_user_machine(dev_id)) {
                    auto sel = this->m_device_manager->get_selected_machine();
                    if (sel && sel->get_dev_id() == dev_id) {
                        obj->parse_json("cloud", msg);
                        GUI::wxGetApp().sidebar().load_ams_list(obj);
                    } else {
                        obj->parse_json("cloud", msg, true);
                    }
                }

                if (GUI::wxGetApp().plater())
                    GUI::wxGetApp().plater()->update_machine_sync_status();
            });
        };

        m_agent->set_on_message_fn(message_arrive_fn);

        auto user_message_arrive_fn = [this](std::string user_id, std::string msg) {
            if (is_closing()) {
                return;
            }
            CallAfter([this, user_id, msg] {
                if (is_closing())
                    return;

                //check user
                if (user_id == m_agent->get_user_id()) {
                    this->m_user_manager->parse_json(msg);
                }

            });
        };

        m_agent->set_on_user_message_fn(user_message_arrive_fn);


        auto lan_message_arrive_fn = [this](std::string dev_id, std::string msg) {
            if (is_closing()) {
                return;
            }
            CallAfter([this, dev_id, msg] {
                if (is_closing())
                    return;

                if (this->process_network_msg(dev_id, msg)) {
                    return;
                }

                if (MachineObject* obj = m_device_manager->get_my_machine(dev_id)) {
                    obj->parse_json("lan", msg);
                    // Orca: skip it if it doesn't support subscription based filament sync
                    if (this->m_device_manager->get_selected_machine() == obj &&
                        m_agent->get_filament_sync_mode() == FilamentSyncMode::subscription) {
                        GUI::wxGetApp().sidebar().load_ams_list(obj);
                    }
                }

                if (GUI::wxGetApp().plater())
                    GUI::wxGetApp().plater()->update_machine_sync_status();
                });
        };
        m_agent->set_on_local_message_fn(lan_message_arrive_fn);
        m_agent->set_queue_on_main_fn([this](std::function<void()> callback) {
            CallAfter(callback);
        });
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit, m_agent=%1%")%m_agent;
}

GUI_App::~GUI_App()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter");
    if (app_config != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy app_config");
        delete app_config;
    }

    if (preset_bundle != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset_bundle");
        delete preset_bundle;
    }

    if (preset_updater != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset updater");
        delete preset_updater;
    }

    StaticBambuLib::release();
    BBLNetworkPlugin::shutdown();


    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit");
}

bool GUI_App::is_blocking_printing(MachineObject *obj_)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return true;
    std::string target_model;
    if (obj_ == nullptr) {
        obj_ = dev->get_selected_machine();
        if (obj_) {
            target_model = obj_->printer_type;
        }
    } else {
        target_model = obj_->printer_type;
    }

    if (!obj_)
    {
        return false;
    }

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    std::string    source_model  = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);

    if (source_model != target_model) {
        std::vector<std::string>      compatible_machine = obj_->get_compatible_machine();
        vector<std::string>::iterator it                 = find(compatible_machine.begin(), compatible_machine.end(), source_model);
        if (it == compatible_machine.end()) {
            return true;
        }
    }
    return false;
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string GUI_App::get_gl_info(bool for_github)
{
    return OpenGLManager::get_gl_info().to_string(for_github);
}

wxGLContext* GUI_App::init_glcontext(wxGLCanvas& canvas)
{
    return m_opengl_mgr.init_glcontext(canvas, init_params != nullptr ? init_params->opengl_version : std::make_pair(0, 0),
        init_params != nullptr ? init_params->opengl_compatibility_profile : false, init_params != nullptr ? init_params->opengl_debug : false);
}

bool GUI_App::init_opengl()
{
#ifdef __linux__
    bool status = m_opengl_mgr.init_gl();
    m_opengl_initialized = true;
    return status;
#else
    return m_opengl_mgr.init_gl();
#endif
}

// gets path to PrusaSlicer.ini, returns semver from first line comment
static boost::optional<Semver> parse_semver_from_ini(std::string path)
{
    std::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string body = buffer.str();
    size_t start = body.find("OrcaSlicer ");
    if (start == std::string::npos) {
        start = body.find("OrcaSlicer ");
        if (start == std::string::npos)
            return boost::none;
    }
    body = body.substr(start + 12);
    size_t end = body.find_first_of(" \n");
    if (end < body.size())
        body.resize(end);
    return Semver::parse(body);
}

void GUI_App::init_download_path()
{
    std::string down_path = app_config->get("download_path");

    if (down_path.empty()) {
        std::string user_down_path = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
        app_config->set("download_path", user_down_path);
    }
    else {
        fs::path dp(down_path);
        if (!fs::exists(dp)) {

            std::string user_down_path = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
            app_config->set("download_path", user_down_path);
        }
    }
}

#if wxUSE_WEBVIEW_EDGE
void GUI_App::init_webview_runtime()
{
    // Check WebView Runtime
    if (!WebView::CheckWebViewRuntime()) {
        int nRet = wxMessageBox(_L("PING Slicer requires the Microsoft WebView2 Runtime to operate certain features.\nClick Yes to install it now."),
                                _L("WebView2 Runtime"), wxYES_NO);
        if (nRet == wxYES) {
            WebView::DownloadAndInstallWebViewRuntime();
        }
    }
}
#endif

/* PING 2026-09-24（Eric 裁，版本治理第 4 件「測試版獨立設定資料夾」；牌 c-0924-DR-01，PM 卡 x-0923-PM-08）
   規則正本＝~/.claude/version-governance.md D-07：**所有測試版共用一個** PINGSlicer-Internal，只跟正式版分開；
   測試版不得預設讀寫正式版的資料夾。測試版＝PING_TEST_BUILD 非空（出貨線 T0xx、開發線 DEV 都算；出貨版為空字串）。
   做法＝測試版改 SetAppName(SLIC3R_APP_KEY "-Internal")：wxStandardPaths 的 GetUserDataDir（設定資料夾）與
   GetUserLocalDataDir（wxWebViewEdge 的 EBWebView＝首頁／工作室的網頁儲存區）都跟著 app 名走
   ⇒ 一處改動、兩個資料夾一起分開、三平台同一套。exe 旁有 data_dir 夾（開發線試用包／跟車驗收）照舊優先用它。
   🔴 分開之前（≤T060）網頁儲存區連 data_dir 夾都隔不開（SOP_工作室端到端GUI自動化 §27-2），這裡一起解。
   第一次（這台還沒有 Internal 夾）從正式資料夾**只讀**複製一次（Eric 裁 Q3 A）；正式資料夾全程不寫。
   已知代價（Eric 裁 Q2 B 的有意識取捨）：退回舊 T 會讀到新 T 改過的資料——T 與 T 之間不在本條防護範圍。 */
static bool ping_is_test_build() { return PING_TEST_BUILD[0] != '\0'; }

// 初始化早於 log 檔建立 ⇒ 複製結果先收在這裡，log 開好後再寫進去（init_app_config 內）。
static std::vector<std::string>& ping_seed_messages()
{
    static std::vector<std::string> msgs;
    return msgs;
}

using PingSkipFn = bool (*)(const std::string& lower_name, int depth);

// 設定資料夾最上層不抄的：記錄／快取／OTA 暫存（會重建）、各種人工備份（.bak／bak-／user_backup，
// 只是體積）、照片磚隱形宿主自己的 WebView 設定檔。user／system／printers／phototile 等整夾照抄
// （DL1016 本機注入在 system 裡，要帶）。
static bool ping_skip_data_entry(const std::string& n, int depth)
{
    if (depth != 0)
        return false;
    return n == "log" || n == "cache" || n == "ota" || n == "webview2_phototile" ||
           n.find(".bak") != std::string::npos || boost::starts_with(n, "bak-") || boost::starts_with(n, "user_backup");
}

// 網頁儲存區只抄 Local Storage／IndexedDB 兩夾（見 ping_seed_test_webview_storage）；夾內不抄鎖檔
// （leveldb 的 LOCK：正式版開著時被鎖、也不該帶過去，leveldb 會自己重建）。
static bool ping_skip_webview_entry(const std::string& n, int /*depth*/) { return n == "lock"; }

struct PingCopyStats
{
    size_t    files = 0, skipped = 0, failed = 0;
    uintmax_t bytes = 0;
};

// src → dst 遞迴複製（只讀 src）。只抄一般檔與資料夾、不跟符號連結；逐項失敗只記數、不中斷。
static void ping_copy_tree(const boost::filesystem::path& src, const boost::filesystem::path& dst, int depth, PingSkipFn skip,
                           PingCopyStats& st)
{
    namespace fs = boost::filesystem;
    boost::system::error_code ec;
    fs::create_directories(dst, ec);
    if (ec) {
        ++st.failed;
        return;
    }
    fs::directory_iterator it(src, ec), end;
    if (ec) {
        ++st.failed;
        return;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ++st.failed;
            break;
        }
        const fs::path    p    = it->path();
        const std::string name = boost::algorithm::to_lower_copy(p.filename().string());
        if (skip(name, depth)) {
            ++st.skipped;
            continue;
        }
        const fs::file_status s = fs::symlink_status(p, ec);
        if (ec) {
            ++st.failed;
            continue;
        }
        if (fs::is_directory(s)) {
            ping_copy_tree(p, dst / p.filename(), depth + 1, skip, st);
        } else if (fs::is_regular_file(s)) {
            fs::copy_file(p, dst / p.filename(), fs::copy_options::none, ec);
            if (ec) {
                ++st.failed;
            } else {
                ++st.files;
                st.bytes += fs::file_size(p, ec);
            }
        } else {
            ++st.skipped;   // 符號連結等：不跟
        }
    }
}

// 把 official 抄成 target（target 不存在時才做）。先抄進旁邊的 *.seeding 暫存夾，全部抄完才改名 ⇒
// 中途被關掉不會留下半套，下次開再重來。only 非空＝只抄這幾個相對子路徑（其餘讓程式自己重建）。回傳是否抄了。
static bool ping_seed_once(const boost::filesystem::path& official, const boost::filesystem::path& target, PingSkipFn skip,
                           const char* what, const std::vector<std::string>& only = {})
{
    namespace fs = boost::filesystem;
    boost::system::error_code ec;
    // 防呆：target 與 official 同一處（app 名沒換成功）⇒ 什麼都不做，絕不在正式資料夾上動作。
    if (fs::path(target).make_preferred() == fs::path(official).make_preferred())
        return false;
    // ⚠ 不能寫 `exists(target, ec) || ec`：Windows 上「不存在」本身也會設 ec（2026-09-24 harness 實抓——
    //    那樣寫等於「永遠不抄、連一行 log 都沒有」）。以 file_status 的型別判：不存在＝可以抄；其他讀不到＝不動。
    const fs::file_status ts = fs::status(target, ec);
    if (fs::exists(ts))
        return false;                                   // 已有測試版的那一份 ⇒ 永遠不再抄
    if (ec && ts.type() != fs::file_not_found) {
        ping_seed_messages().push_back(std::string("PING 測試版資料夾：") + what + " 判斷不了 " + target.string() + " 在不在（" + ec.message() + "），不複製");
        return false;
    }
    if (!fs::is_directory(official, ec)) {
        ping_seed_messages().push_back(std::string("PING 測試版資料夾：") + what + " 沒有正式版可複製（" + official.string() + "），從空白開始");
        return false;
    }
    fs::path tmp = target;
    tmp += ".seeding";
    fs::remove_all(tmp, ec);                            // 上次抄到一半留下的暫存（在 Internal 旁邊，不是正式資料夾）
    PingCopyStats st;
    if (only.empty()) {
        ping_copy_tree(official, tmp, 0, skip, st);
    } else {
        fs::create_directories(tmp, ec);
        for (const std::string& sub : only)
            if (fs::is_directory(official / sub, ec))
                ping_copy_tree(official / sub, tmp / sub, 1, skip, st);
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        boost::system::error_code ec2;
        fs::remove_all(tmp, ec2);
        ping_seed_messages().push_back(std::string("PING 測試版資料夾：") + what + " 複製後改名失敗（" + ec.message() + "），從空白開始");
        return false;
    }
    ping_seed_messages().push_back((boost::format("PING 測試版資料夾：%1% 已從正式版只讀複製一次：%2% → %3%（檔 %4%／%5% bytes／略過 %6%／失敗 %7%）") %
                                    what % official.string() % target.string() % st.files % st.bytes % st.skipped % st.failed)
                                       .str());
    return true;
}

// 設定資料夾：<使用者資料根>/PINGSlicer-Internal ← <使用者資料根>/PINGSlicer（只在這台還沒有 Internal 夾時）。
// 🔴 兩支 seed 都包 try：複製只是方便，任何例外都只記下來、從空白開始，**絕不能讓測試版開不起來**。
static void ping_seed_test_data_dir(const boost::filesystem::path& internal_dir)
{
    if (!ping_is_test_build())
        return;
    try {
        const boost::filesystem::path official = internal_dir.parent_path() / SLIC3R_APP_KEY;
        if (ping_seed_once(official, internal_dir, ping_skip_data_entry, "設定資料夾")) {
            // 讓人在檔案總管裡一眼看得出這是什麼夾（寫在測試版這一份裡，正式資料夾不寫）
            // 檔名刻意用 ASCII：path::string() 的窄字元轉換不必賭語系設定
            boost::nowide::ofstream f((internal_dir / "_PING_TEST_DATA_ROOT_README.txt").string());
            f << "這是 PING Slicer 廠內測試版（標題列帶 Beta 的版本）共用的設定資料夾，與正式版的 "
              << official.string() << " 分開。\n"
              << "第一次開測試版（" << PING_TEST_BUILD << "）時從正式版只讀複製一次；之後兩邊各自獨立，正式版不會被測試版改到。\n"
              << "規則：版本治理 D-07（Eric 2026-09-24 裁：所有測試版共用這一個）。\n";
        }
    } catch (const std::exception& e) {
        ping_seed_messages().push_back(std::string("PING 測試版資料夾：設定資料夾複製例外（") + e.what() + "），從空白開始");
    } catch (...) {
        ping_seed_messages().push_back("PING 測試版資料夾：設定資料夾複製發生未知例外，從空白開始");
    }
}

// 網頁儲存區（首頁／工作室的 WebView2）：%LOCALAPPDATA%\PINGSlicer-Internal\EBWebView ← %LOCALAPPDATA%\PINGSlicer\EBWebView。
// exe 旁有 data_dir 夾的試用包也走這條（它們的網頁儲存區原本同樣跟正式版共用）。
// **只抄 Default\Local Storage 與 Default\IndexedDB**＝照片磚「我的圖」、舊校正表等頁面資料（2026-09-24 本機實量：
// EBWebView 169 MB，Local Storage 只 817 KB）；其餘是瀏覽器快取、歷程、WebView 自己存的登入資料——不該帶也不必帶，
// WebView2 開起來會自己把缺的補齊。
static void ping_seed_test_webview_storage()
{
#ifdef _WIN32
    if (!ping_is_test_build())
        return;
    namespace fs = boost::filesystem;
    try {
        const fs::path internal_local = fs::path(wxStandardPaths::Get().GetUserLocalDataDir().ToUTF8().data());
        const fs::path official_local = internal_local.parent_path() / SLIC3R_APP_KEY;
        ping_seed_once(official_local / "EBWebView", internal_local / "EBWebView", ping_skip_webview_entry, "網頁儲存區",
                       {"Default/Local Storage", "Default/IndexedDB"});
    } catch (const std::exception& e) {
        ping_seed_messages().push_back(std::string("PING 測試版資料夾：網頁儲存區複製例外（") + e.what() + "），從空白開始");
    } catch (...) {
        ping_seed_messages().push_back("PING 測試版資料夾：網頁儲存區複製發生未知例外，從空白開始");
    }
#endif
}

void GUI_App::init_app_config()
{
	// Profiles for the alpha are stored into the PrusaSlicer-alpha directory to not mix with the current release.
    // PING：測試版改用 "<APP_KEY>-Internal"（見上方 ping_is_test_build 註解）；出貨版維持原名。
    SetAppName(ping_is_test_build() ? SLIC3R_APP_KEY "-Internal" : SLIC3R_APP_KEY);
//	SetAppName(SLIC3R_APP_KEY "-alpha");
//  SetAppName(SLIC3R_APP_KEY "-beta");
//	SetAppDisplayName(SLIC3R_APP_NAME);

	// Set the Slic3r data directory at the Slic3r XS module.
	// Unix: ~/ .Slic3r
	// Windows : "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
	// Mac : "~/Library/Application Support/Slic3r"

    if (data_dir().empty()) {
        // Orca: check if data_dir folder exists in application folder use it if it exists
        // Note:wxStandardPaths::Get().GetExecutablePath() return following paths
        // Unix: /usr/local/bin/exename
        // Windows: "C:\Programs\AppFolder\exename.exe"
        // Mac: /Applications/exename.app/Contents/MacOS/exename
        // TODO: have no idea what to do with Linux bundles
        auto _app_folder = boost::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data()).parent_path();
#ifdef __APPLE__
        // On macOS, the executable is inside the .app bundle.
        _app_folder = _app_folder.parent_path().parent_path().parent_path();
#endif
        boost::filesystem::path app_data_dir_path = _app_folder / "data_dir";
        if (boost::filesystem::exists(app_data_dir_path)) {
            set_data_dir(app_data_dir_path.string());
        }
        else{
            boost::filesystem::path data_dir_path;
            #ifndef __linux__
                std::string data_dir = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
                //BBS create folder if not exists
                data_dir_path = boost::filesystem::path(data_dir);
                ping_seed_test_data_dir(data_dir_path);   // PING：測試版第一次開＝從正式版只讀複製一次；出貨版不做事
                set_data_dir(data_dir);
            #else
                // Since version 2.3, config dir on Linux is in ${XDG_CONFIG_HOME}.
                // https://github.com/prusa3d/PrusaSlicer/issues/2911
                wxString dir;
                if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
                    dir = wxFileName::GetHomeDir() + wxS("/.config");
                data_dir_path = boost::filesystem::path((dir + "/" + GetAppName()).ToUTF8().data());
                ping_seed_test_data_dir(data_dir_path);   // PING：同上（放在 flatpak 遷移之前）
                migrate_flatpak_legacy_datadir(data_dir_path);
                set_data_dir(data_dir_path.string());
            #endif
            if (!boost::filesystem::exists(data_dir_path)){
                boost::filesystem::create_directory(data_dir_path);
            }
        }

        // Change current dirtory of application

#ifdef _WIN32
    [[maybe_unused]] auto unused_result = _chdir(encode_path((Slic3r::data_dir() + "/log").c_str()).c_str());
#else
    [[maybe_unused]] auto unused_result = chdir(encode_path((Slic3r::data_dir() + "/log").c_str()).c_str());
#endif

    } else {
        m_datadir_redefined = true;
    }

    // PING：網頁儲存區跟設定資料夾分開走（試用包的 data_dir 夾管不到它），測試版第一次開也從正式版只讀複製一次。
    //       必須早於任何 WebView 建立——之後 WebView2 會自己建出 EBWebView，就再也不會抄了。
    ping_seed_test_webview_storage();

    // start log here
    std::time_t       t        = std::time(0);
    std::tm *         now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "debug_%a_%b_%d_%H_%M_%S_");
    buf << get_current_pid() << ".log";
    std::string log_filename = buf.str();
#if !BBL_RELEASE_TO_PUBLIC
    set_log_path_and_level(log_filename, 5);
#else
    set_log_path_and_level(log_filename, 3);
#endif

    BOOST_LOG_TRIVIAL(info) << boost::format("gui mode, Current OrcaSlicer Version %1% build %2%") % SoftFever_VERSION % GIT_COMMIT_HASH;
    // PING：測試版資料夾的一次性複製結果（發生在 log 檔建立之前，這裡補寫；只有第一次開會有，用 warning 讓預設等級也看得到）
    for (const std::string& m : ping_seed_messages())
        BOOST_LOG_TRIVIAL(warning) << m;
    ping_seed_messages().clear();
    if (ping_is_test_build())
        BOOST_LOG_TRIVIAL(info) << "PING 測試版（" << PING_TEST_BUILD << "）設定資料夾＝" << data_dir();

    //BBS: remove GCodeViewer as seperate APP logic
	if (!app_config)
        app_config = new AppConfig();
        //app_config = new AppConfig(is_editor() ? AppConfig::EAppMode::Editor : AppConfig::EAppMode::GCodeViewer);

    m_config_corrupted = false;
	// load settings
	m_app_conf_exists = app_config->exists();
	if (m_app_conf_exists) {
        std::string error = app_config->load();
        if (!error.empty()) {
            // Orca: if the config file is corrupted, we will show a error dialog and create a default config file.
            m_config_corrupted = true;

        }
        // Save orig_version here, so its empty if no app_config existed before this run.
        m_last_config_version = app_config->orig_version();//parse_semver_from_ini(app_config->config_path());
    }
    else {
#ifdef _WIN32
        // update associate files from registry information
        if (is_associate_files(L"3mf")) {
            app_config->set("associate_3mf", "true");
        }
        if (is_associate_files(L"stl")) {
            app_config->set("associate_stl", "true");
        }
        if (is_associate_files(L"step") && is_associate_files(L"stp")) {
            app_config->set("associate_step", "true");
        }
#endif // _WIN32
    }
    set_logging_level(Slic3r::level_string_to_boost(app_config->get("log_severity_level")));

}

// returns true if found newer version and user agreed to use it
bool GUI_App::check_older_app_config(Semver current_version, bool backup)
{
    //BBS: current no need these logic
    return false;
}

void GUI_App::copy_older_config()
{
    preset_bundle->copy_files(m_older_data_dir_path);
}

std::map<std::string, std::string> GUI_App::get_extra_header()
{
    std::map<std::string, std::string> extra_headers;
    extra_headers.insert(std::make_pair("X-BBL-Client-Type", "slicer"));
    extra_headers.insert(std::make_pair("X-BBL-Client-Name", SLIC3R_APP_NAME));
    extra_headers.insert(std::make_pair("X-BBL-Client-Version", VersionInfo::convert_full_version(SLIC3R_VERSION)));
#if defined(__WINDOWS__)
#ifdef _M_X64
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "windows"));
#else
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "windows_arm"));
#endif
#elif defined(__APPLE__)
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "macos"));
#elif defined(__LINUX__)
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "linux"));
#endif
    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);
    std::string os_version = (boost::format("%1%.%2%.%3%") % major % minor % micro).str();
    extra_headers.insert(std::make_pair("X-BBL-OS-Version", os_version));
    if (app_config)
        extra_headers.insert(std::make_pair("X-BBL-Device-ID", app_config->get("slicer_uuid")));
    extra_headers.insert(std::make_pair("X-BBL-Language", convert_studio_language_to_api(into_u8(current_language_code_safe()))));
    return extra_headers;
}

//BBS
void GUI_App::init_http_extra_header()
{
    std::map<std::string, std::string> extra_headers = get_extra_header();

    if (m_agent)
        m_agent->set_extra_http_header(extra_headers);
}

void GUI_App::update_http_extra_header()
{
    std::map<std::string, std::string> extra_headers = get_extra_header();
    Slic3r::Http::set_extra_headers(extra_headers);
    if (m_agent)
        m_agent->set_extra_http_header(extra_headers);
}

void GUI_App::on_start_subscribe_again(std::string dev_id)
{
    auto start_subscribe_timer = new wxTimer(this, wxID_ANY);
    Bind(wxEVT_TIMER, [this, start_subscribe_timer, dev_id](auto& e) {
        start_subscribe_timer->Stop();
        Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (!dev) return;
        MachineObject* obj = dev->get_selected_machine();
        if (!obj) return;

        if ( (dev_id == obj->get_dev_id()) && obj->is_connecting() && obj->subscribe_counter > 0) {
            obj->subscribe_counter--;
            if(wxGetApp().getAgent()) wxGetApp().getAgent()->set_user_selected_machine(dev_id);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": dev_id=" << obj->get_dev_id();
        }
    });
    start_subscribe_timer->Start(5000, wxTIMER_ONE_SHOT);
}

std::string GUI_App::get_local_models_path()
{
    std::string local_path = "";
    if (data_dir().empty()) {
        return local_path;
    }

    auto models_folder = (boost::filesystem::path(data_dir()) / "models");
    local_path = models_folder.string();

    if (!fs::exists(models_folder)) {
        if (!fs::create_directory(models_folder)) {
            local_path = "";
        }
        BOOST_LOG_TRIVIAL(info) << "create models folder:" << models_folder.string();
    }
    return local_path;
}

void GUI_App::init_single_instance_checker(const std::string &name, const std::string &path)
{
    BOOST_LOG_TRIVIAL(debug) << "init wx instance checker " << name << " "<< path;
    m_single_instance_checker = std::make_unique<wxSingleInstanceChecker>(boost::nowide::widen(name), boost::nowide::widen(path));
}

bool GUI_App::OnInit()
{
    try {
        return on_init_inner();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "OnInit Got Fatal error: " << e.what();
        generic_exception_handle();
        return false;
    }
}

int GUI_App::OnExit()
{
    // C-2：照片磚隱形宿主先收乾淨（先標 Closing，晚到的 COM/thread/CallAfter 回呼安靜退場）
    photo_tile_shutdown_host();

    stop_sync_user_preset();

    if (m_device_manager) {
        delete m_device_manager;
        m_device_manager = nullptr;
    }

    if (m_user_manager) {
        delete m_user_manager;
        m_user_manager = nullptr;
    }

    // Clear the printer agent cache before destroying the NetworkAgent.
    // This disconnects all cached agents and releases their shared_ptrs,
    // ensuring clean thread shutdown before the agent is deleted.
    NetworkAgentFactory::clear_printer_agent_cache();

    if (m_agent) {
        // BBS avoid a crash on mac platform
#ifdef __WINDOWS__
        m_agent->start_discovery(false, false);
#endif
        delete m_agent;
        m_agent = nullptr;
    }

    // Orca: clean up encrypted bbl network log file if plugin is used
    // No point to keep them as they are encrypted and can't be used for debugging
    try {
        auto              log_folder  = boost::filesystem::path(data_dir()) / "log";
        const std::string filePattern = R"(debug_network_.*\.log\.enc)";
        std::regex        pattern(filePattern);
        if (boost::filesystem::exists(log_folder)) {
            std::vector<boost::filesystem::path> network_logs;
            for (auto& it : boost::filesystem::directory_iterator(log_folder)) {
                auto temp_path = it.path();
                if (boost::filesystem::is_regular_file(temp_path) && std::regex_match(temp_path.filename().string(), pattern)) {
                    network_logs.push_back(temp_path.filename());
                }
            }
            for (auto f : network_logs) {
                boost::filesystem::remove(f);
            }
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Failed to clean up encrypt bbl network log file";
    }

    return wxApp::OnExit();
}

class wxBoostLog : public wxLog
{
    void DoLogText(const wxString &msg) override {

        BOOST_LOG_TRIVIAL(warning) << msg.ToUTF8().data();
    }
    ~wxBoostLog() override
    {
        // This is a hack. Prevent thread logs from going to wxGuiLog on app quit.
        auto t = wxLog::SetActiveTarget(this);
        wxLog::FlushActive();
        wxLog::SetActiveTarget(t);
    }
};

std::string get_system_info()
{
    std::stringstream out;

    std::string b_start  = "";
    std::string b_end    = "";
    std::string line_end = "\n";

    out << b_start << "Operating System:    " << b_end << wxPlatformInfo::Get().GetOperatingSystemFamilyName() << line_end;
    out << b_start << "System Architecture: " << b_end << wxPlatformInfo::Get().GetBitnessName() << line_end;
    out << b_start <<
#if defined _WIN32
        "Windows Version:     "
#else
        // Hopefully some kind of unix / linux.
        "System Version:      "
#endif
        << b_end << wxPlatformInfo::Get().GetOperatingSystemDescription() << line_end;
    out << b_start << "Total RAM size [MB]: " << b_end << Slic3r::format_memsize_MB(Slic3r::total_physical_memory());

    return out.str();
}

bool GUI_App::on_init_inner()
{
    wxLog::SetActiveTarget(new wxBoostLog());
#if BBL_RELEASE_TO_PUBLIC
    wxLog::SetLogLevel(wxLOG_Message);
#endif

    ::Label::initSysFont();

    // Set initialization of image handlers before any UI actions - See GH issue #7469
    wxInitAllImageHandlers();
#ifdef NDEBUG
    wxImage::SetDefaultLoadFlags(0); // ignore waring in release build
#endif

#if defined(_WIN32) && ! defined(_WIN64)
    // BBS: remove 32bit build prompt
    // Win32 32bit build.
#endif // _WIN64

    // Forcing back menu icons under gtk2 and gtk3. Solution is based on:
    // https://docs.gtk.org/gtk3/class.Settings.html
    // see also https://docs.wxwidgets.org/3.0/classwx_menu_item.html#a2b5d6bcb820b992b1e4709facbf6d4fb
    // TODO: Find workaround for GTK4
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_object_set (gtk_settings_get_default (), "gtk-menu-images", TRUE, NULL);
#endif

#if defined(__WXGTK20__) || defined(__WXGTK3__)
    // Suppress harmless GTK critical warnings from the GTK3/wxWidgets interaction.
    // These include widget allocation on hidden widgets and events on unrealized widgets.
    g_log_set_handler("Gtk", G_LOG_LEVEL_CRITICAL,
        [](const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
            if (message && (strstr(message, "gtk_widget_set_allocation") ||
                            strstr(message, "WIDGET_REALIZED_FOR_EVENT")))
                return;
            g_log_default_handler(log_domain, log_level, message, user_data);
        }, nullptr);
#endif

#ifdef WIN32
    //BBS set crash log folder
    CBaseException::set_log_folder(data_dir());
#endif

    wxGetApp().Bind(wxEVT_QUERY_END_SESSION, [this](auto & e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "received wxEVT_QUERY_END_SESSION";
        if (mainframe) {
            wxCloseEvent e2(wxEVT_CLOSE_WINDOW);
            e2.SetCanVeto(true);
            mainframe->GetEventHandler()->ProcessEvent(e2);
            if (e2.GetVeto()) {
                e.Veto();
                return;
            }
        }
        for (auto d : dialogStack)
            d->EndModal(wxID_ABORT);
    });

    // Verify resources path
    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format(_L("Resources path does not exist or is not a directory: %s"), resources_dir));

#ifdef __linux__
    if (! check_old_linux_datadir(GetAppName())) {
        std::cerr << "Quitting, user chose to move their data to new location." << std::endl;
        return false;
    }
#endif

    BOOST_LOG_TRIVIAL(info) << get_system_info();

// initialize label colors and fonts
    init_label_colours();
    init_fonts();
    wxGetApp().Update_dark_mode_flag();
    
#if defined(__WINDOWS__)
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    m_is_arm64 = false;
    if (hKernel32) {
        auto fnIsWow64Process2 = (LPFN_ISWOW64PROCESS2)GetProcAddress(hKernel32, "IsWow64Process2");
        if (fnIsWow64Process2) {
            USHORT processMachine = 0;
            USHORT nativeMachine = 0;
            if (fnIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
                if (nativeMachine == IMAGE_FILE_MACHINE_ARM64) {//IMAGE_FILE_MACHINE_ARM64
                    m_is_arm64 = true;
                }
                BOOST_LOG_TRIVIAL(info) << boost::format("processMachine architecture %1%, nativeMachine %2% m_is_arm64 %3%")%(int)(processMachine) %(int) nativeMachine %m_is_arm64;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << boost::format("IsWow64Process2 failed, set m_is_arm64 to %1%") %m_is_arm64;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(info) << boost::format("can not find IsWow64Process2, set m_is_arm64 to %1%") %m_is_arm64;
        }
    }
    else {
        BOOST_LOG_TRIVIAL(info) << boost::format("can not find kernel32, set m_is_arm64 to %1%") %m_is_arm64;
    }
#endif
    // Enable this to get the default Win32 COMCTRL32 behavior of static boxes.
//    wxSystemOptions::SetOption("msw.staticbox.optimized-paint", 0);
    // Enable this to disable Windows Vista themes for all wxNotebooks. The themes seem to lead to terrible
    // performance when working on high resolution multi-display setups.
//    wxSystemOptions::SetOption("msw.notebook.themed-background", 0);

//     Slic3r::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;
    if (is_editor()) {
        std::string msg = Slic3r::Http::tls_global_init();
        std::string ssl_cert_store = app_config->get("tls_accepted_cert_store_location");
        bool ssl_accept = app_config->get("tls_cert_store_accepted") == "yes" && ssl_cert_store == Slic3r::Http::tls_system_cert_store();

        if (!msg.empty() && !ssl_accept) {
            RichMessageDialog
                dlg(nullptr,
                    wxString::Format(_L("%s\nDo you want to continue?"), msg),
                    "PING Slicer", wxICON_QUESTION | wxYES_NO);
            dlg.ShowCheckBox(_L("Remember my choice"));
            if (dlg.ShowModal() != wxID_YES) return false;

            app_config->set("tls_cert_store_accepted",
                dlg.IsCheckBoxChecked() ? "yes" : "no");
            app_config->set("tls_accepted_cert_store_location",
                dlg.IsCheckBoxChecked() ? Slic3r::Http::tls_system_cert_store() : "");
        }
    }

    // !!! Initialization of UI settings as a language, application color mode, fonts... have to be done before first UI action.
    // Like here, before the show InfoDialog in check_older_app_config()

    // If load_language() fails, the application closes.
    load_language(wxString(), true);
#ifdef _MSW_DARK_MODE

#ifndef __WINDOWS__
    wxSystemAppearance app = wxSystemSettings::GetAppearance();
    GUI::wxGetApp().app_config->set("dark_color_mode", app.IsDark() ? "1" : "0");
    GUI::wxGetApp().app_config->save();
#endif // __APPLE__


    bool init_dark_color_mode = dark_mode();
    bool init_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
#ifdef __WINDOWS__
     NppDarkMode::InitDarkMode(init_dark_color_mode, init_sys_menu_enabled);
#endif // __WINDOWS__

#endif


#ifdef _MSW_DARK_MODE
    // app_config can be updated in check_older_app_config(), so check if dark_color_mode and sys_menu_enabled was changed
    if (bool new_dark_color_mode = dark_mode();
        init_dark_color_mode != new_dark_color_mode) {

#ifdef __WINDOWS__
        NppDarkMode::SetDarkMode(new_dark_color_mode);
#endif // __WINDOWS__

        init_label_colours();
        //update_label_colours_from_appconfig();
    }
    if (bool new_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
        init_sys_menu_enabled != new_sys_menu_enabled)
#ifdef __WINDOWS__
        NppDarkMode::SetSystemMenuForApp(new_sys_menu_enabled);
#endif
#endif

    if (m_last_config_version) {
        int last_major = m_last_config_version->maj();
        int last_minor = m_last_config_version->min();
        int last_patch = m_last_config_version->patch()/100;
        std::string studio_ver = SLIC3R_VERSION;
        int cur_major = atoi(studio_ver.substr(0,2).c_str());
        int cur_minor = atoi(studio_ver.substr(3,2).c_str());
        int cur_patch = atoi(studio_ver.substr(6,2).c_str());
        BOOST_LOG_TRIVIAL(info) << boost::format("last app version {%1%.%2%.%3%}, current version {%4%.%5%.%6%}")
            %last_major%last_minor%last_patch%cur_major%cur_minor%cur_patch;
        if ((last_major != cur_major)
            ||(last_minor != cur_minor)
            ||(last_patch != cur_patch)) {
            remove_old_networking_plugins();
        }
    }

    if(app_config->get("version") != SLIC3R_VERSION) {
        app_config->set("version", SLIC3R_VERSION);
    }

    SplashScreen * scrn = nullptr;
    if (app_config->get("show_splash_screen") == "true") {
        // make a bitmap with dark grey banner on the left side
        //BBS make BBL splash screen bitmap
        wxBitmap bmp = SplashScreen::MakeBitmap();
        // Detect position (display) to show the splash screen
        // Now this position is equal to the mainframe position
        wxPoint splashscreen_pos = wxDefaultPosition;
        if (app_config->has("window_mainframe")) {
            auto metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            if (metrics)
                splashscreen_pos = metrics->get_rect().GetPosition();
        }

        BOOST_LOG_TRIVIAL(info) << "begin to show the splash screen...";
        //BBS use BBL splashScreen
        scrn = new SplashScreen(bmp, wxSPLASH_CENTRE_ON_SCREEN | wxSPLASH_TIMEOUT, 1500, splashscreen_pos);
        wxYield();
        scrn->SetText(_L("Loading configuration")+ dots);
    }

    BOOST_LOG_TRIVIAL(info) << "loading systen presets...";
    preset_bundle = new PresetBundle();

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    preset_bundle->setup_directories();


    if (m_init_app_config_from_older)
        copy_older_config();

    if (is_editor()) {
#ifdef __WXMSW__
        if (app_config->get("associate_3mf") == "true")
            associate_files(L"3mf");
        if (app_config->get("associate_stl") == "true")
            associate_files(L"stl");
        if (app_config->get("associate_step") == "true") {
            associate_files(L"step");
            associate_files(L"stp");
        }
        associate_url(L"orcaslicer");

        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif // __WXMSW__

        preset_updater = new PresetUpdater();
        Bind(EVT_SLIC3R_VERSION_ONLINE, [this](const wxCommandEvent& evt) {
            if (this->plater_ != nullptr) {
                // this->plater_->get_notification_manager()->push_notification(NotificationType::NewAppAvailable);
                //BBS show msg box to download new version
               /* wxString tips = wxString::Format(_L("Click to download new version in default browser: %s"), version_info.version_str);
                DownloadDialog dialog(this->mainframe,
                    tips,
                    _L("New version of PING Slicer"),
                    false,
                    wxCENTER | wxICON_INFORMATION);


                dialog.SetExtendedMessage(extmsg);*/
                std::string skip_version_str = this->app_config->get("app", "skip_version");
                bool skip_this_version = false;
                if (!skip_version_str.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "new version = " << version_info.version_str << ", skip version = " << skip_version_str;
                    if (version_info.version_str <= skip_version_str) {
                        skip_this_version = true;
                    } else {
                        app_config->set("skip_version", "");
                        skip_this_version = false;
                    }
                }
                if (!skip_this_version
                    || evt.GetInt() != 0) {
                    UpdateVersionDialog dialog(this->mainframe);
                    wxString            extmsg = wxString::FromUTF8(version_info.description);
                    dialog.update_version_info(extmsg, version_info.version_str);
                    //dialog.update_version_info(version_info.description);
                    if (evt.GetInt() != 0) {
                        dialog.m_button_skip_version->Hide();
                    }
                    switch (dialog.ShowModal())
                    {
                    case wxID_YES:
                        wxLaunchDefaultBrowser(version_info.url);
                        break;
                    case wxID_NO:
                        break;
                    default:
                        ;
                    }
                }
            }
            });

        Bind(EVT_ENTER_FORCE_UPGRADE, [this](const wxCommandEvent& evt) {
                wxString      version_str = wxString::FromUTF8(this->app_config->get("upgrade", "version"));
                wxString      description_text = wxString::FromUTF8(this->app_config->get("upgrade", "description"));
                std::string   download_url = this->app_config->get("upgrade", "url");
                wxString tips = wxString::Format(_L("Click to download new version in default browser: %s"), version_str);
                DownloadDialog dialog(this->mainframe,
                    tips,
                    _L("The PING Slicer needs an upgrade"),
                    false,
                    wxCENTER | wxICON_INFORMATION);
                dialog.SetExtendedMessage(description_text);

                int result = dialog.ShowModal();
                switch (result)
                {
                 case wxID_YES:
                     wxLaunchDefaultBrowser(download_url);
                     break;
                 case wxID_NO:
                     wxGetApp().mainframe->Close(true);
                     break;
                 default:
                     wxGetApp().mainframe->Close(true);
                }
            });

        Bind(EVT_SHOW_NO_NEW_VERSION, [this](const wxCommandEvent& evt) {
            wxString msg = _L("This is the newest version.");
            InfoDialog dlg(nullptr, _L("Info"), msg);
            dlg.ShowModal();
        });

        Bind(EVT_SHOW_DIALOG, [this](const wxCommandEvent& evt) {
            wxString msg = evt.GetString();
            InfoDialog dlg(this->mainframe, _L("Info"), msg);
            dlg.Bind(wxEVT_DESTROY, [this](auto& e) {
                m_info_dialog_content = wxEmptyString;
            });
            dlg.ShowModal();
        });
    }
    else {
#ifdef __WXMSW__
        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif // __WXMSW__
    }

    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(true);

    preset_bundle->backup_user_folder();

    Bind(EVT_UPDATE_MACHINE_LIST, &GUI_App::on_update_machine_list, this);
    Bind(EVT_USER_LOGIN, &GUI_App::on_user_login, this);
    Bind(EVT_USER_LOGIN_HANDLE, &GUI_App::on_user_login_handle, this);
    Bind(EVT_CHECK_PRIVACY_VER, &GUI_App::on_check_privacy_update, this);
    Bind(EVT_CHECK_PRIVACY_SHOW, &GUI_App::show_check_privacy_dlg, this);

    Bind(EVT_SHOW_IP_DIALOG, &GUI_App::show_ip_address_enter_dialog_handler, this);


    std::map<std::string, std::string> extra_headers = get_extra_header();
    Slic3r::Http::set_extra_headers(extra_headers);

    // Orca: select network plugin version based on configured version string
    std::string configured_version = app_config->get_network_plugin_version();
    NetworkAgent::use_legacy_network = (configured_version == BAMBU_NETWORK_AGENT_VERSION_LEGACY);
    BOOST_LOG_TRIVIAL(info) << "Network plugin mode: "
        << (NetworkAgent::use_legacy_network ? ("legacy (version: " + std::string(BAMBU_NETWORK_AGENT_VERSION_LEGACY) + ")") : ("modern (version: " + configured_version + ")"));
    // Force legacy network plugin if debugger attached
    // See https://github.com/bambulab/BambuStudio/issues/6726
    /* if (!NetworkAgent::use_legacy_network) {
        bool debugger_attached = false;
#if defined(__WINDOWS__)
        debugger_attached = IsDebuggerPresent();
#elif defined(__WXOSX__) || defined(__linux__)
        debugger_attached = is_debugger_present();
#endif
        if (debugger_attached) {
            NetworkAgent::use_legacy_network = true;
            wxMessageBox("Force using legacy bambu networking plugin because debugger is attached! If the app terminates itself immediately, please delete installed plugin and try again!");
        }
    } */
    copy_network_if_available();
    on_init_network();

    if (m_agent && m_agent->is_user_login()) {
        enable_user_preset_folder(true);
    } else {
        enable_user_preset_folder(false);
    }

    // BBS if load user preset failed
    //if (loaded_preset_result != 0) {
        try {
            // Enable all substitutions (in both user and system profiles), but log the substitutions in user profiles only.
            // If there are substitutions in system profiles, then a "reconfigure" event shall be triggered, which will force
            // installation of a compatible system preset, thus nullifying the system preset substitutions.
            init_params->preset_substitutions = preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        }
        catch (const std::exception& ex) {
            show_error(nullptr, ex.what());
        }
    //}

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
    register_win32_dpi_event();
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN
    register_win32_device_notification_event();
#endif // WIN32

    // Let the libslic3r know the callback, which will translate messages on demand.
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);

    BOOST_LOG_TRIVIAL(info) << "create the main window";
    mainframe = new MainFrame();
    // hide settings tabs after first Layout
    if (is_editor()) {
        mainframe->select_tab(size_t(0));
    }

    sidebar().obj_list()->init();
    //sidebar().aux_list()->init_auxiliary();
    if (mainframe->m_project != nullptr)   // PING：專案頁已移除（m_project=nullptr）
        mainframe->m_project->init_auxiliary();

//     update_mode(); // !!! do that later
    SetTopWindow(mainframe);

    plater_->init_notification_manager();

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    if (is_gcode_viewer()) {
        mainframe->update_layout();
        if (plater_ != nullptr)
            // ensure the selected technology is ptFFF
            plater_->set_printer_technology(ptFFF);
    }
    else
        load_current_presets();

    if (plater_ != nullptr) {
        plater_->reset_project_dirty_initial_presets();
        plater_->update_project_dirty_from_presets();
        plater_->get_partplate_list().set_filament_count(preset_bundle->filament_presets.size());
    }

    // BBS:
#ifdef __WINDOWS__
    mainframe->topbar()->SaveNormalRect();
#endif
    mainframe->Show(true);
    BOOST_LOG_TRIVIAL(info) << "main frame firstly shown";

//#if BBL_HAS_FIRST_PAGE
    //BBS: set tp3DEditor firstly
    /*plater_->canvas3D()->enable_render(false);
    mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    scrn->SetText(_L("Loading Opengl resourses..."));
    plater_->select_view_3D("3D");
    //BBS init the opengl resource here
    Size canvas_size = plater_->canvas3D()->get_canvas_size();
    wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
    wxGetApp().init_opengl();
    plater_->canvas3D()->init();
    wxGetApp().imgui()->new_frame();
    plater_->canvas3D()->enable_render(true);
    plater_->canvas3D()->render();
    if (is_editor())
        mainframe->select_tab(size_t(0));*/
//#else
    //plater_->trigger_restore_project(1);
//#endif

    obj_list()->set_min_height();

    update_mode(); // update view mode after fix of the object_list size

#ifdef __APPLE__
   other_instance_message_handler()->bring_instance_forward();
#endif //__APPLE__

    Bind(EVT_HTTP_ERROR, &GUI_App::on_http_error, this);


    Bind(wxEVT_IDLE, [this](wxIdleEvent& event)
    {
        bool curr_studio_active = this->is_studio_active();
        if (m_studio_active != curr_studio_active) {
            if (curr_studio_active) {
                BOOST_LOG_TRIVIAL(info) << "studio is active, start to subscribe";
                if (m_agent) {
                    json j = json::object();
                    m_agent->start_subscribe("app");
                }
            } else {
                BOOST_LOG_TRIVIAL(info) << "studio is inactive, stop to subscribe";
                if (m_agent) {
                    json j = json::object();
                    m_agent->stop_subscribe("app");
                }
            }
            m_studio_active = curr_studio_active;
        }


        if (! plater_)
            return;

        // BBS
        //this->obj_manipul()->update_if_dirty();

        //use m_post_initialized instead
        //static bool update_gui_after_init = true;

        // An ugly solution to GH #5537 in which GUI_App::init_opengl (normally called from events wxEVT_PAINT
        // and wxEVT_SET_FOCUS before GUI_App::post_init is called) wasn't called before GUI_App::post_init and OpenGL wasn't initialized.
//#ifdef __linux__
//        if (!m_post_initialized && m_opengl_initialized) {
//#else
        // PING：app 已經在關（MainFrame::shutdown() 設了 closing）就不要再跑 post_init()。
        // PrusaSlicer 原本在 shutdown() 清掉 plater_ 擋住這條路（註解「to avoid any manipulations with
        // them from App->wxEVT_IDLE after of the mainframe closing」），BBS 把那行註解掉之後，
        // 「post_init 之前就關窗」（例：PING_QUOTE_SMOKE 暖機）會在關閉中的 app 上跑 post_init，
        // 它排的 CallAfter（設定精靈、Tab 樹更新）在主視窗刪掉後才執行 ⇒ 0xC0000005（牌 c-0919-QSM-01）。
        if (!m_post_initialized && !m_adding_script_handler && !is_closing()) {
//#endif
            m_post_initialized = true;
#ifdef WIN32
            this->mainframe->register_win32_callbacks();
#endif
            this->post_init();

            update_publish_status();
        }

        if (m_post_initialized && app_config->dirty())
            app_config->save();

    });

    m_initialized = true;

    flush_logs();

    BOOST_LOG_TRIVIAL(info) << "finished the gui app init";
    if (m_config_corrupted) {
        m_config_corrupted = false;
        show_error(nullptr,
                   _u8L(
                       "The PING Slicer configuration file may be corrupted and cannot be parsed.\nPING Slicer has attempted to recreate the "
                       "configuration file.\nPlease note, application settings will be lost, but printer profiles will not be affected."));
    }
    return true;
}

void GUI_App::copy_network_if_available()
{
    if (app_config->get("update_network_plugin") != "true")
        return;

    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    auto cache_folder = data_dir_path / "ota";
    std::string changelog_file = cache_folder.string() + "/network_plugins.json";

    std::string cached_version;
    if (boost::filesystem::exists(changelog_file)) {
        try {
            boost::nowide::ifstream ifs(changelog_file);
            json j;
            ifs >> j;
            if (j.contains("version"))
                cached_version = j["version"];
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": cached_version = " << cached_version;
        } catch (nlohmann::detail::parse_error& err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << changelog_file << " failed: " << err.what();
        }
    }

    if (cached_version.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": no version found in changelog, aborting copy";
        app_config->set("update_network_plugin", "false");
        return;
    }

    std::string network_library, player_library, live555_library, network_library_dst, player_library_dst, live555_library_dst;
#if defined(_MSC_VER) || defined(_WIN32)
    network_library = cache_folder.string() + "/bambu_networking.dll";
    player_library = cache_folder.string() + "/BambuSource.dll";
    live555_library = cache_folder.string() + "/live555.dll";
    network_library_dst = plugin_folder.string() + "/" + std::string(BAMBU_NETWORK_LIBRARY) + "_" + cached_version + ".dll";
    player_library_dst = plugin_folder.string() + "/BambuSource.dll";
    live555_library_dst = plugin_folder.string() + "/live555.dll";
#elif defined(__WXMAC__)
    network_library = cache_folder.string() + "/libbambu_networking.dylib";
    player_library = cache_folder.string() + "/libBambuSource.dylib";
    live555_library = cache_folder.string() + "/liblive555.dylib";
    network_library_dst = plugin_folder.string() + "/lib" + std::string(BAMBU_NETWORK_LIBRARY) + "_" + cached_version + ".dylib";
    player_library_dst = plugin_folder.string() + "/libBambuSource.dylib";
    live555_library_dst = plugin_folder.string() + "/liblive555.dylib";
#else
    network_library = cache_folder.string() + "/libbambu_networking.so";
    player_library = cache_folder.string() + "/libBambuSource.so";
    live555_library = cache_folder.string() + "/liblive555.so";
    network_library_dst = plugin_folder.string() + "/lib" + std::string(BAMBU_NETWORK_LIBRARY) + "_" + cached_version + ".so";
    player_library_dst = plugin_folder.string() + "/libBambuSource.so";
    live555_library_dst = plugin_folder.string() + "/liblive555.so";
#endif

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": checking network_library " << network_library << ", player_library " << player_library;
    if (!boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": create directory " << plugin_folder.string();
        boost::filesystem::create_directory(plugin_folder);
    }
    std::string error_message;
    if (boost::filesystem::exists(network_library)) {
        CopyFileResult cfr = copy_file(network_library, network_library_dst, error_message, false);
        if (cfr != CopyFileResult::SUCCESS) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Copying failed(" << cfr << "): " << error_message;
            return;
        }

        static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
        fs::permissions(network_library_dst, perms);
        fs::remove(network_library);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Copying network library from " << network_library << " to " << network_library_dst << " successfully.";

        app_config->set(SETTING_NETWORK_PLUGIN_VERSION, cached_version);
        app_config->save();
    }

    if (boost::filesystem::exists(player_library)) {
        CopyFileResult cfr = copy_file(player_library, player_library_dst, error_message, false);
        if (cfr != CopyFileResult::SUCCESS) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Copying failed(" << cfr << "): " << error_message;
            return;
        }

        static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
        fs::permissions(player_library_dst, perms);
        fs::remove(player_library);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Copying player library from " << player_library << " to " << player_library_dst << " successfully.";
    }

    if (boost::filesystem::exists(live555_library)) {
        CopyFileResult cfr = copy_file(live555_library, live555_library_dst, error_message, false);
        if (cfr != CopyFileResult::SUCCESS) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Copying failed(" << cfr << "): " << error_message;
            return;
        }

        static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
        fs::permissions(live555_library_dst, perms);
        fs::remove(live555_library);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": Copying live555 library from " << live555_library << " to " << live555_library_dst << " successfully.";
    }
    if (boost::filesystem::exists(changelog_file))
        fs::remove(changelog_file);
    app_config->set("update_network_plugin", "false");
}

bool GUI_App::on_init_network(bool try_backup)
{
    // PING: 強制不載入 Bambu 網路外掛(BambuNetwork DLL)，即使舊 config 殘留 installed_networking=true
    //        → 永不連 Bambu 雲端；走下方 L3469「無外掛」路徑。LAN 送印改用內建 PrintHost(Moonraker/Octoprint)
    auto should_load_networking_plugin = false;  // 原: app_config->get_bool("installed_networking")

    std::string config_version = app_config->get_network_plugin_version();

    if (should_load_networking_plugin) {
        if (config_version.empty()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": no version configured, need to download";
            m_networking_need_update = true;

            if (!m_device_manager)
                m_device_manager = new Slic3r::DeviceManager();
            if (!m_user_manager)
                m_user_manager = new Slic3r::UserManager();

            return false;
        }

        int load_agent_dll = Slic3r::NetworkAgent::initialize_network_module(false, config_version);
    __retry:
        if (!load_agent_dll) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, load dll ok";

            std::string loaded_version = Slic3r::NetworkAgent::get_version();
            if (app_config && !loaded_version.empty() && loaded_version != "00.00.00.00") {
                std::string config_version = app_config->get_network_plugin_version();
                std::string config_base    = extract_base_version(config_version);
                if (config_base != loaded_version) {
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": syncing config version from " << config_version << " to loaded "
                                            << loaded_version;
                    app_config->set(SETTING_NETWORK_PLUGIN_VERSION, loaded_version);
                    app_config->save();
                }
            }

            if (check_networking_version()) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, compatibility version";
                auto bambu_source = Slic3r::NetworkAgent::get_bambu_source_entry();
                if (!bambu_source) {
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": can not get bambu source module!";
                    m_networking_compatible = false;
                    if (should_load_networking_plugin) {
                        m_networking_need_update = true;
                    }
                }
            } else {
                if (try_backup) {
                    int result = Slic3r::NetworkAgent::unload_network_module();
                    BOOST_LOG_TRIVIAL(info) << "on_init_network, version mismatch, unload_network_module, result = " << result;
                    load_agent_dll = Slic3r::NetworkAgent::initialize_network_module(true, config_version);
                    try_backup     = false;
                    goto __retry;
                }
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, version dismatch, need upload network module";
                if (should_load_networking_plugin) {
                    m_networking_need_update = true;
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, load dll failed";
            if (should_load_networking_plugin) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, need upload network module";
                m_networking_need_update = true;
            }
        }
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", create network agent...");
    //std::string data_dir = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
    std::string data_directory = data_dir();

    // Register all printer agents before creating the network agent
    Slic3r::NetworkAgentFactory::register_all_agents();

    // m_agent = new Slic3r::NetworkAgent(data_directory);
    std::unique_ptr<Slic3r::NetworkAgent> agent_ptr = Slic3r::create_agent_from_config(data_directory, app_config);
    m_agent = agent_ptr.release();

    if (!m_device_manager)
        m_device_manager = new Slic3r::DeviceManager(m_agent);
    else
        m_device_manager->set_agent(m_agent);

    if (!m_user_manager)
        m_user_manager = new Slic3r::UserManager(m_agent);
    else
        m_user_manager->set_agent(m_agent);

    if (this->is_enable_multi_machine()) {
        if (!m_task_manager) {
            m_task_manager = new Slic3r::TaskManager(m_agent);
            m_task_manager->start();
        }

        m_device_manager->EnableMultiMachine(true);
    } else {
        m_device_manager->EnableMultiMachine(false);
    }

    //BBS set config dir
    if (m_agent) {
        m_agent->set_config_dir(data_directory);
    }
    //BBS start http log
    if (m_agent) {
        m_agent->init_log();
    }

    //BBS set cert dir
    if (m_agent)
        m_agent->set_cert_file(resources_dir() + "/cert", "slicer_base64.cer");

    init_http_extra_header();

    if (m_agent) {
        init_networking_callbacks();
        std::string country_code = app_config->get_country_code();
        m_agent->set_country_code(country_code);
        m_agent->start();
    }

    if (!should_load_networking_plugin) {
        int result = Slic3r::NetworkAgent::unload_network_module();
        BOOST_LOG_TRIVIAL(info) << "on_init_network, unload_network_module, result = " << result;

        if (!m_device_manager)
            m_device_manager = new Slic3r::DeviceManager();

        if (!m_user_manager)
            m_user_manager = new Slic3r::UserManager();
    }

    if (should_load_networking_plugin && m_networking_compatible && !NetworkAgent::use_legacy_network) {
        app_config->clear_remind_network_update_later();

        if (has_network_update_available()) {
            std::string latest = get_latest_network_version();

            bool should_prompt = !app_config->is_network_update_prompt_disabled()
                && !app_config->is_network_version_skipped(latest)
                && !app_config->should_remind_network_update_later();

            if (should_prompt) {
                CallAfter([this]() {
                    show_network_plugin_download_dialog(true);
                });
            }
        }
    }

    return true;
}

unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
        ));
}

void GUI_App::switch_printer_agent()
{
    if (!m_agent) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": no agent exists";
        return;
    }

    // Read printer_agent from config, falling back to default
    std::string effective_agent_id = ORCA_PRINTER_AGENT_ID;
    if (preset_bundle->is_bbl_vendor()) {
        effective_agent_id = BBL_PRINTER_AGENT_ID;
    } else {
        const DynamicPrintConfig& config = preset_bundle->printers.get_edited_preset().config;
        if (config.has("printer_agent")) {
            const std::string& value = config.option<ConfigOptionString>("printer_agent")->value;
            if (!value.empty())
                effective_agent_id = value;
        }
    }

    // Check if agent is registered
    if (!NetworkAgentFactory::is_printer_agent_registered(effective_agent_id)) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": unregistered agent ID '" << effective_agent_id
                                   << "', keeping current agent";
        // Keep current agent, don't switch
        return;
    }

    std::string current_agent_id;
    if (m_agent->get_printer_agent())
        current_agent_id = m_agent->get_printer_agent()->get_agent_info().id;

    if (current_agent_id != effective_agent_id) {
        std::string log_dir = data_dir();
        std::shared_ptr<ICloudServiceAgent> cloud_agent = m_agent->get_cloud_agent();

        // Create new printer agent via registry
        std::shared_ptr<IPrinterAgent> new_printer_agent =
            NetworkAgentFactory::create_printer_agent_by_id(effective_agent_id, cloud_agent, log_dir);

        if (!new_printer_agent) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": failed to create agent '" << effective_agent_id << "', keeping current agent";
            return;
        }

        // Swap the agent
        m_agent->set_printer_agent(new_printer_agent);
        sidebar().update_all_preset_comboboxes();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": printer agent switched to " << effective_agent_id;

        // Auto-switch MachineObject
        select_machine(effective_agent_id);
    }
}

void GUI_App::select_machine(const std::string& agent_id)
{
    // Skip for BBL agent for now - uses its own device discovery/selection
    // Orca todo: revisit in future if we want to support auto-switching for BBL printers
    if (agent_id == BBL_PRINTER_AGENT_ID) {
        return;
    }

    if (!m_device_manager || !preset_bundle) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": no device manager or preset bundle";
        return;
    }

    // Get config source (preset or physical printer)
    const auto& preset = preset_bundle->printers.get_edited_preset();
    const DynamicPrintConfig* host_cfg = &preset.config;

    std::string print_host = host_cfg->opt_string("print_host");
    if (print_host.empty()) {
        return;
    }
    std::string port = host_cfg->opt_string("printhost_port");

    // Generate dev_id from host and port
    std::string dev_id = MachineObject::dev_id_from_address(print_host, port);

    // Check if already exists by dev_id
    MachineObject* existing = m_device_manager->get_local_machine(dev_id);

    // If not found by dev_id, search by full_addr
    if (!existing) {
        auto local_machines = m_device_manager->get_local_machinelist();
        for (auto& [id, machine] : local_machines) {
            if (machine && machine->get_dev_ip() == dev_id) {
                existing = machine;
                break;
            }
        }
    }

    // If machine doesn't exist, create it first
    if (!existing) {
        BBLocalMachine machine;
        machine.dev_id = dev_id;
        // We use dev_id as dev_ip to store the address (host:port)
        machine.dev_ip = dev_id;
        machine.dev_name = dev_id;
        machine.printer_type = preset.config.opt_string("printer_model");
        auto access_code = preset.config.opt_string("printhost_apikey");
        // Orca expect non empty access code
        if (access_code.empty()) {
            access_code = "88888888";
        }

        existing = m_device_manager->insert_local_device(
            machine, "lan", "free", "", access_code);

        if (!existing) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to create machine dev_id=" << dev_id;
            return;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": created new machine dev_id=" << dev_id;
    }
    existing->local_use_ssl = boost::istarts_with(print_host, "https://");

    // Use MonitorPanel::select_machine() to trigger full selection flow
    // This reuses existing logic for machine switching (UI updates, callbacks, etc.)
    if (mainframe && mainframe->m_monitor) {
        mainframe->m_monitor->select_machine(dev_id);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": triggered select_machine for dev_id=" << dev_id;
    } else {
        // Fallback if MonitorPanel not available
        m_device_manager->set_selected_machine(dev_id);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": fallback set_selected_machine dev_id=" << dev_id;
    }
}

bool GUI_App::dark_mode()
{
#ifdef SUPPORT_DARK_MODE
#if __APPLE__
    // The check for dark mode returns false positive on 10.12 and 10.13,
    // which allowed setting dark menu bar and dock area, which is
    // is detected as dark mode. We must run on at least 10.14 where the
    // proper dark mode was first introduced.
    return wxPlatformInfo::Get().CheckOSVersion(10, 14) && mac_dark_mode();
#else
    return wxGetApp().app_config->get("dark_color_mode") == "1" ? true : check_dark_mode();
    //const unsigned luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    //return luma < 128;
#endif
#else
    //BBS disable DarkUI mode
    return false;
#endif
}

const wxColour GUI_App::get_label_default_clr_system()
{
    return dark_mode() ? wxColour(115, 220, 103) : wxColour(26, 132, 57);
}

const wxColour GUI_App::get_label_default_clr_modified()
{
    return dark_mode() ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
}

void GUI_App::init_label_colours()
{
    bool is_dark_mode = dark_mode();
    m_color_label_modified = is_dark_mode ? wxColour("#F1754E") : wxColour("#F1754E");
    m_color_label_sys      = is_dark_mode ? wxColour("#B2B3B5") : wxColour("#363636");

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    m_color_label_default           = is_dark_mode ? wxColour(250, 250, 250) : m_color_label_sys; // wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_label_default = is_dark_mode ? wxColour(230, 230, 230): wxSystemSettings::GetColour(/*wxSYS_COLOUR_HIGHLIGHTTEXT*/wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_default       = is_dark_mode ? wxColour("#36363B") : wxColour("#F1F1F1"); // ORCA row highlighting
    m_color_hovered_btn_label       = is_dark_mode ? wxColour(255, 255, 254) : wxColour(0,0,0);
    m_color_default_btn_label       = is_dark_mode ? wxColour(255, 255, 254): wxColour(0,0,0);
    m_color_selected_btn_bg         = is_dark_mode ? wxColour(84, 84, 91)   : wxColour(206, 206, 206);
#else
    m_color_label_default = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
#endif
    m_color_window_default          = is_dark_mode ? wxColour(43, 43, 43)   : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    StateColor::SetDarkMode(is_dark_mode);
}

void GUI_App::update_label_colours_from_appconfig()
{
    if (app_config->has("label_clr_sys")) {
        auto str = app_config->get("label_clr_sys");
        if (str != "")
            m_color_label_sys = wxColour(str);
    }

    if (app_config->has("label_clr_modified")) {
        auto str = app_config->get("label_clr_modified");
        if (str != "")
            m_color_label_modified = wxColour(str);
    }
}

void GUI_App::update_publish_status()
{
    // mainframe->show_publish_button(has_model_mall());
    // if (app_config->get("staff_pick_switch") == "true") {
    //     mainframe->m_webview->SendDesignStaffpick(has_model_mall());
    // }
}

bool GUI_App::has_model_mall()
{
    return false;  // PING: 隱藏 Bambu/MakerWorld 模型商城
}

void GUI_App::update_label_colours()
{
    for (Tab* tab : tabs_list)
        tab->update_label_colours();
}

#ifdef _WIN32
static bool is_focused(HWND hWnd)
{
    HWND hFocusedWnd = ::GetFocus();
    return hFocusedWnd && hWnd == hFocusedWnd;
}

static bool is_default(wxWindow* win)
{
    wxTopLevelWindow* tlw = find_toplevel_parent(win);
    if (!tlw)
        return false;

    return win == tlw->GetDefaultItem();
}
#endif

void GUI_App::UpdateDarkUI(wxWindow* window, bool highlited/* = false*/, bool just_font/* = false*/)
{
    if (wxButton *btn = dynamic_cast<wxButton*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
        else {
#ifdef _WIN32
            if (btn->GetId() == wxID_OK || btn->GetId() == wxID_CANCEL) {
                bool is_focused_button = false;
                bool is_default_button = false;

                if (!(btn->GetWindowStyle() & wxNO_BORDER)) {
                    btn->SetWindowStyle(btn->GetWindowStyle() | wxNO_BORDER);
                    highlited = true;
                }

                auto mark_button = [this, btn, highlited](const bool mark) {
                    btn->SetBackgroundColour(mark ? m_color_selected_btn_bg : highlited ? m_color_highlight_default : m_color_window_default);
                    btn->SetForegroundColour(mark ? m_color_hovered_btn_label :m_color_default_btn_label);
                    btn->Refresh();
                    btn->Update();
                };

                // hovering
                btn->Bind(wxEVT_ENTER_WINDOW, [mark_button](wxMouseEvent& event) { mark_button(true); event.Skip(); });
                btn->Bind(wxEVT_LEAVE_WINDOW, [mark_button, btn](wxMouseEvent& event) { mark_button(is_focused(btn->GetHWND())); event.Skip(); });
                // focusing
                btn->Bind(wxEVT_SET_FOCUS, [mark_button](wxFocusEvent& event) { mark_button(true); event.Skip(); });
                btn->Bind(wxEVT_KILL_FOCUS, [mark_button](wxFocusEvent& event) { mark_button(false); event.Skip(); });

                is_focused_button = is_focused(btn->GetHWND());
                is_default_button = is_default(btn);
                mark_button(is_focused_button);
            }
#endif
        }
    }

    if (Button* btn = dynamic_cast<Button*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
    }


    /*if (m_is_dark_mode != dark_mode() )
        m_is_dark_mode = dark_mode();*/

    if (m_is_dark_mode) {

        auto orig_col = window->GetBackgroundColour();
        auto bg_col = StateColor::darkModeColorFor(orig_col);
        // there are cases where the background color of an item is bright, specifically:
        // * the background color of a button: #EA4E16  -- 73
        if (bg_col != orig_col) {
            window->SetBackgroundColour(bg_col);
        }

        orig_col = window->GetForegroundColour();
        auto fg_col = StateColor::darkModeColorFor(orig_col);
        auto fg_l = StateColor::GetLightness(fg_col);

        auto color_difference = StateColor::GetColorDifference(bg_col, fg_col);

        // fallback and sanity check with LAB
        // color difference of less than 2 or 3 is not normally visible, and even less than 30-40 doesn't stand out
        if (color_difference < 10) {
            fg_col = StateColor::SetLightness(fg_col, 90);
        }
        // some of the stock colors have a lightness of ~49
        if (fg_l < 45) {
            fg_col = StateColor::SetLightness(fg_col, 70);
        }
        // at this point it shouldn't be possible that fg_col is the same as bg_col, but let's be safe
        if (fg_col == bg_col) {
            fg_col = StateColor::SetLightness(fg_col, 70);
        }

        window->SetForegroundColour(fg_col);
    }
    else {
        auto original_col = window->GetBackgroundColour();
        auto bg_col = StateColor::lightModeColorFor(original_col);

        if (bg_col != original_col) {
            window->SetBackgroundColour(bg_col);
        }

        original_col = window->GetForegroundColour();
        auto fg_col = StateColor::lightModeColorFor(original_col);

        if (fg_col != original_col) {
            window->SetForegroundColour(fg_col);
        }
    }
}

// recursive function for scaling fonts for all controls in Window
static void update_dark_children_ui(wxWindow* window, bool just_buttons_update = false)
{
    /*bool is_btn = dynamic_cast<wxButton*>(window) != nullptr;
    is_btn = false;*/
    if (!window) return;

    if (ScalableButton* btn = dynamic_cast<ScalableButton*>(window)) {
        btn->UpdateDarkUI();
    } else {
        wxGetApp().UpdateDarkUI(window);
    }

    auto children = window->GetChildren();
    for (auto child : children) {
        update_dark_children_ui(child);
    }
}

// Note: Don't use this function for Dialog contains ScalableButtons
void GUI_App::UpdateDarkUIWin(wxWindow* win)
{
    update_dark_children_ui(win);
}

void GUI_App::Update_dark_mode_flag()
{
    m_is_dark_mode = dark_mode();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": switch the current dark mode status to %1% ")%m_is_dark_mode;
}

void GUI_App::UpdateDlgDarkUI(wxDialog* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateFrameDarkUI(wxFrame* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited/* = false*/)
{
#ifdef __WINDOWS__
    UpdateDarkUI(dvc, highlited ? dark_mode() : false);
#ifdef _MSW_DARK_MODE
    //dvc->RefreshHeaderDarkMode(&m_normal_font);
    HWND hwnd;
    if (!dvc->HasFlag(wxDV_NO_HEADER)) {
        hwnd = (HWND) dvc->GenericGetHeader()->GetHandle();
        hwnd = GetWindow(hwnd, GW_CHILD);
        if (hwnd != NULL)
            NppDarkMode::SetDarkListViewHeader(hwnd);
        wxItemAttr attr;
        attr.SetTextColour(NppDarkMode::GetTextColor());
        attr.SetFont(m_normal_font);
        dvc->SetHeaderAttr(attr);
    }
#endif //_MSW_DARK_MODE
    if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);
    if (dvc->GetBorder() != wxBORDER_SIMPLE)
        dvc->SetWindowStyle(dvc->GetWindowStyle() | wxBORDER_SIMPLE);
#endif
}

void GUI_App::UpdateAllStaticTextDarkUI(wxWindow* parent)
{
#ifdef __WINDOWS__
    wxGetApp().UpdateDarkUI(parent);

    auto children = parent->GetChildren();
    for (auto child : children) {
        if (dynamic_cast<wxStaticText*>(child))
            child->SetForegroundColour(m_color_label_default);
    }
#endif
}

void GUI_App::init_fonts()
{
    // BBS: modify font
    m_small_font = Label::Body_10;
    m_bold_font = Label::Body_10.Bold();
    m_normal_font = Label::Body_10;

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/

    // wxSYS_OEM_FIXED_FONT and wxSYS_ANSI_FIXED_FONT use the same as
    // DEFAULT in wxGtk. Use the TELETYPE family as a work-around
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_small_font.GetPointSize());
}

void GUI_App::update_fonts(const MainFrame *main_frame)
{
    /* Only normal and bold fonts are used for an application rescale,
     * because of under MSW small and normal fonts are the same.
     * To avoid same rescaling twice, just fill this values
     * from rescaled MainFrame
     */
	if (main_frame == nullptr)
		main_frame = this->mainframe;
    m_normal_font   = Label::Body_14; // BBS: larger font size
    m_small_font    = m_normal_font;
    m_bold_font     = m_normal_font.Bold();
    m_link_font     = m_bold_font.Underlined();
    m_em_unit       = main_frame->em_unit();
    m_code_font.SetPointSize(m_small_font.GetPointSize());
}

void GUI_App::set_label_clr_modified(const wxColour& clr)
{
    return;
    //BBS
    /*
    if (m_color_label_modified == clr)
        return;
    m_color_label_modified = clr;
    const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
    app_config->save();
    */
}

void GUI_App::set_label_clr_sys(const wxColour& clr)
{
    return;
    //BBS
    /*
    if (m_color_label_sys == clr)
        return;
    m_color_label_sys = clr;
    const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
    app_config->save();
    */
}

bool GUI_App::get_side_menu_popup_status()
{
    return m_side_popup_status;
}

void GUI_App::set_side_menu_popup_status(bool status)
{
    m_side_popup_status = status;
}

std::string GUI_App::link_to_network_check()
{
    std::string url;
    std::string country_code = app_config->get_country_code();


    if (country_code == "US") {
        url = "https://status.bambulab.com";
    }
    else if (country_code == "CN") {
        url = "https://status.bambulab.cn";
    }
    else {
        url = "https://status.bambulab.com";
    }
    //wxLaunchDefaultBrowser(url);
    return url; // ORCA
}

std::string GUI_App::link_to_lan_only_wiki()
{
    std::string url;
    std::string country_code = app_config->get_country_code();

    if (country_code == "US") {
        url = "https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode";
    }
    else if (country_code == "CN") {
        url = "https://wiki.bambulab.com/zh/knowledge-sharing/enable-lan-mode";
    }
    else {
        url = "https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode";
    }
    //wxLaunchDefaultBrowser(url);
    return url; // ORCA
}

bool GUI_App::tabs_as_menu() const
{
    return false;
}

wxSize GUI_App::get_min_size() const
{
    return wxSize(76*m_em_unit, 49 * m_em_unit);
}

float GUI_App::toolbar_icon_scale(const bool is_limited/* = false*/) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    //return icon_sc;

    const std::string& auto_val = app_config->get("toolkit_size");

    if (auto_val.empty())
        return icon_sc;

    int int_val =  100;
    // correct value in respect to toolkit_size
    int_val = std::min(atoi(auto_val.c_str()), int_val);

    if (is_limited && int_val < 50)
        int_val = 50;

    return 0.01f * int_val * icon_sc;
}

void GUI_App::set_auto_toolbar_icon_scale(float scale) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    long int_val = std::min(int(std::lround(scale / icon_sc * 100)), 100);
    std::string val = std::to_string(int_val);

    app_config->set("toolkit_size", val);
}

// check user printer_presets for the containing information about "Print Host upload"
void GUI_App::check_printer_presets()
{
//BBS
#if 0
    std::vector<std::string> preset_names = PhysicalPrinter::presets_with_print_host_information(preset_bundle->printers);
    if (preset_names.empty())
        return;

    // BBS: remove "print host upload" message dialog
    preset_bundle->physical_printers.load_printers_from_presets(preset_bundle->printers);
#endif
}

void switch_window_pools();
void release_window_pools();

void GUI_App::recreate_GUI(const wxString &msg_name)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "recreate_GUI enter";
    m_is_recreating_gui = true;

    update_http_extra_header();

    mainframe->shutdown();
    ProgressDialog dlg(msg_name, msg_name, 100, nullptr, wxPD_AUTO_HIDE);
    dlg.Pulse();
    dlg.Update(10, _L("Rebuild") + dots);

    MainFrame *old_main_frame = mainframe;
    struct ClientData : wxClientData
    {
        ~ClientData() { release_window_pools(); }
    };
    old_main_frame->SetClientObject(new ClientData);

    switch_window_pools();
    mainframe = new MainFrame();
    if (is_editor())
        // hide settings tabs after first Layout
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    // Propagate model objects to object list.
    sidebar().obj_list()->init();
    //sidebar().aux_list()->init_auxiliary();
    //mainframe->m_auxiliary->init_auxiliary();
    SetTopWindow(mainframe);

    dlg.Update(30, _L("Rebuild") + dots);
    old_main_frame->Destroy();

    dlg.Update(80, _L("Loading current presets") + dots);
    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));
    load_current_presets();
    mainframe->Show(true);
    //mainframe->refresh_plugin_tips();

    dlg.Update(90, _L("Loading a mode view") + dots);

    obj_list()->set_min_height();
    update_mode();

    // clear previous hms query, so that the hms info can use different language
    if (hms_query) hms_query->clear_hms_info();

    //BBS: trigger restore project logic here, and skip confirm
    plater_->trigger_restore_project(1);

    // #ys_FIXME_delete_after_testing  Do we still need this  ?
//     CallAfter([]() {
//         // Run the config wizard, don't offer the "reset user profile" checkbox.
//         config_wizard_startup(true);
//     });


    update_publish_status();

    m_is_recreating_gui = false;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "recreate_GUI exit";
}

void GUI_App::system_info()
{
    //SysInfoDialog dlg;
    //dlg.ShowModal();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
}


void GUI_App::ShowUserGuide() {
    // BBS:Show NewUser Guide
    try {
        bool res = false;
        GuideFrame GuideDlg(this);
                //if (GuideDlg.IsFirstUse())
        res = GuideDlg.run();
if (res) {
            load_current_presets();
            update_publish_status();
            mainframe->refresh_plugin_tips();
            // BBS: remove SLA related message
        }
    } catch (std::exception &) {
        // wxMessageBox(e.what(), "", MB_OK);
    }
}

void GUI_App::ShowDownNetPluginDlg() {
    return;  // PING: 停用「下載 Bambu Network 外掛」對話框
    try {
        auto iter = std::find_if(dialogStack.begin(), dialogStack.end(), [](auto dialog) {
            return dynamic_cast<DownloadProgressDialog *>(dialog) != nullptr;
        });
        if (iter != dialogStack.end())
            return;
        DownloadProgressDialog dlg(_L("Downloading Bambu Network Plug-in"));
        dlg.ShowModal();
    } catch (std::exception &) {
        ;
    }
}

void GUI_App::ShowUserLogin(bool show)
{
    return;  // PING: 移除 Bambu 帳號登入對話框
    // BBS: User Login Dialog
    if (show) {
        try {
            if (!login_dlg)
                login_dlg = new ZUserLogin();
            else {
                delete login_dlg;
                login_dlg = new ZUserLogin();
            }
            login_dlg->ShowModal();
        } catch (std::exception &) {
            ;
        }
    } else {
        if (login_dlg)
            login_dlg->EndModal(wxID_OK);
    }
}


void GUI_App::ShowOnlyFilament() {
    // BBS:Show NewUser Guide
    try {
        bool       res = false;
        GuideFrame GuideDlg(this);
        GuideDlg.SetStartPage(GuideFrame::GuidePage::BBL_FILAMENT_ONLY);
        res = GuideDlg.run();
        if (res) {
            load_current_presets();

            // BBS: remove SLA related message
        }
    } catch (std::exception &) {
        // wxMessageBox(e.what(), "", MB_OK);
    }
}



// static method accepting a wxWindow object as first parameter
bool GUI_App::catch_error(std::function<void()> cb,
    //                       wxMessageDialog* message_dialog,
    const std::string& err /*= ""*/)
{
    if (!err.empty()) {
        if (cb)
            cb();
        //         if (message_dialog)
        //             message_dialog->(err, "Error", wxOK | wxICON_ERROR);
        show_error(/*this*/nullptr, err);
        return true;
    }
    return false;
}

// static method accepting a wxWindow object as first parameter
void fatal_error(wxWindow* parent)
{
    show_error(parent, "");
    //     exit 1; // #ys_FIXME
}

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
static void update_scrolls(wxWindow* window)
{
    wxWindowList::compatibility_iterator node = window->GetChildren().GetFirst();
    while (node)
    {
        wxWindow* win = node->GetData();
        if (dynamic_cast<wxScrollHelper*>(win) ||
            dynamic_cast<wxTreeCtrl*>(win) ||
            dynamic_cast<wxTextCtrl*>(win))
            NppDarkMode::SetDarkExplorerTheme(win->GetHWND());

        update_scrolls(win);
        node = node->GetNext();
    }
}
#endif //_MSW_DARK_MODE


#ifdef _MSW_DARK_MODE
void GUI_App::force_menu_update()
{
    NppDarkMode::SetSystemMenuForApp(app_config->get("sys_menu_enabled") == "1");
}
#endif //_MSW_DARK_MODE
#endif //__WINDOWS__

void GUI_App::force_colors_update()
{
#ifdef _MSW_DARK_MODE
#ifdef __WINDOWS__
    NppDarkMode::SetDarkMode(dark_mode());
    if (WXHWND wxHWND = wxToolTip::GetToolTipCtrl())
        NppDarkMode::SetDarkExplorerTheme((HWND)wxHWND);
    NppDarkMode::SetDarkTitleBar(mainframe->GetHWND());


    //NppDarkMode::SetDarkExplorerTheme((HWND)mainframe->m_settings_dialog.GetHWND());
    //NppDarkMode::SetDarkTitleBar(mainframe->m_settings_dialog.GetHWND());

#endif // __WINDOWS__
#endif //_MSW_DARK_MODE
    m_force_colors_update = true;
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings()
{
    update_label_colours();
    // Upadte UI colors before Update UI from settings
    if (m_force_colors_update) {
        m_force_colors_update = false;
        //UpdateDlgDarkUI(&mainframe->m_settings_dialog);
        //mainframe->m_settings_dialog.Refresh();
        //mainframe->m_settings_dialog.Update();

        if (mainframe) {
#ifdef __WINDOWS__
            mainframe->force_color_changed();
            update_scrolls(mainframe);
            update_scrolls(&mainframe->m_settings_dialog);
#endif //_MSW_DARK_MODE
            update_dark_children_ui(mainframe);
        }
    }

    if (mainframe) {mainframe->update_ui_from_settings();}
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": received wxEVT_CLOSE_WINDOW, trigger save for window_mainframe";
        window_pos_save(window, "mainframe");
        event.Skip();
    });

    if (window_pos_restore(window, "mainframe", default_maximized)) {
        on_window_geometry(window, [=]() {
            window_pos_sanitize(window);
        });
    } else {
        on_window_geometry(window, [=]() {
            window_pos_center(window);
        });
    }
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (3MF):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files) const
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
#ifdef __APPLE__
        _L("Choose one or more files (3MF/STEP/STL/SVG/OBJ/AMF/USD*/ABC/PLY):"),
#else
        _L("Choose one or more files (3MF/STEP/STL/SVG/OBJ/AMF):"),
#endif
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

void GUI_App::import_zip(wxWindow* parent, wxString& input_file) const
{
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
                        _L("Choose ZIP file") + ":",
                        from_u8(app_config->get_last_dir()), "",
                        file_wildcards(FT_ZIP), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::load_gcode(wxWindow* parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (GCODE/3MF):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_GCODE), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

wxString GUI_App::transition_tridid(int trid_id) const
{
    if (trid_id == VIRTUAL_TRAY_MAIN_ID || trid_id == VIRTUAL_TRAY_DEPUTY_ID)
    {
        assert(0);
        return _L("Ext");
    }

    wxString maping_dict[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };

    if (trid_id >= 128 * 4) {
        trid_id -= 128 * 4;
        int id_index = trid_id / 4;
        return wxString::Format("%s", maping_dict[id_index]);
    }
    else if (trid_id >= 0x80 && trid_id <= 0x87) { // n3s
        const char base = 'A' + (trid_id - 128);
        wxString prefix("HT-");
        prefix.append(base);
        return prefix;
    }
    else {
        int id_index = std::clamp((int)ceil(trid_id / 4), 0, 25);
        int id_suffix = trid_id % 4 + 1;
        return wxString::Format("%s%d", maping_dict[id_index], id_suffix);
    }
}

//BBS
void GUI_App::request_login(bool show_user_info)
{
    ShowUserLogin();

    if (show_user_info) {
        get_login_info();
    }
}

void GUI_App::get_login_info()
{
    if (m_agent) {
        if (m_agent->is_user_login()) {
            std::string login_cmd = m_agent->build_login_cmd();
            wxString strJS = wxString::Format("window.postMessage(%s)", login_cmd);
            GUI::wxGetApp().run_script(strJS);
        }
        else {
            // OrcaNetwork performs async refresh on startup; avoid clearing
            // persisted tokens when the UI polls before refresh completes.
            if (m_agent->get_version() != "orca_network") {
                m_agent->user_logout();
                std::string logout_cmd = m_agent->build_logout_cmd();
                wxString    strJS      = wxString::Format("window.postMessage(%s)", logout_cmd);
                GUI::wxGetApp().run_script(strJS);
            }
        }
        if(app_config->get_bool("installed_networking")) {
            mainframe->m_webview->SetLoginPanelVisibility(true);
        } else {
            mainframe->m_webview->SetLoginPanelVisibility(false);
        }
    }
}

bool GUI_App::is_user_login()
{
    if (m_agent) {
        return m_agent->is_user_login();
    }
    return false;
}


bool GUI_App::check_login()
{
    bool result = false;
    if (m_agent) {
        result = m_agent->is_user_login();
    }

    if (!result) {
        ShowUserLogin();
    }
    return result;
}

void GUI_App::request_user_handle(int online_login)
{
    auto evt = new wxCommandEvent(EVT_USER_LOGIN_HANDLE);
    evt->SetInt(online_login);
    wxQueueEvent(this, evt);
}

void GUI_App::request_user_login(int online_login)
{
    auto evt = new wxCommandEvent(EVT_USER_LOGIN);
    evt->SetInt(online_login);
    wxQueueEvent(this, evt);
}

void GUI_App::request_user_logout()
{
    if (m_agent && m_agent->is_user_login()) {
        // Update data first before showing dialogs
        m_agent->user_logout(true);
        m_agent->set_user_selected_machine("");
        /* delete old user settings */
        bool     transfer_preset_changes = false;
        wxString header = _L("Some presets are modified.") + "\n" +
            _L("You can keep the modified presets to the new project, discard or save changes as new presets.");
        wxGetApp().check_and_keep_current_preset_changes(_L("User logged out"), header, ActionButtons::KEEP | ActionButtons::SAVE, &transfer_preset_changes);

        m_device_manager->clean_user_info();
        remove_user_presets();
        enable_user_preset_folder(false);
        preset_bundle->load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::Enable);
        mainframe->update_side_preset_ui();

        GUI::wxGetApp().stop_sync_user_preset();
    }
}

int GUI_App::request_user_unbind(std::string dev_id)
{
    int result = -1;
    if (m_agent) {
        result = m_agent->unbind(dev_id);
        BOOST_LOG_TRIVIAL(info) << "request_user_unbind, dev_id = " << dev_id << ", result = " << result;
        return result;
    }
    return result;
}

// ---- PING 照片磚：工作室匯出時的自動選機（Eric 2026-07-29 裁 A 案）----
// 依工作室送來的 mode（dual/quad）＋口徑，從 preset bundle「實資料」解析目標同進照片磚機：
// printer_model 含「同進照片磚」＋ dual→FD／quad→FF 家族 ＋ printer_variant 對口徑。
// 不寫死機名＝日後加新照片磚機（或 C 案準備頁整合要列可選機）都吃同一來源。
/* 機型名去掉變體字尾＝「系列」。與選機頁 `resources/web/guide/21/21.js` 的
   `PING_VARIANT_SUFFIX` 同一條規則（那邊是分組用，這邊是配對用）——**改一邊要改兩邊**，
   網頁端引用不到 C++ 的常數。
   例：`FF800 同進` → `FF800`／`FF800 同進照片磚` → `FF800`／`FD450 Pro 同進` → `FD450 Pro`。 */
static std::string ping_printer_series(const std::string& printer_model)
{
    static const std::string SUFFIXES[] = { " 同進照片磚", " 單料頭", " 單噴頭", " 同進", " 3in1", " 關門" };
    for (const std::string& suffix : SUFFIXES) {
        const size_t n = suffix.size();
        if (printer_model.size() > n && printer_model.compare(printer_model.size() - n, n, suffix) == 0)
            return printer_model.substr(0, printer_model.size() - n);
    }
    return printer_model;
}

struct PingPhotoTilePrinter
{
    bool        known_mode       = false; // mode 欄位可辨識（舊版網頁沒帶＝false，行為照舊）
    bool        already_selected = false; // 目前機器（含其衍生 user preset）已符合＝不切不打擾
    std::string preset_name;              // 需要切換時的目標；解析不到＝空
    /* 2026-09-07：配到的是**別的系列**的照片磚機（例：FD450 Pro 同進 沒有專屬照片磚版本，
       只好用 FD300 同進照片磚）。列印範圍與起始碼會是那一台的 ⇒ 要在通知裡講出來，不能靜默。 */
    bool        cross_series     = false;
    /* 2026-09-13 Eric 裁「甲」（牌 c-0913-PTI-01）：目前選中的是**脫鉤的自訂 preset**
       （非系統、又找不到系統父 preset）時不算「已在照片磚機上」，照常找系統照片磚機切過去；
       這裡記原本那台的名字，給交付通知說明用。空＝沒有發生這件事。 */
    std::string detached_from;
    /* 🗑 2026-09-07 移除 `exists_but_hidden`（Eric 裁 #99 Q3 甲）：那個欄位是 0815 A 案的產物，
       用來分辨「有這台機但使用者沒在選機清單勾」。照片磚機已從選機清單整批移除 ⇒ 它永遠是
       「沒勾」，那條分支恆真、訊息也永遠是錯的（叫人去一個已經沒有照片磚分頁的地方勾）。 */
};

static PingPhotoTilePrinter ping_resolve_photo_tile_printer(const std::string& mode, const std::string& nozzle)
{
    PingPhotoTilePrinter out;
    if ((mode != "dual" && mode != "quad") || nozzle.empty())
        return out;
    out.known_mode = true;

    // C-1：照片磚機的判定收斂到 PhotoTileCapability（單一來源），這裡只再加
    // 「家族要對得上 mode」與「口徑要等於網頁送來的值」兩個本流程專屬條件。
    // 判準若要改（例如日後 preset 欄位化），只改 PhotoTileCapability 一處。
    const std::string family  = mode == "dual" ? "FD" : "FF";
    const auto        matches = [&](const Preset& preset) {
        const PhotoTileCapability cap = photo_tile_capability_of(preset);
        if (!cap.is_photo_tile || cap.family != family)
            return false;
        const ConfigOptionString* pv = preset.config.option<ConfigOptionString>("printer_variant");
        return pv != nullptr && pv->value == nozzle;
    };

    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return out;
    const Preset& selected = bundle->printers.get_selected_preset();
    if (matches(selected)) {
        /* 【2026-09-13 Eric 裁「甲」・牌 c-0913-PTI-01】脫鉤的自訂 preset 不算「已在照片磚機上」。
           實錄（T043 驗收截圖）：選中的「FD300 同進照片磚」是 inherits 為空的 user preset
           （.info base_id＝系統 FD300 同進照片磚 0.4 nozzle，衍生後脫鉤、名字又改成機型名）。
           PhotoTileCapability 只看 printer_model ⇒ 它照樣被判成照片磚機 ⇒ 原本這裡回 already_selected
           ⇒ 不切機、線材修正也不跑。但照片磚專用線材與照片磚製程的 compatible_printers 是**按系統
           preset 名字**比對的，脫鉤 preset 名字對不上、又沒有系統父 preset 可回退 ⇒ 料槽全「不支援」、
           製程只剩 Orca 內建的 Default Setting。擋下只會讓人卡住 ⇒ 改成照常往下找系統照片磚機切過去。
           「有系統父 preset 的衍生 user preset」維持原行為：相容性會回退到父名，沒有這個問題。
           判定「是不是照片磚機」仍只在 PhotoTileCapability 一處；「脫鉤與否」是本流程專屬條件，放這裡。 */
        const Preset* parent   = bundle->printers.get_preset_parent(selected);
        const bool    detached = !selected.is_system && (parent == nullptr || !parent->is_system);
        if (!detached) {
            out.already_selected = true;
            return out;
        }
        out.detached_from = selected.name;
    }
    /* 【2026-09-07 Eric 裁 #99 Q3 甲——**取代** 2026-08-15 的 A 案】不再要求 `is_visible`。
       0815 A 案（只認使用者已加入的機型）的理由是「不要自動切到一台他沒勾選、可能根本沒有的
       機器」，**那條的前提是「照片磚機是使用者要自己在選機清單裡挑的機器」**。
       0907 之後前提沒了：照片磚機已從「選擇 3D 列印機」整批移除（guide/21・24 的
       `PingIsPhotoTileModel`），它是照片磚功能的內部載體（64 個虛擬料槽＋零回抽），
       **永遠不會是 is_visible** ⇒ 沿用舊判準等於照片磚從此配不到任何機型。
       ⇒ 改成「找得到就用」，安裝交給 `ping_install_photo_tile_printer()` 在切換前當場做。
       0815 想防的事沒有回來：那時的風險是「整組裝八台又自動切過去」，現在**一次只裝一台、
       而且是他這張磚真正要用的那一台**。 */
    /* 🔴 2026-09-07 順手修掉一個同型的舊 bug：原本是「掃到第一個家族對得上的就用」，
       而 printer 預設集是**按名字排序**的（`Preset::operator<` 比 name）⇒ 四料那一組永遠先掃到
       `FF600 同進照片磚`（'6' < '8'）⇒ **手上是 FF800 同進的人，做四料照片磚一律被切到 FF600**
       ——床 600 不是 800，而且畫面上只會說「已自動切換機型」，看不出換錯了。
       ⇒ 改成兩段：**同系列優先**（FF800 同進 → FF800 同進照片磚），沒有同系列的才退回家族比對，
       而且退回時把 `cross_series` 立起來讓上層講出來（例：FD450 Pro 同進 目前沒有專屬照片磚版本）。 */
    const std::string cur_series =
        ping_printer_series(bundle->printers.get_selected_preset().config.opt_string("printer_model"));
    const Preset* family_hit = nullptr;
    for (const Preset& preset : bundle->printers) {
        if (!preset.is_system || !matches(preset))
            continue;
        const ConfigOptionString* pm = preset.config.option<ConfigOptionString>("printer_model");
        if (pm != nullptr && !cur_series.empty() && ping_printer_series(pm->value) == cur_series) {
            out.preset_name = preset.name;      // 同系列＝首選，直接收工
            return out;
        }
        if (family_hit == nullptr)
            family_hit = &preset;               // 家族對得上的第一台，當退路
    }
    if (family_hit != nullptr) {
        out.preset_name  = family_hit->name;
        out.cross_series = true;
    }
    return out;
}

/* C-2：回推工作室頁的 JS 字串跳脫（值會被拼進 RunScript 的單行腳本）。
   UTF-8 位元組原樣保留；'<' 也跳脫＝保守擋 </script> 類序列。 */
static std::string ping_js_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (const char ch : s) {
        const unsigned char c = (unsigned char) ch;
        switch (c) {
        case '"':  out += "\\\"";  break;
        case '\\': out += "\\\\";  break;
        case '\n': out += "\\n";   break;
        case '\r': out += "\\r";   break;
        case '<':  out += "\\x3c"; break;
        default:
            if (c < 0x20) { char buf[8]; ::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
            else          out += ch;
        }
    }
    return out;
}

/* base64 解碼走這支（AIP 第二班 2026-10-06 真 App 驗收：照片磚頁送一塊 "abc" ⇒ App 當場閃退）。
   beast 的 base64::decode 只保證寫在 decoded_size(n)＝n/4*3 以內，前提是 n 為 4 的倍數（beast 註明 requires n&3==0）；
   不是的話會多寫 1～2 byte 到緩衝區外，n < 4 時緩衝區是空的＝寫到空指標。
   ⇒ 長度不是 4 的倍數就不解、回空的：呼叫端原本的「解不開／不完整」判斷（解出來是空的、長度不符）照原路擋下、照原句回報。
   回傳 first＝解出的位元組，second＝beast 讀到第幾個字元（呼叫端用它檢查後面只剩 '='）。 */
static std::pair<std::vector<unsigned char>, size_t> ping_base64_decode(const std::string& encoded)
{
    if (encoded.empty() || encoded.size() % 4 != 0)
        return {};
    std::vector<unsigned char> out(boost::beast::detail::base64::decoded_size(encoded.size()));
    const auto r = boost::beast::detail::base64::decode(out.data(), encoded.data(), encoded.size());
    out.resize(r.first);
    return {std::move(out), r.second};
}

/* AIP 第二班（開發中清單 #22）：照片磚頁分塊送來一個檔——「貼回的 AI 圖」與「我的款式」存檔共用這三段。
   規則照 phototile_matlib_save_* 抄（材料庫那支照出貨的樣子不動）：①連號 ②塊數 ③總長度 ④上限，另加壞 base64 一道；
   中途壞了只記原因、後面的塊一律不收，到 end 才回報一次——不回報兩次，也絕不半套落檔。
   what＝訊息裡的主詞（例「貼回的 AI 圖」）、undone＝沒做成的結果（例「這次沒有換圖」）。 */
static void photo_tile_upload_begin(PhotoTileUpload& up, size_t size, size_t chunks, size_t max_bytes, size_t max_chunks,
                                    const std::string& what, const std::string& undone)
{
    up = PhotoTileUpload();
    if (size == 0 || size > max_bytes || chunks == 0 || chunks > max_chunks || chunks > size) {        // ④上限
        const std::string limit = max_bytes >= 1048576 ? std::to_string(max_bytes / 1048576) + " MB" : std::to_string(max_bytes / 1024) + " KB";
        up.state = 2;
        up.fail  = what + "的大小或分塊數不合法（上限 " + limit + "），" + undone + "。";
        BOOST_LOG_TRIVIAL(warning) << "PhotoTile 收件：拒收 size=" << size << ", chunks=" << chunks;
        return;
    }
    up.buffer.reserve(size);
    up.expected_size   = size;
    up.expected_chunks = chunks;
    up.state           = 1;
}

static void photo_tile_upload_chunk(PhotoTileUpload& up, size_t index, const std::string& encoded,
                                    const std::string& what, const std::string& undone)
{
    if (up.state != 1)
        return;                                        // 閒置或已作廢：不收（原因等 end 一次回報）
    const auto fail = [&](const char* why) {
        up.state = 2;
        up.fail  = what + why + undone + "。";
        up.buffer.clear();
    };
    if (index != up.next_chunk || encoded.empty()) {   // ①連號
        fail("的分塊亂序或遺失，");
        return;
    }
    const auto [decoded, consumed] = ping_base64_decode(encoded);
    size_t pad = 0;                                    // 壞 base64 一律擋（判法同 phototile_matlib_save_chunk）
    for (size_t k = consumed; k < encoded.size(); ++k) {
        if (encoded[k] == '=') ++pad; else { pad = 99; break; }
    }
    if (encoded.size() % 4 != 0 || pad > 2 || decoded.empty()) {
        fail("的分塊內容解不開，");
        return;
    }
    if (up.buffer.size() + decoded.size() > up.expected_size) {   // ③總長度
        fail("的長度超過宣告，");
        return;
    }
    up.buffer.insert(up.buffer.end(), decoded.begin(), decoded.end());
    ++up.next_chunk;
}

// 收尾：成功回 true、內容交給 bytes；失敗回 false、why＝要給畫面的那一句。不論成敗，收件狀態一律歸零。
static bool photo_tile_upload_end(PhotoTileUpload& up, std::vector<unsigned char>& bytes, std::string& why,
                                  const std::string& what, const std::string& undone)
{
    const int    state     = up.state;
    const bool   count_ok  = up.next_chunk == up.expected_chunks;   // ②塊數
    const size_t want_size = up.expected_size;
    why = up.fail;
    bytes.clear();
    bytes.swap(up.buffer);
    up = PhotoTileUpload();
    if (state == 2)
        return false;
    if (state != 1)               why = what + "沒有開頭就結束，" + undone + "。";
    else if (!count_ok)           why = what + "的分塊數不符，" + undone + "。";
    else if (bytes.size() != want_size) why = what + "的總長度不符，" + undone + "。";   // ③總長度
    else                          return true;
    bytes.clear();
    return false;
}

/* 原子寫檔（照 phototile_matlib_save_end 那段抄、多一道讀回比對）：先寫 .tmp → 讀回逐位元組比對 →
   keep_bak 時把舊檔複製成 .bak（備份失敗只記 log、不擋）→ Slic3r::rename_file 換上。失敗回 false＝原檔未動。
   ⚠ 系統錯誤訊息只進 log、不進畫面（Windows 給的是本機字碼頁，不是 UTF-8）。 */
static bool photo_tile_write_file_atomic(const boost::filesystem::path& path, const std::vector<unsigned char>& bytes, bool keep_bak)
{
    boost::system::error_code ec;
    boost::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(warning) << "PhotoTile：建資料夾失敗 " << path.parent_path().string() << " err=" << ec.message();
        return false;
    }
    const boost::filesystem::path tmp_path(path.string() + ".tmp");
    bool write_ok = false;
    {
        boost::nowide::ofstream output(tmp_path.string(), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        write_ok = output.good();
    }
    if (write_ok) {
        std::vector<unsigned char> back(bytes.size());
        boost::nowide::ifstream input(tmp_path.string(), std::ios::binary);
        write_ok = input && input.read(reinterpret_cast<char*>(back.data()), static_cast<std::streamsize>(back.size())) &&
                   input.peek() == std::char_traits<char>::eof() && back == bytes;
    }
    if (!write_ok) {
        boost::filesystem::remove(tmp_path, ec);
        BOOST_LOG_TRIVIAL(warning) << "PhotoTile：暫存檔寫入或讀回不符 " << tmp_path.string();
        return false;
    }
    if (keep_bak && boost::filesystem::exists(path, ec)) {
        boost::filesystem::copy_file(path, boost::filesystem::path(path.string() + ".bak"),
                                     boost::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            BOOST_LOG_TRIVIAL(warning) << "PhotoTile：備份舊檔失敗 " << path.string() << ".bak err=" << ec.message();
    }
    if (const std::error_code rc = Slic3r::rename_file(tmp_path.string(), path.string())) {
        boost::filesystem::remove(tmp_path, ec);
        BOOST_LOG_TRIVIAL(warning) << "PhotoTile：換上新檔失敗 " << path.string() << " err=" << rc.message();
        return false;
    }
    return true;
}

void GUI_App::step_repair_page_result(bool ok, const std::string& message, const std::string& path)
{
    const std::string js = std::string("window.PINGStepRepair && window.PINGStepRepair.hostResult({ok:")
        + (ok ? "true" : "false") + ",message:\"" + ping_js_escape(message)
        + "\",path:\"" + ping_js_escape(path) + "\"});";
    CallAfter([this, js] {
        if (mainframe && mainframe->m_webview && mainframe->m_webview->IsStepRepairPage())
            mainframe->m_webview->RunScript(from_u8(js));
    });
}

std::string GUI_App::handle_web_request(std::string cmd)
{
    try {
        //BBS use nlohmann json format
        std::stringstream ss(cmd), oss;
        pt::ptree root, response;
        pt::read_json(ss, root);
        if (root.empty())
            return "";

        boost::optional<std::string> sequence_id = root.get_optional<std::string>("sequence_id");
        boost::optional<std::string> command = root.get_optional<std::string>("command");
        if (command.has_value()) {
            std::string command_str = command.value();
            if (command_str.compare("request_project_download") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_project_download(project_id.value());
                    }
                }
            }
            else if (command_str.compare("open_project") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_open_project(project_id.value());
                    }
                }
            }
            else if (command_str.compare("get_login_info") == 0) {
                CallAfter([this] {
                        get_login_info();
                    });
            }
            else if (command_str.compare("homepage_login_or_register") == 0) {
                CallAfter([this] {
                    this->request_login(true);
                });
            }
            else if (command_str.compare("homepage_logout") == 0) {
                CallAfter([this] {
                    wxGetApp().request_user_logout();
                });
            }
            else if (command_str.compare("homepage_modeldepot") == 0) {
                CallAfter([this] {
                    wxGetApp().open_mall_page_dialog();
                });
            }
            else if (command_str.compare("homepage_newproject") == 0) {
                this->request_open_project("<new>");
            }
            else if (command_str.compare("homepage_openproject") == 0) {
                this->request_open_project({});
            }
            else if (command_str.compare("homepage_phototile") == 0) {
                CallAfter([this] { open_photo_tile(); });
            }
            else if (command_str.compare("homepage_step_repair") == 0) {
                CallAfter([this] { open_step_repair(); });
            }
            else if (command_str.compare("step_repair_home") == 0) {
                if (!mainframe || !mainframe->m_webview || !mainframe->m_webview->IsStepRepairPage())
                    return "";
                m_step_repair_export_buffer.clear();
                m_step_repair_export_expected_size = 0;
                m_step_repair_export_expected_chunks = 0;
                m_step_repair_export_next_chunk = 0;
                m_step_repair_export_active = false;
                m_step_repair_suggested_name.clear();
                CallAfter([this] {
                    if (mainframe && mainframe->m_webview)
                        mainframe->m_webview->ShowHomepage();
                });
            }
            else if (command_str.compare("step_repair_export_begin") == 0) {
                constexpr size_t max_step_repair_bytes = 256ULL * 1024ULL * 1024ULL;
                constexpr size_t max_step_repair_chunks = 8192;
                const size_t expected_size = root.get<size_t>("data.size", 0);
                const size_t expected_chunks = root.get<size_t>("data.chunks", 0);
                const int protocol_version = root.get<int>("data.protocol_version", 0);
                const std::string core_version = root.get<std::string>("data.core_version", "");
                const std::string decision = root.get<std::string>("data.decision", "");
                const std::string proposed_name = root.get<std::string>("data.filename", "");

                m_step_repair_export_buffer.clear();
                m_step_repair_export_expected_size = 0;
                m_step_repair_export_expected_chunks = 0;
                m_step_repair_export_next_chunk = 0;
                m_step_repair_export_active = false;
                m_step_repair_suggested_name.clear();

                if (!mainframe || !mainframe->m_webview || !mainframe->m_webview->IsStepRepairPage()) {
                    BOOST_LOG_TRIVIAL(warning) << "Rejected STEP repair export outside its pinned page";
                    return "";
                }
                /* ④ 契約 §3：頁面回報的 core／protocol 必須與 manifest 完全相同。
                   manifest 本身不合格（pin.valid == false）就直接不放行——不自動降版、不猜。 */
                const StepRepairPin& pin = step_repair_pin();
                if (!pin.valid || protocol_version != pin.protocol_version || core_version != pin.core_version ||
                    decision != "repair-accepted" ||
                    expected_size == 0 || expected_size > max_step_repair_bytes ||
                    expected_chunks == 0 || expected_chunks > max_step_repair_chunks || proposed_name.size() > 240) {
                    BOOST_LOG_TRIVIAL(warning) << "Rejected invalid STEP repair export: core=" << core_version
                                               << ", protocol=" << protocol_version << ", size=" << expected_size
                                               << ", chunks=" << expected_chunks << ", decision=" << decision;
                    step_repair_page_result(false, "修補檔的版本或大小不符合安全規則，原始檔未變更。");
                    return "";
                }

                wxFileName safe_name(from_u8(proposed_name));
                wxString stem = safe_name.GetName();
                if (stem.IsEmpty())
                    stem = "model";
                if (!stem.EndsWith("_修補"))
                    stem += "_修補";
                safe_name.Assign(stem + ".step");
                m_step_repair_suggested_name = into_u8(safe_name.GetFullName());
                m_step_repair_export_buffer.reserve(expected_size);
                m_step_repair_export_expected_size = expected_size;
                m_step_repair_export_expected_chunks = expected_chunks;
                m_step_repair_export_active = true;
            }
            else if (command_str.compare("step_repair_export_chunk") == 0) {
                if (!m_step_repair_export_active || !mainframe || !mainframe->m_webview ||
                    !mainframe->m_webview->IsStepRepairPage())
                    return "";

                const size_t index = root.get<size_t>("data.index", size_t(-1));
                const std::string encoded = root.get<std::string>("data.payload", "");
                if (index != m_step_repair_export_next_chunk || encoded.empty() || encoded.size() > 300ULL * 1024ULL) {
                    BOOST_LOG_TRIVIAL(warning) << "STEP repair chunk out of sequence or oversized: expected="
                                               << m_step_repair_export_next_chunk << ", got=" << index;
                    m_step_repair_export_active = false;
                    m_step_repair_export_buffer.clear();
                    step_repair_page_result(false, "修補檔傳輸不完整，原始檔未變更。");
                    return "";
                }

                std::vector<unsigned char> decoded(boost::beast::detail::base64::decoded_size(encoded.size()));
                const auto result = boost::beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
                decoded.resize(result.first);
                const size_t trailing_padding = encoded.size() - result.second;
                const bool valid_padding = encoded.size() % 4 == 0 && trailing_padding <= 2 &&
                    std::all_of(encoded.begin() + result.second, encoded.end(), [](char value) { return value == '='; }) &&
                    (trailing_padding == 0 || index + 1 == m_step_repair_export_expected_chunks);
                if (!valid_padding || decoded.empty() ||
                    m_step_repair_export_buffer.size() + decoded.size() > m_step_repair_export_expected_size) {
                    m_step_repair_export_active = false;
                    m_step_repair_export_buffer.clear();
                    step_repair_page_result(false, "修補檔資料無法驗證，原始檔未變更。");
                    return "";
                }
                m_step_repair_export_buffer.insert(m_step_repair_export_buffer.end(), decoded.begin(), decoded.end());
                ++m_step_repair_export_next_chunk;
            }
            else if (command_str.compare("step_repair_export_end") == 0) {
                if (!m_step_repair_export_active || !mainframe || !mainframe->m_webview ||
                    !mainframe->m_webview->IsStepRepairPage() ||
                    m_step_repair_export_next_chunk != m_step_repair_export_expected_chunks ||
                    m_step_repair_export_buffer.size() != m_step_repair_export_expected_size) {
                    m_step_repair_export_active = false;
                    m_step_repair_export_buffer.clear();
                    step_repair_page_result(false, "修補檔傳輸尚未完成，原始檔未變更。");
                    return "";
                }

                static constexpr char step_signature[] = "ISO-10303-21;";
                const size_t prefix_size = std::min<size_t>(m_step_repair_export_buffer.size(), 256);
                const auto signature = std::search(
                    m_step_repair_export_buffer.begin(), m_step_repair_export_buffer.begin() + prefix_size,
                    std::begin(step_signature), std::end(step_signature) - 1);
                if (signature == m_step_repair_export_buffer.begin() + prefix_size) {
                    m_step_repair_export_active = false;
                    m_step_repair_export_buffer.clear();
                    step_repair_page_result(false, "修補結果不是有效的 STEP 交換檔，原始檔未變更。");
                    return "";
                }

                std::vector<unsigned char> step_bytes;
                step_bytes.swap(m_step_repair_export_buffer);
                const std::string suggested_name = m_step_repair_suggested_name;
                m_step_repair_export_expected_size = 0;
                m_step_repair_export_expected_chunks = 0;
                m_step_repair_export_next_chunk = 0;
                m_step_repair_export_active = false;
                m_step_repair_suggested_name.clear();

                CallAfter([this, step_bytes = std::move(step_bytes), suggested_name]() mutable {
                    if (!plater() || plater()->is_background_process_slicing()) {
                        step_repair_page_result(false, "切片進行中，暫時不能載入修補版本；檔案尚未寫入。");
                        return;
                    }

                    wxFileDialog dialog(GetTopWindow(), "另存並使用修補 STEP",
                        from_u8(app_config->get_last_dir()), from_u8(suggested_name),
                        "STEP files (*.step)|*.step", wxFD_SAVE);
                    if (dialog.ShowModal() != wxID_OK) {
                        step_repair_page_result(false, "已取消；原始檔未變更。");
                        return;
                    }

                    wxFileName output_name(dialog.GetPath());
                    output_name.SetExt("step");
                    const wxString output_path = output_name.GetFullPath();
                    if (wxFileExists(output_path)) {
                        step_repair_page_result(false, "為避免覆寫原始檔或既有檔案，請改用一個尚不存在的新檔名。");
                        return;
                    }

                    wxFile output_file;
                    if (!output_file.Create(output_path, false)) {
                        step_repair_page_result(false, "無法建立修補檔，請改用有寫入權限的位置。");
                        return;
                    }
                    const auto written = output_file.Write(step_bytes.data(), step_bytes.size());
                    output_file.Close();
                    if (written != step_bytes.size()) {
                        wxRemoveFile(output_path); // 僅移除本次剛建立且寫入失敗的殘檔。
                        step_repair_page_result(false, "修補檔沒有完整寫入，殘檔已清除；原始檔未變更。");
                        return;
                    }

                    app_config->set("last_dir", into_u8(output_name.GetPath()));
                    wxArrayString input_files;
                    input_files.Add(output_path);
                    plater()->load_files(input_files);
                    BOOST_LOG_TRIVIAL(info) << "STEP repair 1.0.2 saved and queued for import: " << into_u8(output_path);
                    step_repair_page_result(true, "修補版本已另存並載入。", into_u8(output_path));
                });
            }
            else if (command_str.compare("phototile_query_capability") == 0) {
                /* 【2026-08-15】頁面主動要料數。DocumentLoaded 那次推送保留當備援，
                   但這條才是可靠的一條——頁面開口時一定已經 ready。 */
                CallAfter([this] {
                    if (mainframe != nullptr && mainframe->m_webview != nullptr) {
                        mainframe->m_webview->SendPhotoTileMachineCapability();
                        /* 丙案：同一個開口一起回。金鑰是使用者隨時可以在說明選單改的東西
                           ⇒ 每次頁面開口都重問一次，不要只在 app 啟動時問一次。 */
                        mainframe->m_webview->SendPhotoTileAiAvailability();
                    }
                });
            }
            /* FBK-11：訊息叫使用者去「選擇 3D 列印機」，就要給得出那扇門
               （Eric 2026-09-02 實走時問「這個畫面並沒有讓我選擇的位置」）。
               🔴 一定要 CallAfter：wizard 是 modal，直接在 web 訊息處理裡開會把 WebView2 的
                  訊息迴圈卡住——同一個坑頁面註解裡記過（面板隱藏時跳 alert，renderer 停住、
                  連 CDP 都進不去）。 */
            else if (command_str.compare("phototile_open_printer_wizard") == 0) {
                CallAfter([this] { run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS); });
            }
            else if (command_str.compare("phototile_home") == 0) {
                /* 覆審 I-3：離開工作室＝取消現役生成（**同步**清，賽跑窗＝零）。
                   C-1 時頁面卸載＝生成自然中止；C-2 把工作搬進 C++ 後這個天然保護消失
                   ——不取消的話，人已在首頁，數秒後結果落盤：被強制彈進 3D 編輯器、
                   盤被換、機型被切。清了 active ⇒ 遲到結果被 result handler 丟棄。 */
                if (!m_photo_tile_active_job.empty()) {
                    const std::string leaving = m_photo_tile_active_job;
                    m_photo_tile_active_job.clear();
                    if (m_photo_tile_host)
                        m_photo_tile_host->cancel(leaving);
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：離開頁面，取消現役 job=" << leaving;
                }
                CallAfter([this] {
                    if (!mainframe)
                        return;
                    if (mainframe->m_webview)
                        mainframe->m_webview->ShowHomepage();
                    /* PING(2026-09-08 Eric 裁)：返回落「準備」頁，不落首頁。
                       入口在準備頁（#99 Q1 甲搬過去的），返回就該回到入口所在的那一頁；
                       首頁自 #99 起沒有照片磚入口，落首頁＝走進死巷（Eric 0908 實走）。
                       上面 ShowHomepage() 保留：之後點「首頁」分頁看到的才是首頁，不是殘留的工作室。
                       STEP 破面檢查的返回維持落首頁——它的入口在首頁；兩個工具同一條原則。 */
                    mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                });
            }
            /* ⚠ phototile_export_begin|chunk|end 三支＝頁面自建 3MF 鏈的舊收件口——
               build3mf 已退役（C-2 第 1 項）＝**現無任何送出端，保留僅為 rollback**。
               它與下面的 phototile_image_* 鏈幾乎同形：改協定時兩鏈都要看（或屆時直接刪本鏈）。 */
            else if (command_str.compare("phototile_export_begin") == 0) {
                constexpr size_t max_photo_tile_bytes = 512ULL * 1024ULL * 1024ULL;
                const size_t expected_size = root.get<size_t>("data.size", 0);
                const size_t expected_chunks = root.get<size_t>("data.chunks", 0);

                m_photo_tile_export_buffer.clear();
                m_photo_tile_export_expected_size = 0;
                m_photo_tile_export_expected_chunks = 0;
                m_photo_tile_export_next_chunk = 0;
                m_photo_tile_export_active = false;

                if (expected_size == 0 || expected_size > max_photo_tile_bytes || expected_chunks == 0 || expected_chunks > 8192) {
                    BOOST_LOG_TRIVIAL(warning) << "Rejected invalid photo tile export: size=" << expected_size
                                               << ", chunks=" << expected_chunks;
                } else {
                    m_photo_tile_export_buffer.reserve(expected_size);
                    m_photo_tile_export_expected_size = expected_size;
                    m_photo_tile_export_expected_chunks = expected_chunks;
                    m_photo_tile_export_active = true;
                }
            }
            else if (command_str.compare("phototile_export_chunk") == 0) {
                if (!m_photo_tile_export_active)
                    return "";

                const size_t index = root.get<size_t>("data.index", size_t(-1));
                const std::string encoded = root.get<std::string>("data.base64", "");
                if (index != m_photo_tile_export_next_chunk || encoded.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "Photo tile export chunk out of sequence: expected="
                                               << m_photo_tile_export_next_chunk << ", got=" << index;
                    m_photo_tile_export_active = false;
                    m_photo_tile_export_buffer.clear();
                    return "";
                }

                std::vector<unsigned char> decoded(boost::beast::detail::base64::decoded_size(encoded.size()));
                const auto result = boost::beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
                decoded.resize(result.first);
                if (decoded.empty() || m_photo_tile_export_buffer.size() + decoded.size() > m_photo_tile_export_expected_size) {
                    BOOST_LOG_TRIVIAL(warning) << "Photo tile export chunk could not be decoded";
                    m_photo_tile_export_active = false;
                    m_photo_tile_export_buffer.clear();
                    return "";
                }

                m_photo_tile_export_buffer.insert(m_photo_tile_export_buffer.end(), decoded.begin(), decoded.end());
                ++m_photo_tile_export_next_chunk;
            }
            else if (command_str.compare("phototile_export_end") == 0) {
                if (!m_photo_tile_export_active ||
                    m_photo_tile_export_next_chunk != m_photo_tile_export_expected_chunks ||
                    m_photo_tile_export_buffer.size() != m_photo_tile_export_expected_size) {
                    BOOST_LOG_TRIVIAL(warning) << "Photo tile export ended before all data arrived";
                    m_photo_tile_export_active = false;
                    m_photo_tile_export_buffer.clear();
                    return "";
                }

                std::vector<unsigned char> three_mf;
                three_mf.swap(m_photo_tile_export_buffer);
                m_photo_tile_export_active = false;
                m_photo_tile_export_expected_size = 0;
                m_photo_tile_export_expected_chunks = 0;
                m_photo_tile_export_next_chunk = 0;

                // 寫暫存→先切機→開檔＝與宿主生成鏈共用同一支（C-2 抽出；行為與 C-1 逐字同）。
                // env＝nullptr：此鏈協定沒有 env（頁面自建 3MF 直送、使用者同步操作、無「遲到」
                // 問題；發送端 build3mf 已於 C-2 第 1 項退役）＝#9 guard 豁免，不是漏網。
                photo_tile_deliver_3mf(three_mf, root.get<std::string>("data.mode", ""),
                                       root.get<std::string>("data.nozzle", ""), nullptr);
            }
            /* ── C-2 第 1 項：工作室 → 隱形宿主的生成鏈（2026-08-04）──────────────
               頁面只送參數（含使用者挑的料色 slotsJson＝C-1 缺口的正主），影像一律用
               C++ 手上的檔案路徑：拖放/首頁入口＝open_photo_tile 記下；頁內選檔/貼上＝
               phototile_image_begin|chunk|end 回送位元組落成暫存檔。進度/結果由宿主
               handler 回推頁面（photo_tile_ensure_host 掛的三支）。 */
            else if (command_str.compare("phototile_generate") == 0) {
                const std::string job_id = root.get<std::string>("data.jobId", "");
                const std::string mode   = root.get<std::string>("data.mode", "");
                const std::string nozzle = root.get<std::string>("data.nozzle", "");
                // 失敗一律用 jobResult 回推頁面（與宿主結果同一條通道），不靜默。
                const auto fail_to_page = [this](const std::string& job, const std::string& code, const std::string& msg) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.jobResult && "
                        "window.PINGPhotoTile.jobResult({jobId:\"") + ping_js_escape(job) + "\",ok:false,errorCode:\"" +
                        ping_js_escape(code) + "\",errorMessage:\"" + ping_js_escape(msg) + "\"});");
                };
                if (job_id.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "phototile_generate 缺 jobId，忽略";
                    return "";
                }
                if (m_photo_tile_source_path.empty() ||
                    !boost::filesystem::exists(boost::filesystem::path(m_photo_tile_source_path))) {
                    fail_to_page(job_id, "bad_image", "沒有可用的影像來源，請重新載入圖片再試。");
                    return "";
                }
                /* slots 只驗「是陣列＋可解析」就原文直通（會被逐字拼進 generate 請求，
                   壞字串會毀掉整包 JSON）。驗不過＝fail-closed 回錯誤——絕不靜默退回自動配色，
                   那正是 C-1 被抓到的缺口。缺席／空字串也一樣擋（🟡覆審：頁面 buildRequest
                   一定送 slots，缺席只可能是壞掉的呼叫端——放行＝fail-open 自動配色）。
                   探針斷言恰一鍵（🟡覆審：`[{…}],"pillar":{…}` 這種夾帶可用重複鍵繞過單次解析）。 */
                std::string slots_json = root.get<std::string>("data.slotsJson", "");
                {
                    bool slots_ok = false;
                    const size_t first_ch = slots_json.find_first_not_of(" \t\r\n");
                    if (first_ch != std::string::npos && slots_json[first_ch] == '[') {
                        try {
                            std::stringstream slots_ss("{\"a\":" + slots_json + "}");
                            pt::ptree probe;
                            pt::read_json(slots_ss, probe);
                            slots_ok = probe.size() == 1 && probe.count("a") == 1;
                        } catch (...) { slots_ok = false; }
                    }
                    if (!slots_ok) {
                        fail_to_page(job_id, "bad_request", "料色資料（slots）格式錯誤，請重試或重開工作室。");
                        return "";
                    }
                }

                PhotoTileEngineRequest engine_req;
                engine_req.job_id     = job_id;
                engine_req.mode       = mode;
                try { engine_req.nozzle = std::stod(nozzle); } catch (...) { engine_req.nozzle = 0.0; }
                engine_req.width_mm     = root.get<double>("data.size.widthMm", 100.0);
                engine_req.height_mm    = root.get<double>("data.size.heightMm", 75.0);
                engine_req.thick_mm     = root.get<double>("data.size.thickMm", 6.0);
                /* 覆審 I-5：整數欄用 get<double>＋lround——get<int> 收到 "5.5" 會因殘留
                   ".5" 靜默回退預設值（同一個 5.5，瀏覽器開發路徑做 6 階、這裡做 8 階）。
                   頁面 handler 也補了 Math.round＝兩端一致。 */
                engine_req.klevels      = (int) std::lround(root.get<double>("data.klevels", 8.0));
                engine_req.noise_mm     = root.get<double>("data.noiseMm", 2.0);
                engine_req.pillar       = root.get<bool>("data.pillar.enabled", true);
                engine_req.pillar_xy_mm = (int) std::lround(root.get<double>("data.pillar.xyMm", 25.0));
                // WT 線：循環洗料塔設定原封轉送引擎（laps 走逗號字串，ptree 不必解陣列）
                engine_req.cycle         = root.get<bool>("data.cycle.enabled", false);
                engine_req.cycle_laps    = root.get<std::string>("data.cycle.laps", "");
                engine_req.cycle_size_mm = root.get<double>("data.cycle.sizeMm", 0.0);
                engine_req.cycle_gap_mm  = root.get<double>("data.cycle.gapMm", 15.0);
                engine_req.cycle_brim_mm = root.get<double>("data.cycle.brimMm", 8.0);
                engine_req.teeth        = root.get<bool>("data.seam.teeth", false);
                engine_req.p2a_block    = root.get<bool>("data.seam.p2aBlock", false);
                engine_req.slots_json   = slots_json;
                engine_req.image_path   = m_photo_tile_source_path;
                // 產品路徑＝metadata on（裁決 6：ping_phototile.json＋內嵌原圖）。
                // 黃金 12 案自組請求、自帶 off ⇒ 基準不受影響。
                engine_req.want_metadata = true;
                engine_req.embed_source  = true;
                engine_req.group_uuid = boost::uuids::to_string(boost::uuids::random_generator()());
                /* 覆審 I-4：解碼後像素 OOM 帽——C-1 設計的 gate 在唯一產品入口原本沒帶
                   （limits 全 0＝不設防；48Mpx 相片＋quad 在 4GB 下 engine_crashed 已實測）。
                   依可用實體記憶體推（宿主 static，夾 8e6～48e6；查不到＝保守 8e6）。
                   ⚠ 不動 grid_max：0＝引擎預設 3200＝工作室歷史行為，動了會改變輸出
                   位元組、打到黃金 12 案基準。
                   env_json 這裡不填＝交給宿主 generate() 蓋章（C-2 第 2 項已接：
                   ensure_host 註冊的 current-env provider；上盤前 env_is_fresh 比對、過期即棄）。 */
                engine_req.max_decoded_pixels = PhotoTileEngineHost::suggest_max_decoded_pixels();

                photo_tile_ensure_host();
                m_photo_tile_active_job    = job_id;
                m_photo_tile_active_mode   = mode;
                m_photo_tile_active_nozzle = nozzle;
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：generate 分派 job=" << job_id << ", mode=" << mode
                                        << ", nozzle=" << nozzle << ", source=" << m_photo_tile_source_path;
                /* 覆審 I-1：generate 同步失敗不再丟棄回傳值。多數失敗路（runtime 缺、CreateEnv
                   立即失敗）已由宿主 fail_active_and_queued → result handler 同步回推頁面並清
                   active；這裡是最後一道網（Closing 等不經 handler 的 false）——仍持有本 job
                   才補回推，不會重複。否則進度條無限轉、產生鈕鎖死、原因永遠看不到。 */
                if (!m_photo_tile_host->generate(engine_req) && m_photo_tile_active_job == job_id) {
                    m_photo_tile_active_job.clear();
                    fail_to_page(job_id, "engine_unavailable", "照片磚引擎無法啟動，請重開 PING Slicer 再試。");
                }
            }
            else if (command_str.compare("phototile_cancel") == 0) {
                const std::string job_id = root.get<std::string>("data.jobId", "");
                /* A 案（Eric 0804 裁）：取消＝頁面當下自行恢復 UI、盤上舊磚不動。這裡清 active
                   ⇒ 這個 job 之後任何遲到的結果（含引擎未就緒期間排隊補跑完的）一律被
                   result handler 丟棄，絕不上盤。 */
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：cancel job=" << job_id
                                        << "（active=" << m_photo_tile_active_job << "）";
                if (!job_id.empty() && job_id == m_photo_tile_active_job)
                    m_photo_tile_active_job.clear();
                if (!job_id.empty() && m_photo_tile_host)
                    m_photo_tile_host->cancel(job_id);
            }
            /* ── 甲案（c-0822-PT-06）：本地風格化 ────────────────────────────────
               把 dev 端 pipeline.py 的 bilateral→mean-shift→k-means→medianBlur 跑在 C++，
               結果**回送頁面當新的來源影像**——不是只在生成時做。
               🔴 只在生成時做＝預覽跑原圖、輸出跑風格化圖＝兩把尺，正是本專案一再付學費的
                  失效形態（分箱vs顯色、④-2 梯子vs分箱、開發線的兩份梯子）。規格＝R6-9／R6-11。
               ⚠ 800px 約 2 秒 ⇒ 一定要背景執行緒；UI 執行緒只負責推 chunk。
               ℹ️ 這條「C++ 給頁面一張圖」的回送鏈，日後丙案（AI 生圖）回圖走同一條。 */
            else if (command_str.compare("phototile_stylize") == 0) {
                const std::string job_id = root.get<std::string>("data.jobId", "");
                const auto style_fail = [this](const std::string& job, const std::string& msg) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.styleError && "
                        "window.PINGPhotoTile.styleError(\"") + ping_js_escape(job) + "\",\"" +
                        ping_js_escape(msg) + "\");");
                };
                if (job_id.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "phototile_stylize 缺 jobId，忽略";
                    return "";
                }
                if (m_photo_tile_source_path.empty() ||
                    !boost::filesystem::exists(boost::filesystem::path(m_photo_tile_source_path))) {
                    style_fail(job_id, "沒有可用的影像來源，請重新載入圖片再試。");
                    return "";
                }
                /* 要壓平「哪一張」由頁面指定（預設 origin，行為與 2026-08-22 相同）。
                   ・origin ＝使用者的原照片：本地款式走這條。**不取目前來源**，否則第二次
                     風格化會拿已經風格化過的圖再跑一次，色塊越滾越大。
                   ・current＝目前的來源影像，也就是**剛回來的 AI 圖**（丙案，Eric 2026-09-02 裁「甲」）。
                     AI 圖不壓平就直接分箱的話，它那片主色會騎在色階門檻上——實測同款式同照片
                     連生三次，身體中位 L* ＝69.8／71.4／72.6 而門檻在 71.1 ⇒ 乾淨／爆白斑／
                     整片消失三種結局。壓平後每一區的 L* 等於某一階的 L*，離門檻最遠。
                   🆕 T061（牌 c-0924-ACC-27；發包單〈九〉9-5「壓平的輸入仍是 AI 原圖」）：current 優先取 **AI 原圖**
                     （m_photo_tile_ai_path）。壓平成功後 m_photo_tile_source_path 會被換成壓平圖（見下面 styled_path）
                     ⇒ 換一組料之後按「依料重算圖面顏色」再壓一次，原本吃到的是**上一次壓平的結果**（K 個平色塊），
                     不是 AI 原圖——色階數一變，那張圖就分不出新的 K 階。沒有 AI 圖時照舊取目前來源。 */
                const std::string style_input = root.get<std::string>("data.input", "origin");
                PhotoStylizeParams sp;
                if (style_input == "current") {
                    const bool has_ai = !m_photo_tile_ai_path.empty() &&
                                        boost::filesystem::exists(boost::filesystem::path(m_photo_tile_ai_path));
                    sp.src_path = has_ai ? m_photo_tile_ai_path : m_photo_tile_source_path;
                } else {
                    if (m_photo_tile_origin_path.empty())
                        m_photo_tile_origin_path = m_photo_tile_source_path;
                    sp.src_path = m_photo_tile_origin_path;
                }
                sp.work_width = static_cast<int>(std::lround(root.get<double>("data.workWidth", 800.0)));
                sp.tones      = static_cast<int>(std::lround(root.get<double>("data.tones", 4.0)));
                /* mapping（2026-09-03，Eric 裁③「四料款式也走壓平」）：
                   rank（預設，雙料）＝ramp 是恰 K 階的梯子；nearest（四料）＝ramp 是 ≥K 個候選色（≤64）。
                   舊頁面不送這個欄位 ⇒ 預設 rank ＝ 行為與 0902 完全相同。 */
                const std::string mapping = root.get<std::string>("data.mapping", "rank");
                sp.mapping = (mapping == "nearest") ? PhotoStylizeMapping::NearestDistinct
                                                    : PhotoStylizeMapping::Rank;
                for (const auto& kv : root.get_child("data.ramp", pt::ptree()))
                    sp.ramp_hex.push_back(kv.second.get_value<std::string>());
                const int ramp_n = static_cast<int>(sp.ramp_hex.size());
                if (sp.mapping == PhotoStylizeMapping::Rank ? (ramp_n != sp.tones)
                                                            : (ramp_n < sp.tones || ramp_n > 64)) {
                    style_fail(job_id, "色階資料不完整，請重開工作室再試。");
                    return "";
                }
                /* 後發蓋先發：使用者連點款式時只認最後一次。舊 job 的結果回來會被
                   下面的 job_id 比對丟掉——不清 active 會出現「畫面已經是新款式、
                   圖卻換成上一款」這種靜默錯配。 */
                m_photo_tile_style_job = job_id;
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 風格化：分派 job=" << job_id
                                        << ", cw=" << sp.work_width << ", K=" << sp.tones
                                        << ", mapping=" << mapping << ", palette=" << ramp_n;
                std::thread([this, sp, job_id]() {
                    PhotoStylizeResult r = ping_photo_stylize(sp);
                    wxTheApp->CallAfter([this, r, job_id]() {
                        if (m_photo_tile_style_job != job_id)      // 已被更新的請求取代
                            return;
                        m_photo_tile_style_job.clear();
                        const auto fail = [this, &job_id](const std::string& msg) {
                            photo_tile_page_script(std::string(
                                "window.PINGPhotoTile && window.PINGPhotoTile.styleError && "
                                "window.PINGPhotoTile.styleError(\"") + ping_js_escape(job_id) + "\",\"" +
                                ping_js_escape(msg) + "\");");
                        };
                        if (!r.ok || r.png.empty()) {
                            fail(r.error.empty() ? std::string("風格化失敗，請重試。") : r.error);
                            return;
                        }
                        /* 🔴 風格化的結果要成為**生成時真正吃的那張**。
                           只把圖回送頁面而不換來源，會變成「畫面看到風格化、印出來是原圖」——
                           這正是本案一開始就決定要避免的那件事，漏這一步等於整個白做。
                           原圖路徑留在 m_photo_tile_origin_path，重新風格化時還吃得到。 */
                        {
                            const boost::filesystem::path styled_path =
                                boost::filesystem::temp_directory_path() /
                                boost::filesystem::unique_path("PING_photo_tile_styled_%%%%-%%%%-%%%%.png");
                            boost::nowide::ofstream out(styled_path.string(), std::ios::binary);
                            out.write(reinterpret_cast<const char*>(r.png.data()),
                                      static_cast<std::streamsize>(r.png.size()));
                            out.close();
                            if (!out.good()) {
                                /* fail-honest：寫不成就別換來源，並誠實告訴頁面——
                                   保留舊來源＝預覽會換、輸出不會換，那比失敗更糟。 */
                                BOOST_LOG_TRIVIAL(warning) << "風格化暫存檔寫入失敗：" << styled_path.string();
                                fail("風格化結果寫入失敗，請重試。");
                                return;
                            }
                            m_photo_tile_source_path = styled_path.string();
                            BOOST_LOG_TRIVIAL(info) << "PhotoTile 風格化：來源已切換為 " << m_photo_tile_source_path;
                        }
                        /* 分塊回送：96 KB 原始塊（可被 3 整除 ⇒ base64 無中段 padding，
                           與頁面回送圖片那條鏈同法）。chunk 大小改了兩邊都要改。 */
                        constexpr size_t raw_chunk = 96 * 1024;
                        const size_t total  = r.png.size();
                        const size_t chunks = (total + raw_chunk - 1) / raw_chunk;
                        photo_tile_page_script(std::string(
                            "window.PINGPhotoTile && window.PINGPhotoTile.styleBegin && "
                            "window.PINGPhotoTile.styleBegin({jobId:\"") + ping_js_escape(job_id) +
                            "\",size:" + std::to_string(total) + ",chunks:" + std::to_string(chunks) +
                            ",w:" + std::to_string(r.width) + ",h:" + std::to_string(r.height) +
                            ",ms:" + std::to_string(static_cast<long long>(r.elapsed_ms)) + "});");
                        for (size_t i = 0; i < chunks; ++i) {
                            const size_t off = i * raw_chunk;
                            const size_t len = std::min(raw_chunk, total - off);
                            std::string b64;
                            b64.resize(boost::beast::detail::base64::encoded_size(len));
                            b64.resize(boost::beast::detail::base64::encode(b64.data(), r.png.data() + off, len));
                            photo_tile_page_script(std::string(
                                "window.PINGPhotoTile && window.PINGPhotoTile.styleChunk && "
                                "window.PINGPhotoTile.styleChunk({jobId:\"") + ping_js_escape(job_id) +
                                "\",index:" + std::to_string(i) + ",base64:\"" + b64 + "\"});");
                        }
                        photo_tile_page_script(std::string(
                            "window.PINGPhotoTile && window.PINGPhotoTile.styleEnd && "
                            "window.PINGPhotoTile.styleEnd({jobId:\"") + ping_js_escape(job_id) + "\"});");
                        BOOST_LOG_TRIVIAL(info) << "PhotoTile 風格化：回送完成 job=" << job_id
                                                << ", bytes=" << total << ", chunks=" << chunks
                                                << ", " << r.elapsed_ms << " ms";
                    });
                }).detach();
            }
            /* ── 丙案（P3「丑」）：AI 生圖 ──────────────────────────────────────
               規格＝`照片磚_核心規格.md` §9（Eric 2026-08-22 一輪八題）。
               走的是甲案那條「C++ 給頁面一張圖」的回送鏈（當初就是照這個打算蓋的），
               差別只有兩件：①圖是跟 OpenAI 要的，不是本機算的 ②失敗要分類（R9-5）。

               🔴 **金鑰明文一步都不進這個檔**：prompt 從頁面來、圖回頁面去，中間那段
                  （load 金鑰／打 API）整段在 `Utils/PingAiImage.cpp`——那是白名單管制的檔。
                  這裡連 `PingAiKeyStore.hpp` 都沒有 include，是刻意的。
               🔴 **回送的結果會成為新的來源影像**，與甲案同一個理由：只給頁面看不換來源＝
                  預覽是 AI 圖、輸出是原照片＝兩把尺。順帶把甲案的「風格化原圖」記錄作廢——
                  AI 生的圖就是新的原圖，之後要本地風格化要從它開始。 */
            else if (command_str.compare("phototile_ai_generate") == 0) {
                const std::string job_id = root.get<std::string>("data.jobId", "");
                const auto ai_fail = [this](const std::string& job, const std::string& kind, const std::string& msg) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.aiError && "
                        "window.PINGPhotoTile.aiError(\"") + ping_js_escape(job) + "\",\"" +
                        ping_js_escape(kind) + "\",\"" + ping_js_escape(msg) + "\");");
                };
                if (job_id.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "phototile_ai_generate 缺 jobId，忽略";
                    return "";
                }
                PingAiImage::Params ap;
                ap.prompt = root.get<std::string>("data.prompt", "");
                /* 🔴 輸入一律是**使用者那張真照片**：origin 有值＝目前的 source 已經是
                   風格化或上一輪 AI 的產物，拿它再生成一次會在生成物上再生成、越滾越遠
                   （與甲案同一條紀律，↳ 規格 R6-9）。 */
                ap.src_path = m_photo_tile_origin_path.empty() ? m_photo_tile_source_path
                                                               : m_photo_tile_origin_path;
                /* R9-8：尺寸由磚體長寬比決定、**不讓使用者選**。
                   **決定權在頁面**，不是這裡——因為 prompt 裡的 `{minPx}` 要用生圖寬度去算
                   （minPx = 2×口徑 ÷ 磚寬 × 生圖解析度），頁面若不知道自己會拿到哪個尺寸，
                   那個數字就會失準（而使用者沒有判斷依據）。這裡只當**守門的**：
                   收到的不是三種合法值之一（舊頁面／被改壞）就自己按長寬比補一個，
                   絕不把非法值送進 API 換一個看不懂的 400。 */
                ap.size = root.get<std::string>("data.size", "");
                if (ap.size != "1024x1024" && ap.size != "1536x1024" && ap.size != "1024x1536")
                    ap.size = PingAiImage::size_for_tile(root.get<double>("data.widthMm", 0.0),
                                                         root.get<double>("data.heightMm", 0.0));
                ap.quality = root.get<std::string>("data.quality", "low");
                if (ap.prompt.empty()) {
                    ai_fail(job_id, "response", "這個款式沒有可用的生圖描述，請換一個款式。");
                    return "";
                }
                // 後發蓋先發：同甲案。使用者連點款式時只認最後一次，舊 job 的結果回來會被丟掉。
                m_photo_tile_ai_job = job_id;
                BOOST_LOG_TRIVIAL(info) << "PhotoTile AI 生圖：分派 job=" << job_id
                                        << ", size=" << ap.size << ", quality=" << ap.quality;
                std::thread([this, ap, job_id]() {
                    PingAiImage::Result r = PingAiImage::generate(ap);
                    wxTheApp->CallAfter([this, r, job_id]() {
                        if (m_photo_tile_ai_job != job_id)      // 已被更新的請求取代
                            return;
                        m_photo_tile_ai_job.clear();
                        /* 內層自己的回報 helper——外層那顆在 UI 執行緒的 lambda 裡捕獲不到
                           （同甲案：那邊也是在 CallAfter 內另立一顆 fail）。 */
                        const auto ai_fail_page = [this, &job_id](const std::string& kind, const std::string& msg) {
                            photo_tile_page_script(std::string(
                                "window.PINGPhotoTile && window.PINGPhotoTile.aiError && "
                                "window.PINGPhotoTile.aiError(\"") + ping_js_escape(job_id) + "\",\"" +
                                ping_js_escape(kind) + "\",\"" + ping_js_escape(msg) + "\");");
                        };
                        if (!r.ok || r.png.empty()) {
                            /* R9-5：把「重試有沒有意義」交給頁面決定，所以要把種類送過去。
                               只回一句錯誤字串的話，頁面唯一能做的就是無差別重試＝重複燒錢。 */
                            const char* kind = "response";
                            switch (r.fail) {
                            case PingAiImage::FailKind::NoKey:   kind = "nokey";   break;
                            case PingAiImage::FailKind::Auth:    kind = "auth";    break;
                            case PingAiImage::FailKind::Quota:   kind = "quota";   break;
                            case PingAiImage::FailKind::Network: kind = "network"; break;
                            default: break;
                            }
                            ai_fail_page(kind, r.error.empty() ? std::string("AI 生圖失敗，請重試。") : r.error);
                            return;
                        }
                        /* 落成暫存檔並換成新的來源——與甲案同一段邏輯、同一個 fail-honest：
                           寫不成就不要換來源，並誠實回報（換一半＝預覽與輸出不同源）。 */
                        const boost::filesystem::path ai_path =
                            boost::filesystem::temp_directory_path() /
                            boost::filesystem::unique_path("PING_photo_tile_ai_%%%%-%%%%-%%%%.png");
                        boost::nowide::ofstream out(ai_path.string(), std::ios::binary);
                        out.write(reinterpret_cast<const char*>(r.png.data()),
                                  static_cast<std::streamsize>(r.png.size()));
                        out.close();
                        if (!out.good()) {
                            BOOST_LOG_TRIVIAL(warning) << "AI 生圖暫存檔寫入失敗：" << ai_path.string();
                            ai_fail_page("response", "生成的圖片寫入失敗，請重試。");
                            return;
                        }
                        /* 換來源前先把「真照片」記下來（第一次才記）——之後不論再生一次、
                           或改跑本地風格化，輸入都還是那張真照片，不會疊在生成物上。
                           換圖時這個記錄會被作廢（image_end／open_photo_tile 都有清）。 */
                        if (m_photo_tile_origin_path.empty())
                            m_photo_tile_origin_path = m_photo_tile_source_path;
                        m_photo_tile_source_path = ai_path.string();
                        m_photo_tile_ai_path     = ai_path.string();   // 另存 AI 圖用（壓平後 source 會被換掉，這個不動）
                        BOOST_LOG_TRIVIAL(info) << "PhotoTile AI 生圖：來源已切換為 " << m_photo_tile_source_path;

                        /* 用量計數（Eric 2026-09-02「提醒他使用了多少的數量」）。
                           🔴 **只在真的拿到圖時才加**——失敗的呼叫（401／429／逾時）不計費，
                              把它們算進去會讓使用者看到一個比帳單大的數字，那比不顯示更糟。
                           落點＝AppConfig（張數不是機密；金鑰存取層碰 AppConfig 才是閘門 C4 禁的事）。 */
                        int ai_count = 0;
                        if (app_config != nullptr) {
                            try { ai_count = std::max(0, std::stoi(app_config->get("ping_ai_image_count"))); }
                            catch (...) { ai_count = 0; }
                            ++ai_count;
                            app_config->set("ping_ai_image_count", std::to_string(ai_count));
                        }

                        // 分塊回送：與甲案同規格（96 KB 可被 3 整除 ⇒ base64 無中段 padding）。
                        constexpr size_t raw_chunk = 96 * 1024;
                        const size_t total  = r.png.size();
                        const size_t chunks = (total + raw_chunk - 1) / raw_chunk;
                        photo_tile_page_script(std::string(
                            "window.PINGPhotoTile && window.PINGPhotoTile.aiBegin && "
                            "window.PINGPhotoTile.aiBegin({jobId:\"") + ping_js_escape(job_id) +
                            "\",size:" + std::to_string(total) + ",chunks:" + std::to_string(chunks) +
                            ",ms:" + std::to_string(static_cast<long long>(r.elapsed_ms)) +
                            ",count:" + std::to_string(ai_count) + "});");   // 累計張數：金額由頁面用款式庫的單價算
                        for (size_t i = 0; i < chunks; ++i) {
                            const size_t off = i * raw_chunk;
                            const size_t len = std::min(raw_chunk, total - off);
                            std::string b64;
                            b64.resize(boost::beast::detail::base64::encoded_size(len));
                            b64.resize(boost::beast::detail::base64::encode(b64.data(), r.png.data() + off, len));
                            photo_tile_page_script(std::string(
                                "window.PINGPhotoTile && window.PINGPhotoTile.aiChunk && "
                                "window.PINGPhotoTile.aiChunk({jobId:\"") + ping_js_escape(job_id) +
                                "\",index:" + std::to_string(i) + ",base64:\"" + b64 + "\"});");
                        }
                        photo_tile_page_script(std::string(
                            "window.PINGPhotoTile && window.PINGPhotoTile.aiEnd && "
                            "window.PINGPhotoTile.aiEnd({jobId:\"") + ping_js_escape(job_id) + "\"});");
                        BOOST_LOG_TRIVIAL(info) << "PhotoTile AI 生圖：回送完成 job=" << job_id
                                                << ", bytes=" << total << ", chunks=" << chunks;
                    });
                }).detach();
            }
            /* ── 另存 AI 圖（Eric 2026-09-03 裁①：存**壓平前**那張＝真正花錢拿到的圖）────────
               生圖成功時 m_photo_tile_ai_path 記著暫存檔；之後壓平會把 source 換成壓平圖，
               但這個記錄不動，所以壓平後仍存得到原 AI 圖。換照片即作廢（與 origin_path 同步清）。
               **複製**而不是搬移：%TEMP% 那份仍是產品內的來源影像。
               對話框走 STEP「另存並使用修補」同一套寫法；差別是這裡允許覆寫（wxFD_OVERWRITE_PROMPT
               由 OS 自己問）——存圖不像 STEP 那樣有「覆寫到原始檔」的風險。 */
            else if (command_str.compare("phototile_ai_save") == 0) {
                const std::string style_name = root.get<std::string>("data.styleName", "AI");
                const auto ai_saved = [this](bool ok, const std::string& path, const std::string& msg) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.aiSaved && "
                        "window.PINGPhotoTile.aiSaved({ok:") + (ok ? "true" : "false") + ",path:\"" +
                        ping_js_escape(path) + "\",message:\"" + ping_js_escape(msg) + "\"});");
                };
                if (m_photo_tile_ai_path.empty() ||
                    !boost::filesystem::exists(boost::filesystem::path(m_photo_tile_ai_path))) {
                    ai_saved(false, "", "目前沒有可另存的 AI 圖——先用一個 AI 款式生一張。");
                    return "";
                }
                const std::string src = m_photo_tile_ai_path;
                CallAfter([this, src, style_name, ai_saved]() {
                    const wxString suggested = from_u8("PING照片磚_AI_") + from_u8(style_name) + "_" +
                                               wxDateTime::Now().Format("%Y%m%d-%H%M") + ".png";
                    wxFileDialog dialog(GetTopWindow(), from_u8("另存 AI 生成圖"),
                        from_u8(app_config->get_last_output_dir(app_config->get_last_dir())), suggested,
                        "PNG (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
                    if (dialog.ShowModal() != wxID_OK) {
                        ai_saved(false, "", "已取消，未存檔。");
                        return;
                    }
                    wxFileName out(dialog.GetPath());
                    out.SetExt("png");
                    const wxString out_path = out.GetFullPath();
                    if (!wxCopyFile(from_u8(src), out_path, true)) {
                        ai_saved(false, "", "寫入失敗，請改用有寫入權限的位置。");
                        return;
                    }
                    app_config->update_last_output_dir(into_u8(out.GetPath()));
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile AI 圖另存：" << into_u8(out_path);
                    ai_saved(true, into_u8(out_path), "");
                });
            }
            /* 頁內「開啟圖片」與 Ctrl+V 的圖只存在頁面裡（C++ 沒有路徑），而宿主吃檔案路徑
               ⇒ 頁面回送原始位元組、這裡落成暫存檔（鏡像 export 鏈的四項驗證：連號/塊數/總長度/上限）。 */
            else if (command_str.compare("phototile_image_begin") == 0) {
                constexpr size_t max_image_bytes = 64ULL * 1024ULL * 1024ULL;   // 與宿主 MAX_IMAGE_BYTES、拖放路同值
                const size_t expected_size   = root.get<size_t>("data.size", 0);
                const size_t expected_chunks = root.get<size_t>("data.chunks", 0);

                m_photo_tile_image_buffer.clear();
                m_photo_tile_image_expected_size = 0;
                m_photo_tile_image_expected_chunks = 0;
                m_photo_tile_image_next_chunk = 0;
                m_photo_tile_image_active = false;
                m_photo_tile_image_mime.clear();

                /* fail-closed（覆審 B-1）：begin 當下就作廢舊來源——之後不論傳輸失敗、
                   或送了 begin 就沒下文，generate 都誠實回「沒有可用的影像來源」，絕不
                   靜默沿用上一張圖（預覽是新圖、生成用舊圖＝與 slots 缺口同形狀）。
                   舊暫存檔的清理與作廢無關：走 m_photo_tile_owned_temp 記帳（覆審 I-7）。 */
                m_photo_tile_source_path.clear();

                if (expected_size == 0 || expected_size > max_image_bytes || expected_chunks == 0 || expected_chunks > 8192) {
                    BOOST_LOG_TRIVIAL(warning) << "Rejected invalid photo tile image upload: size=" << expected_size
                                               << ", chunks=" << expected_chunks;
                    photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片檔案過大（上限 64 MB）或傳輸資料異常，請重新選一次圖片。');");
                } else {
                    m_photo_tile_image_buffer.reserve(expected_size);
                    m_photo_tile_image_expected_size = expected_size;
                    m_photo_tile_image_expected_chunks = expected_chunks;
                    m_photo_tile_image_mime = root.get<std::string>("data.mime", "image/png");
                    m_photo_tile_image_active = true;
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：影像回送開始 size=" << expected_size
                                            << ", chunks=" << expected_chunks << ", mime=" << m_photo_tile_image_mime;
                }
            }
            else if (command_str.compare("phototile_image_chunk") == 0) {
                if (!m_photo_tile_image_active)
                    return "";

                const size_t index = root.get<size_t>("data.index", size_t(-1));
                const std::string encoded = root.get<std::string>("data.base64", "");
                if (index != m_photo_tile_image_next_chunk || encoded.empty()) {              // ①連號
                    BOOST_LOG_TRIVIAL(warning) << "Photo tile image chunk out of sequence: expected="
                                               << m_photo_tile_image_next_chunk << ", got=" << index;
                    m_photo_tile_image_active = false;
                    m_photo_tile_image_buffer.clear();
                    // 來源已在 begin 作廢＝維持 fail-closed；這裡只補「讓失敗顯性」（覆審 B-1）
                    photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片傳輸中斷，請重新選一次圖片。');");
                    return "";
                }

                std::vector<unsigned char> decoded(boost::beast::detail::base64::decoded_size(encoded.size()));
                const auto decode_result = boost::beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
                decoded.resize(decode_result.first);
                if (decoded.empty() || m_photo_tile_image_buffer.size() + decoded.size() > m_photo_tile_image_expected_size) {
                    BOOST_LOG_TRIVIAL(warning) << "Photo tile image chunk could not be decoded";   // ③總長上限
                    m_photo_tile_image_active = false;
                    m_photo_tile_image_buffer.clear();
                    photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片傳輸資料異常，請重新選一次圖片。');");
                    return "";
                }

                m_photo_tile_image_buffer.insert(m_photo_tile_image_buffer.end(), decoded.begin(), decoded.end());
                ++m_photo_tile_image_next_chunk;
            }
            else if (command_str.compare("phototile_image_end") == 0) {
                if (!m_photo_tile_image_active ||
                    m_photo_tile_image_next_chunk != m_photo_tile_image_expected_chunks ||     // ②塊數
                    m_photo_tile_image_buffer.size() != m_photo_tile_image_expected_size) {    // ③總長度
                    BOOST_LOG_TRIVIAL(warning) << "Photo tile image upload ended before all data arrived";
                    m_photo_tile_image_active = false;
                    m_photo_tile_image_buffer.clear();
                    photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片傳輸不完整，請重新選一次圖片。');");
                    return "";
                }

                // 副檔名跟著 mime：宿主注入時會再從路徑副檔名推回 mime（PhotoTileEngineHost 背景讀檔）
                // 🟡覆審：補 gif/avif（頁面收 image/*；缺列會落成 .png、metadata 的 mime 記錯）
                std::string ext = "png";
                if (m_photo_tile_image_mime == "image/jpeg" || m_photo_tile_image_mime == "image/jpg") ext = "jpg";
                else if (m_photo_tile_image_mime == "image/webp") ext = "webp";
                else if (m_photo_tile_image_mime == "image/bmp")  ext = "bmp";
                else if (m_photo_tile_image_mime == "image/gif")  ext = "gif";
                else if (m_photo_tile_image_mime == "image/avif") ext = "avif";

                const boost::filesystem::path image_path = boost::filesystem::temp_directory_path() /
                    boost::filesystem::unique_path("PING_photo_tile_src_%%%%-%%%%-%%%%." + ext);
                boost::nowide::ofstream output(image_path.string(), std::ios::binary);
                output.write(reinterpret_cast<const char*>(m_photo_tile_image_buffer.data()),
                             static_cast<std::streamsize>(m_photo_tile_image_buffer.size()));
                output.close();
                const bool write_ok = output.good();

                m_photo_tile_image_active = false;
                m_photo_tile_image_buffer.clear();
                m_photo_tile_image_expected_size = 0;
                m_photo_tile_image_expected_chunks = 0;
                m_photo_tile_image_next_chunk = 0;

                if (!write_ok) {
                    /* fail-honest：寫檔失敗就維持來源為空（begin 已清）——保留舊路徑會讓
                       「預覽是新圖、生成用舊圖」靜默發生；generate 會誠實回「沒有可用的影像來源」。 */
                    BOOST_LOG_TRIVIAL(warning) << "Unable to write photo tile source image: " << image_path.string();
                    m_photo_tile_source_path.clear();
                    photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.imageError('圖片暫存檔寫入失敗，請重新選一次圖片。');");
                    return "";
                }
                /* 覆審 I-7：刪舊來源＝記帳制——只刪 m_photo_tile_owned_temp 記過帳的那顆
                   （唯一設值點＝自己寫暫存成功這一刻），**絕不憑檔名長相刪**：舊版 substring
                   比對整條路徑，使用者留存的真實照片（如 D:\照片\PING_photo_tile_src_0001.png）
                   會被連帶刪掉、不進資源回收筒。拖放／開檔的真實路徑永不入帳＝不可能被刪。
                   現役 job 可能還在讀舊檔（冷啟動排隊窗，🟡覆審）＝那就不刪、留給 %TEMP%（寧漏勿誤）。 */
                const std::string previous_owned = m_photo_tile_owned_temp;
                m_photo_tile_source_path = image_path.string();
                /* 甲案：換圖＝風格化原圖記錄作廢。不清的話，下一次風格化會拿**上一張圖**
                   當輸入，而畫面上是新圖——又是「預覽一張、輸出另一張」那型。 */
                m_photo_tile_origin_path.clear();
                m_photo_tile_ai_path.clear();      // 換圖＝上一張 AI 圖作廢（另存按鈕在頁面同步收掉）
                m_photo_tile_owned_temp  = image_path.string();
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：影像回送落檔 " << m_photo_tile_source_path;
                if (!previous_owned.empty() && previous_owned != m_photo_tile_source_path) {
                    if (m_photo_tile_active_job.empty()) {
                        try { boost::filesystem::remove(boost::filesystem::path(previous_owned)); }
                        catch (...) {}
                    } else {
                        BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：現役 job 進行中，延後不刪舊暫存 " << previous_owned;
                    }
                }
            }
            /* 【段 B・車 2（牌 c-0923-ACC-21）】照片磚「材料庫」的家＝App 設定資料夾
               （規格 R6-14 子題 2 裁 B：庫落 data_dir——使用者備份得到、清瀏覽器資料也不會不見）。
               檔案＝<data_dir>/phototile/material_library.json。宿主**不解讀內容**（schema／正規化住頁面 matlib.js），
               只負責「完整地存、完整地讀」。
               🔴 收件四項驗證照活著的 phototile_image_* 抄：①連號 ②塊數 ③總長度 ④上限（另加壞 base64 一道）。
                  參考模型＝matlib.js 的 createSaveAssembler()；**改規則兩邊一起改**
                  （tools/ping/phototile_protocol_test.js §8 會比對這裡的上限值與四個訊息名）。
               🔴 一次存檔只回一次結果（在 end）：中途壞掉就記下原因、後面的塊一律不收、到 end 才回報——
                  不回報兩次，也絕不半套落檔（半套＝使用者的校正資料被截斷，而且不報錯）。
               🔴 原子寫：先寫 .tmp、確認寫滿才換上（Slic3r::rename_file＝原子取代）；換上前把舊檔複製成 .bak。
               ⚠ 系統錯誤訊息（ec.message()）只進 log、不進畫面：Windows 會給本機字碼頁的字串，不是 UTF-8。 */
            else if (command_str.compare("phototile_matlib_load") == 0) {
                constexpr size_t max_matlib_bytes = 524288;   // 512 KB——同值住 matlib.js HOST_MAX_BYTES
                const boost::filesystem::path lib_path =
                    boost::filesystem::path(data_dir()) / "phototile" / "material_library.json";
                std::string reply;
                boost::system::error_code ec;
                if (!boost::filesystem::exists(lib_path, ec)) {
                    reply = "{ok:true,exists:false}";
                } else {
                    const boost::uintmax_t size = boost::filesystem::file_size(lib_path, ec);
                    if (ec || size == 0 || size > max_matlib_bytes) {
                        BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫：不載入 " << lib_path.string() << " size=" << size
                                                   << (ec ? (" err=" + ec.message()) : std::string());
                        reply = std::string("{ok:false,message:\"") +
                                ping_js_escape("材料庫檔案是空的、讀不到大小、或超過 512 KB，這次沒有載入（原檔未動）。") + "\"}";
                    } else {
                        std::vector<unsigned char> bytes(static_cast<size_t>(size));
                        boost::nowide::ifstream input(lib_path.string(), std::ios::binary);
                        if (!input || !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
                            reply = std::string("{ok:false,message:\"") + ping_js_escape("材料庫檔案讀取失敗（原檔未動）。") + "\"}";
                        } else {
                            std::string encoded(boost::beast::detail::base64::encoded_size(bytes.size()), '\0');
                            encoded.resize(boost::beast::detail::base64::encode(&encoded[0], bytes.data(), bytes.size()));
                            reply = "{ok:true,exists:true,size:" + std::to_string(bytes.size()) + ",base64:\"" + encoded + "\"}";
                        }
                    }
                }
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 材料庫：讀取 " << lib_path.string() << " → " << reply.substr(0, 32);
                photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.matlibLoaded && "
                                       "window.PINGPhotoTile.matlibLoaded(" + reply + ");");
            }
            else if (command_str.compare("phototile_matlib_save_begin") == 0) {
                constexpr size_t max_matlib_bytes  = 524288;   // 同上
                constexpr size_t max_matlib_chunks = 4096;     // 同值住 matlib.js HOST_MAX_CHUNKS
                const size_t expected_size   = root.get<size_t>("data.size", 0);
                const size_t expected_chunks = root.get<size_t>("data.chunks", 0);
                m_photo_tile_matlib_buffer.clear();
                m_photo_tile_matlib_expected_size   = 0;
                m_photo_tile_matlib_expected_chunks = 0;
                m_photo_tile_matlib_next_chunk      = 0;
                m_photo_tile_matlib_fail.clear();
                if (expected_size == 0 || expected_size > max_matlib_bytes ||                          // ④上限
                    expected_chunks == 0 || expected_chunks > max_matlib_chunks || expected_chunks > expected_size) {
                    m_photo_tile_matlib_state = 2;
                    m_photo_tile_matlib_fail  = "材料庫大小或分塊數不合法（上限 512 KB），這次沒有存檔。";
                    BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫：拒收 size=" << expected_size << ", chunks=" << expected_chunks;
                } else {
                    m_photo_tile_matlib_buffer.reserve(expected_size);
                    m_photo_tile_matlib_expected_size   = expected_size;
                    m_photo_tile_matlib_expected_chunks = expected_chunks;
                    m_photo_tile_matlib_state           = 1;
                }
            }
            else if (command_str.compare("phototile_matlib_save_chunk") == 0) {
                if (m_photo_tile_matlib_state != 1)
                    return "";                                  // 閒置或已作廢：不收（原因等 end 一次回報）
                const size_t index = root.get<size_t>("data.index", size_t(-1));
                const std::string encoded = root.get<std::string>("data.base64", "");
                const auto fail = [this](const char* why) {
                    m_photo_tile_matlib_state = 2;
                    m_photo_tile_matlib_fail  = why;
                    m_photo_tile_matlib_buffer.clear();
                };
                if (index != m_photo_tile_matlib_next_chunk || encoded.empty()) {                       // ①連號
                    fail("材料庫存檔的分塊亂序或遺失，這次沒有存檔。");
                    return "";
                }
                std::vector<unsigned char> decoded(boost::beast::detail::base64::decoded_size(encoded.size()));
                const auto dr = boost::beast::detail::base64::decode(decoded.data(), encoded.data(), encoded.size());
                decoded.resize(dr.first);
                /* 壞 base64 一律擋（同 matlib.js b64ToBytes）：beast 遇到非法字元會「停下來」而不是報錯，
                   不驗就會靜默收下一段較短的資料。合法＝長度是 4 的倍數、停下來之後只剩最多兩個 '='。 */
                size_t pad = 0;
                for (size_t k = dr.second; k < encoded.size(); ++k) {
                    if (encoded[k] == '=') ++pad; else { pad = 99; break; }
                }
                if (encoded.size() % 4 != 0 || pad > 2 || decoded.empty()) {
                    fail("材料庫存檔的分塊內容解不開，這次沒有存檔。");
                    return "";
                }
                if (m_photo_tile_matlib_buffer.size() + decoded.size() > m_photo_tile_matlib_expected_size) {   // ③總長度
                    fail("材料庫存檔的長度超過宣告，這次沒有存檔。");
                    return "";
                }
                m_photo_tile_matlib_buffer.insert(m_photo_tile_matlib_buffer.end(), decoded.begin(), decoded.end());
                ++m_photo_tile_matlib_next_chunk;
            }
            else if (command_str.compare("phototile_matlib_save_end") == 0) {
                const auto saved = [this](bool ok, const std::string& message, const std::string& path) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.matlibSaved && "
                        "window.PINGPhotoTile.matlibSaved({ok:") + (ok ? "true" : "false") + ",message:\"" +
                        ping_js_escape(message) + "\",path:\"" + ping_js_escape(path) + "\"});");
                };
                const int         state       = m_photo_tile_matlib_state;
                const std::string fail_reason = m_photo_tile_matlib_fail;
                const bool        count_ok    = m_photo_tile_matlib_next_chunk == m_photo_tile_matlib_expected_chunks;   // ②塊數
                const size_t      want_size   = m_photo_tile_matlib_expected_size;
                std::vector<unsigned char> bytes;
                bytes.swap(m_photo_tile_matlib_buffer);
                m_photo_tile_matlib_state = 0;
                m_photo_tile_matlib_fail.clear();
                m_photo_tile_matlib_expected_size   = 0;
                m_photo_tile_matlib_expected_chunks = 0;
                m_photo_tile_matlib_next_chunk      = 0;
                if (state == 2) { saved(false, fail_reason, ""); return ""; }
                if (state != 1) { saved(false, "沒有進行中的材料庫存檔。", ""); return ""; }
                if (!count_ok)  { saved(false, "材料庫存檔的分塊數不符，這次沒有存檔。", ""); return ""; }
                if (bytes.size() != want_size) { saved(false, "材料庫存檔的總長度不符，這次沒有存檔。", ""); return ""; }   // ③總長度

                const boost::filesystem::path dir      = boost::filesystem::path(data_dir()) / "phototile";
                const boost::filesystem::path lib_path = dir / "material_library.json";
                const boost::filesystem::path tmp_path = dir / "material_library.json.tmp";
                const boost::filesystem::path bak_path = dir / "material_library.json.bak";
                boost::system::error_code ec;
                boost::filesystem::create_directories(dir, ec);
                if (ec) {
                    BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫：建資料夾失敗 " << dir.string() << " err=" << ec.message();
                    saved(false, "建不了材料庫的資料夾，這次沒有存檔（原檔未動）。", "");
                    return "";
                }
                bool write_ok = false;
                {
                    boost::nowide::ofstream output(tmp_path.string(), std::ios::binary | std::ios::trunc);
                    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    output.close();
                    write_ok = output.good();
                }
                /* 寫滿才換上：讀回大小對不上＝不換（就地覆寫失敗會留下 0 byte 檔，SOP_單檔編譯檢查 §6-6 付過的學費） */
                if (write_ok) {
                    const boost::uintmax_t written = boost::filesystem::file_size(tmp_path, ec);
                    write_ok = !ec && written == bytes.size();
                }
                if (!write_ok) {
                    boost::filesystem::remove(tmp_path, ec);
                    BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫：暫存檔寫入失敗 " << tmp_path.string();
                    saved(false, "材料庫暫存檔寫入失敗，這次沒有存檔（原檔未動）。", "");
                    return "";
                }
                if (boost::filesystem::exists(lib_path, ec)) {
                    boost::filesystem::copy_file(lib_path, bak_path, boost::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)   // 備份失敗不擋存檔（只記 log）：擋下來＝使用者這一次的校正資料反而沒地方放
                        BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫：備份舊檔失敗 " << bak_path.string() << " err=" << ec.message();
                }
                if (const std::error_code rc = Slic3r::rename_file(tmp_path.string(), lib_path.string())) {
                    boost::filesystem::remove(tmp_path, ec);
                    BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫：換上新檔失敗 " << lib_path.string() << " err=" << rc.message();
                    saved(false, "材料庫換上新檔失敗，這次沒有存檔（原檔未動）。", "");
                    return "";
                }
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 材料庫：已存 " << bytes.size() << " bytes → " << lib_path.string();
                saved(true, "", lib_path.string());
            }
            /* 【〈九〉9-6 分車表「T062」那一列（牌 c-0924-ACC-30）】照片磚「匯出材料庫…」＝整庫（含封存的）一個檔，
               搬電腦或給同事，對方按「匯入…」選這個檔（〈材料庫管理〉Q4／Q10）。
               內容由頁面組（matlib.js exportText＝畫面上看到的那一份）；宿主照舊**不解讀**，只負責開存檔視窗、完整地寫。
               為什麼走宿主：嵌入式 WebView 的下載沒有存檔視窗（mac 版甚至不會動）——同「另存 AI 圖…」那條路。
               一則訊息、不分塊：上限同存檔（512 KB）；上限與壞 base64 的擋法照 phototile_matlib_save_* 抄
               （tools/ping/phototile_protocol_test.js §8 比對這裡的訊息名）。
               🔴 先寫 .tmp、寫滿才換上：使用者選了覆寫既有檔時，寫到一半失敗不能把原檔弄成 0 byte。 */
            else if (command_str.compare("phototile_matlib_export") == 0) {
                constexpr size_t max_matlib_bytes = 524288;   // 同值住 matlib.js HOST_MAX_BYTES
                const auto exported = [this](bool ok, const std::string& path, const std::string& message) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.matlibExported && "
                        "window.PINGPhotoTile.matlibExported({ok:") + (ok ? "true" : "false") + ",path:\"" +
                        ping_js_escape(path) + "\",message:\"" + ping_js_escape(message) + "\"});");
                };
                const size_t      want    = root.get<size_t>("data.size", 0);
                const std::string encoded = root.get<std::string>("data.base64", "");
                std::string       name    = root.get<std::string>("data.name", "");
                std::vector<unsigned char> bytes(boost::beast::detail::base64::decoded_size(encoded.size()));
                const auto dr = boost::beast::detail::base64::decode(bytes.data(), encoded.data(), encoded.size());
                bytes.resize(dr.first);
                size_t pad = 0;   // 壞 base64 一律擋（判法同 phototile_matlib_save_chunk：beast 遇到非法字元只會停下來、不報錯）
                for (size_t k = dr.second; k < encoded.size(); ++k) {
                    if (encoded[k] == '=') ++pad; else { pad = 99; break; }
                }
                if (want == 0 || want > max_matlib_bytes || encoded.size() % 4 != 0 || pad > 2 || bytes.size() != want) {
                    BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫匯出：拒收 size=" << want << ", decoded=" << bytes.size();
                    exported(false, "", "材料庫內容不完整或超過 512 KB，沒有匯出。");
                    return "";
                }
                if (name.empty()) name = "照片磚材料庫.json";
                CallAfter([this, bytes, name, exported]() {
                    wxFileDialog dialog(GetTopWindow(), from_u8("匯出材料庫"),
                        from_u8(app_config->get_last_output_dir(app_config->get_last_dir())), from_u8(name),
                        "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
                    if (dialog.ShowModal() != wxID_OK) {
                        exported(false, "", "已取消，沒有匯出。");
                        return;
                    }
                    wxFileName out(dialog.GetPath());
                    out.SetExt("json");
                    const boost::filesystem::path out_path(into_u8(out.GetFullPath()));
                    const boost::filesystem::path tmp_path(out_path.string() + ".tmp");
                    boost::system::error_code ec;
                    bool write_ok = false;
                    {
                        boost::nowide::ofstream output(tmp_path.string(), std::ios::binary | std::ios::trunc);
                        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                        output.close();
                        write_ok = output.good();
                    }
                    if (write_ok) {
                        const boost::uintmax_t written = boost::filesystem::file_size(tmp_path, ec);
                        write_ok = !ec && written == bytes.size();
                    }
                    if (write_ok)
                        write_ok = !Slic3r::rename_file(tmp_path.string(), out_path.string());
                    if (!write_ok) {
                        boost::filesystem::remove(tmp_path, ec);
                        BOOST_LOG_TRIVIAL(warning) << "PhotoTile 材料庫匯出：寫入失敗 " << out_path.string();
                        exported(false, "", "寫入失敗，請改用有寫入權限的位置（原本的檔沒有動）。");
                        return;
                    }
                    app_config->update_last_output_dir(into_u8(out.GetPath()));
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile 材料庫匯出：" << bytes.size() << " bytes → " << out_path.string();
                    exported(true, out_path.string(), "");
                });
            }
            /* 【AIP 第二班（開發中清單 #22；規格 R9-11；計畫頁 §03 刀 3）】客戶的 AI 產圖改成「複製提示詞 → 用自己的 AI 產圖 → 貼回工作室」。
               ① phototile_ai_import_begin／chunk／end：貼回的圖（頁面一律先轉 PNG——OpenCV 建置關了 JPEG／WebP 解碼）收進來當 AI 圖，
                  跟金鑰直連生回來那張走同一條：source＝ai_path＝它、origin 記原照片 ⇒ 壓平（phototile_stylize input:'current'）與
                  「另存 AI 圖」都照舊吃它，只有一把尺。成功才換來源；失敗不清掉舊圖（不像 phototile_image_* 換照片那條）。
               ② phototile_source_origin：〔回到原圖〕＝來源換回原照片；這次產的 AI 圖留在頁面的清單上，點一下會重新送進來（走 ①）。
               ③ phototile_mystyles_load／save_*：「我的款式」存在這台電腦（<data_dir>/phototile/my_styles.json，跟材料庫同一個資料夾）；
                  宿主不解讀內容，只負責完整地存、完整地讀（.tmp → 讀回比對 → 舊檔留 .bak → 換上）。
               🔴 這幾支只收照片磚頁送的（同 STEP 修補頁那道信任邊界：會叫 C++ 寫檔、換來源）；既有的 phototile_* 沒有這道，這次不回頭補。 */
            else if (command_str.compare("phototile_ai_import_begin") == 0 || command_str.compare("phototile_ai_import_chunk") == 0 ||
                     command_str.compare("phototile_ai_import_end") == 0 || command_str.compare("phototile_source_origin") == 0 ||
                     command_str.compare("phototile_mystyles_load") == 0 || command_str.compare("phototile_mystyles_save_begin") == 0 ||
                     command_str.compare("phototile_mystyles_save_chunk") == 0 || command_str.compare("phototile_mystyles_save_end") == 0) {
                if (!mainframe || !mainframe->m_webview || !mainframe->m_webview->IsPhotoTilePage()) {
                    BOOST_LOG_TRIVIAL(warning) << "Rejected " << command_str << " outside the photo-tile page";
                    return "";
                }
                constexpr size_t max_import_bytes   = 64 * 1024 * 1024;   // 同 phototile_image_begin
                constexpr size_t max_import_chunks  = 8192;
                constexpr size_t max_mystyles_bytes = 524288;             // 同材料庫（matlib.js HOST_MAX_BYTES）
                constexpr size_t max_mystyles_chunks = 4096;
                static const std::string import_what = "貼回的 AI 圖", import_undone = "這次沒有換圖";
                static const std::string mine_what   = "「我的款式」存檔", mine_undone = "這次沒有存檔";
                const boost::filesystem::path mine_path = boost::filesystem::path(data_dir()) / "phototile" / "my_styles.json";
                if (command_str.compare("phototile_ai_import_begin") == 0) {
                    m_photo_tile_import_id = root.get<std::string>("data.id", "");
                    photo_tile_upload_begin(m_photo_tile_import, root.get<size_t>("data.size", 0), root.get<size_t>("data.chunks", 0),
                                            max_import_bytes, max_import_chunks, import_what, import_undone);
                }
                else if (command_str.compare("phototile_ai_import_chunk") == 0) {
                    photo_tile_upload_chunk(m_photo_tile_import, root.get<size_t>("data.index", size_t(-1)),
                                            root.get<std::string>("data.base64", ""), import_what, import_undone);
                }
                else if (command_str.compare("phototile_ai_import_end") == 0) {
                    const std::string id = m_photo_tile_import_id;
                    m_photo_tile_import_id.clear();
                    const auto imported = [this, &id](bool ok, const std::string& message) {
                        photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.aiImported && "
                            "window.PINGPhotoTile.aiImported({id:\"") + ping_js_escape(id) + "\",ok:" + (ok ? "true" : "false") +
                            ",message:\"" + ping_js_escape(message) + "\"});");
                    };
                    std::vector<unsigned char> bytes;
                    std::string why;
                    if (!photo_tile_upload_end(m_photo_tile_import, bytes, why, import_what, import_undone)) {
                        imported(false, why);
                        return "";
                    }
                    static const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
                    if (bytes.size() < sizeof(png_sig) || std::memcmp(bytes.data(), png_sig, sizeof(png_sig)) != 0) {
                        imported(false, "貼回的圖不是 PNG，這次沒有換圖（原本的圖沒動）。");
                        return "";
                    }
                    const boost::filesystem::path ai_path = boost::filesystem::temp_directory_path() /
                        boost::filesystem::unique_path("PING_photo_tile_ai_%%%%-%%%%-%%%%.png");
                    if (!photo_tile_write_file_atomic(ai_path, bytes, false)) {
                        imported(false, "貼回的圖暫存檔寫入失敗，這次沒有換圖（原本的圖沒動）。");
                        return "";
                    }
                    // 同 phototile_ai_generate 成功那段：原照片只在第一次記；暫存檔同那條路不刪（壓平的背景執行緒可能正在讀舊的那張）
                    if (m_photo_tile_origin_path.empty())
                        m_photo_tile_origin_path = m_photo_tile_source_path;
                    m_photo_tile_source_path = ai_path.string();
                    m_photo_tile_ai_path     = ai_path.string();
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile 貼回 AI 圖：" << bytes.size() << " bytes，來源已切換為 " << m_photo_tile_source_path;
                    imported(true, "");
                }
                else if (command_str.compare("phototile_source_origin") == 0) {
                    const auto replied = [this](bool ok, const std::string& message) {
                        photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.sourceOrigin && "
                            "window.PINGPhotoTile.sourceOrigin({ok:") + (ok ? "true" : "false") + ",message:\"" + ping_js_escape(message) + "\"});");
                    };
                    boost::system::error_code ec;
                    // origin 空＝目前的來源就是原圖（見 m_photo_tile_origin_path 的註解）
                    const std::string& origin = m_photo_tile_origin_path.empty() ? m_photo_tile_source_path : m_photo_tile_origin_path;
                    if (origin.empty() || !boost::filesystem::exists(boost::filesystem::path(origin), ec)) {
                        replied(false, "找不到原本那張照片，請重新選一次照片。");
                        return "";
                    }
                    m_photo_tile_source_path = origin;
                    m_photo_tile_ai_path.clear();   // AI 圖留在頁面的清單上；點回它＝重新貼回（走 phototile_ai_import_*）
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile 回到原圖：來源已切換為 " << m_photo_tile_source_path;
                    replied(true, "");
                }
                else if (command_str.compare("phototile_mystyles_load") == 0) {
                    std::string reply;
                    boost::system::error_code ec;
                    if (!boost::filesystem::exists(mine_path, ec)) {
                        reply = "{ok:true,exists:false}";
                    } else {
                        const boost::uintmax_t size = boost::filesystem::file_size(mine_path, ec);
                        std::vector<unsigned char> bytes(ec || size > max_mystyles_bytes ? 0 : static_cast<size_t>(size));
                        boost::nowide::ifstream input(mine_path.string(), std::ios::binary);
                        if (bytes.empty() || !input || !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
                            BOOST_LOG_TRIVIAL(warning) << "PhotoTile 我的款式：不載入 " << mine_path.string() << " size=" << size;
                            reply = std::string("{ok:false,message:\"") +
                                    ping_js_escape("「我的款式」檔案是空的、讀不到、或超過 512 KB，這次沒有載入（原檔未動）。") + "\"}";
                        } else {
                            std::string encoded(boost::beast::detail::base64::encoded_size(bytes.size()), '\0');
                            encoded.resize(boost::beast::detail::base64::encode(&encoded[0], bytes.data(), bytes.size()));
                            reply = "{ok:true,exists:true,size:" + std::to_string(bytes.size()) + ",base64:\"" + encoded + "\"}";
                        }
                    }
                    photo_tile_page_script("window.PINGPhotoTile && window.PINGPhotoTile.mystylesLoaded && "
                                           "window.PINGPhotoTile.mystylesLoaded(" + reply + ");");
                }
                else if (command_str.compare("phototile_mystyles_save_begin") == 0) {
                    photo_tile_upload_begin(m_photo_tile_mystyles, root.get<size_t>("data.size", 0), root.get<size_t>("data.chunks", 0),
                                            max_mystyles_bytes, max_mystyles_chunks, mine_what, mine_undone);
                }
                else if (command_str.compare("phototile_mystyles_save_chunk") == 0) {
                    photo_tile_upload_chunk(m_photo_tile_mystyles, root.get<size_t>("data.index", size_t(-1)),
                                            root.get<std::string>("data.base64", ""), mine_what, mine_undone);
                }
                else {   // phototile_mystyles_save_end
                    const auto saved = [this](bool ok, const std::string& message, const std::string& path) {
                        photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.mystylesSaved && "
                            "window.PINGPhotoTile.mystylesSaved({ok:") + (ok ? "true" : "false") + ",message:\"" +
                            ping_js_escape(message) + "\",path:\"" + ping_js_escape(path) + "\"});");
                    };
                    std::vector<unsigned char> bytes;
                    std::string why;
                    if (!photo_tile_upload_end(m_photo_tile_mystyles, bytes, why, mine_what, mine_undone)) {
                        saved(false, why, "");
                        return "";
                    }
                    if (!photo_tile_write_file_atomic(mine_path, bytes, true)) {
                        saved(false, "「我的款式」寫入失敗，這次沒有存檔（原檔未動）。", "");
                        return "";
                    }
                    BOOST_LOG_TRIVIAL(info) << "PhotoTile 我的款式：已存 " << bytes.size() << " bytes → " << mine_path.string();
                    saved(true, "", mine_path.string());
                }
            }
            else if (command_str.compare("get_recent_projects") == 0) {
                if (mainframe) {
                    if (mainframe->m_webview) {
                        mainframe->m_webview->SendRecentList(INT_MAX);
                    }
                }
            }
            // else if (command_str.compare("modelmall_model_advise_get") == 0) {
            //     if (mainframe && this->app_config->get("staff_pick_switch") == "true") {
            //         if (mainframe->m_webview) {
            //             mainframe->m_webview->SendDesignStaffpick(has_model_mall());
            //         }
            //     }
            // }
            // else if (command_str.compare("modelmall_model_open") == 0) {
            //     if (root.get_child_optional("data") != boost::none) {
            //         pt::ptree data_node = root.get_child("data");
            //         boost::optional<std::string> id = data_node.get_optional<std::string>("id");
            //         if (id.has_value() && mainframe->m_webview) {
            //             mainframe->m_webview->OpenModelDetail(id.value(), m_agent);
            //         }
            //     }
            // }
            else if (command_str.compare("homepage_open_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> path = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_open_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_remove_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_all_recentfile") == 0) {
                this->request_remove_project("");
            }
            else if (command_str.compare("homepage_explore_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value())
                    {
                        boost::filesystem::path NowFile(path.value());

                        std::string FilePath = NowFile.make_preferred().string();
                        desktop_open_any_folder(FilePath);
                    }
                }
            }
            else if (command_str.compare("homepage_open_hotspot") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> url = data_node.get_optional<std::string>("url");
                    if (url.has_value()) {
                        this->request_open_project(url.value());
                    }
                }
            }
            else if (command_str.compare("begin_network_plugin_download") == 0) {
                CallAfter([this] { wxGetApp().ShowDownNetPluginDlg(); });
            }
            else if (command_str.compare("get_web_shortcut") == 0) {
                if (root.get_child_optional("key_event") != boost::none) {
                    pt::ptree key_event_node = root.get_child("key_event");
                    auto keyCode = key_event_node.get<int>("key");
                    auto ctrlKey = key_event_node.get<bool>("ctrl");
                    auto shiftKey = key_event_node.get<bool>("shift");
                    auto cmdKey = key_event_node.get<bool>("cmd");

                    wxKeyEvent e(wxEVT_CHAR_HOOK);
#ifdef __APPLE__
                    e.SetControlDown(cmdKey);
                    e.SetRawControlDown(ctrlKey);
#else
                    e.SetControlDown(ctrlKey);
#endif
                    e.SetShiftDown(shiftKey);
                    keyCode     = keyCode == 188 ? ',' : keyCode;
                    e.m_keyCode = keyCode;
                    e.SetEventObject(mainframe);
                    wxPostEvent(mainframe, e);
                }
            }
            else if (command_str.compare("userguide_wiki_open") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("url");
                    if (path.has_value()) {
                        wxLaunchDefaultBrowser(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_open_ccabin") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("file");
                    if (path.has_value()) {
                        std::string Fullpath = resources_dir() + "/web/homepage/model/" + path.value();

                        this->request_open_project(Fullpath);
                    }
                }
            }
            else if (command_str.compare("common_openurl") == 0) {
                boost::optional<std::string> path      = root.get_optional<std::string>("url");
                if (path.has_value()) {
                    wxLaunchDefaultBrowser(path.value());
                }
            } 
            else if (command_str.compare("homepage_makerlab_get") == 0) {
                //if (mainframe->m_webview) { mainframe->m_webview->SendMakerlabList(); }
            }
            else if (command_str.compare("makerworld_model_open") == 0) 
            {
                if (root.get_child_optional("model") != boost::none) {
                    pt::ptree                    data_node = root.get_child("model");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("url");
                    if (path.has_value()) 
                    { 
                        wxString realurl = from_u8(url_decode(path.value()));
                        wxGetApp().request_model_download(realurl);
                    }
                }
            }
        }
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "parse json cmd failed " << cmd;
        return "";
    }
    return "";
}

void GUI_App::handle_script_message(std::string msg)
{
    try {
        json j = json::parse(msg);
        if (j.contains("command")) {
            wxString cmd = j["command"];
            if (cmd == "user_login") {
                if (m_agent) {
                    m_agent->change_user(j.dump());
                    if (m_agent->is_user_login()) {
                        request_user_login(1);
                    }
                }
            }
        }
    }
    catch (...) {
        ;
    }
}

void GUI_App::request_model_download(wxString url)
{
    if (plater_) {
        plater_->request_model_download(url);
    }
}

//BBS download project by project id
void GUI_App::download_project(std::string project_id)
{
    if (plater_) {
        plater_->request_download_project(project_id);
    }
}

void GUI_App::request_project_download(std::string project_id)
{
    if (!check_login()) return;

    download_project(project_id);
}

void GUI_App::request_open_project(std::string project_id)
{
    if (plater()->is_background_process_slicing()) {
        Slic3r::GUI::show_info(nullptr, _L("new or open project file is not allowed during the slicing process!"), _L("Open Project"));
        return;
    }

    if (project_id == "<new>")
        plater()->new_project();
    else if (project_id.empty())
        plater()->load_project();
    else if (std::find_if_not(project_id.begin(), project_id.end(),
        [](char c) { return std::isdigit(c); }) == project_id.end())
        ;
    else if (boost::algorithm::starts_with(project_id, "http"))
        ;
    else
        CallAfter([this, project_id] { mainframe->open_recent_project(-1, wxString::FromUTF8(project_id)); });
}


// PING(2026-09-09，牌 c-0909-TH-01)：見 .hpp。
// 延遲重試：第一版用 CallAfter 立刻重排，實測 3 次都在載入的巢狀事件迴圈裡跑掉（21:26:02 log），板還是空的就放棄了。
// 改 wxTimer 一次性 500 ms；timer 在自己的 handler 裡不能 delete，交給 CallAfter 收。
static void ping_phototile_thumbnail_retry_later(GUI_App* app, const std::string& project_path, int retries_left)
{
    wxTimer* t = new wxTimer();
    t->Bind(wxEVT_TIMER, [app, project_path, retries_left, t](wxTimerEvent&) {
        app->CallAfter([t] { delete t; });
        app->ping_phototile_write_plate_thumbnail(project_path, retries_left);
    });
    t->StartOnce(500);
}

void GUI_App::ping_phototile_write_plate_thumbnail(const std::string& project_path, int retries_left)
{
    if (plater() == nullptr || mainframe == nullptr)
        return;
    PartPlate* plate = plater()->get_partplate_list().get_plate(0);
    bool same_project = false;
    {
        boost::system::error_code ec;
        const boost::filesystem::path cur(into_u8(plater()->get_project_filename(".3mf")));
        const boost::filesystem::path want(project_path);
        same_project = !cur.empty() && boost::filesystem::exists(cur, ec) && boost::filesystem::exists(want, ec) &&
                       boost::filesystem::equivalent(cur, want, ec) && !ec;
    }
    if (plate == nullptr || plate->empty() || !same_project) {
        if (retries_left > 0) {
            ping_phototile_thumbnail_retry_later(this, project_path, retries_left - 1);
        } else {
            BOOST_LOG_TRIVIAL(warning) << "PING plate thumbnail: give up after retries: plate_empty=" << (plate == nullptr || plate->empty())
                                       << " same_project=" << same_project << " cur=" << into_u8(plater()->get_project_filename(".3mf"))
                                       << " want=" << project_path;
        }
        return;
    }
    plater()->update_all_plate_thumbnails(true);   // 與 Ctrl+S（export_3mf）同一條渲染路徑
    if (!plate->thumbnail_data.is_valid()) {
        BOOST_LOG_TRIVIAL(warning) << "PING plate thumbnail: render produced no data for " << project_path;
        return;
    }
    if (bbs_3mf_add_plate_thumbnail(project_path.c_str(), plate->thumbnail_data)) {
        mainframe->refresh_recent_project_thumbnail(wxString::FromUTF8(project_path));
        BOOST_LOG_TRIVIAL(warning) << "PING plate thumbnail: written and recent-list refreshed: " << project_path;   // warning 等級＝log 預設等級看得到
    }
}

/* 照片磚機型的安裝（**2026-09-07 Eric 裁 #99 Q3 甲後重寫**）。

   ▍原本是什麼、為什麼砍掉
   原本是 `ping_install_photo_tile_printers()`：一進工作室就掃全部 system preset，把
   `printer_model` 含「同進照片磚」的**整組裝上**（FD300×2＋FF600×3＋FF800×3＝3 個機型 8 個
   口徑變體）。出發點是 Eric 2026-08-07 的「不要等到匯出才說找不到機型」，但代價就是回報中心
   **#99**：只有 FD300 的人也會被塞進 FF600／FF800 兩台永遠用不到的機器。
   🔴 而且**弄不掉**——選機精靈是整組覆蓋（`GuideFrame::apply_config` → `set_vendors`），
   取消勾選是真的移除，但下次一開工作室這裡又會判「未安裝」而裝回去，中間沒有任何訊息。
   （舊註解寫「使用者自己取消勾選的情形也不強制裝回」，與 `AppConfig::set_variant(false)`
   會 erase 的實際行為不符，是錯的。）

   ▍現在是什麼
   照片磚機**已從「選擇 3D 列印機」整批移除**（`resources/web/guide/21|24` 的
   `PingIsPhotoTileModel`）——它不是使用者要挑的機器，是照片磚功能的內部載體
   （64 個虛擬料槽＋零回抽）。所以這裡改成**只在要切過去的那一刻、只裝那一台**。
   Eric 2026-08-07「不要等到匯出才說找不到機型」那條沒有被丟掉，換成了進門的
   `ping_photo_tile_entry_block_reason()`：機型做不到就在**進門當下**講，而不是做完一整張磚
   才講。 */

// 只裝一台（`preset_name` ＝ 解析器挑出來的目標機）。回傳 true＝這次真的新裝了。
static bool ping_install_photo_tile_printer(const std::string& preset_name)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    AppConfig*    config = wxGetApp().app_config;
    if (bundle == nullptr || config == nullptr || preset_name.empty())
        return false;

    const Preset* preset = bundle->printers.find_preset(preset_name, false);
    if (preset == nullptr || !preset->is_system || preset->vendor == nullptr)
        return false;
    const ConfigOptionString* pm = preset->config.option<ConfigOptionString>("printer_model");
    const ConfigOptionString* pv = preset->config.option<ConfigOptionString>("printer_variant");
    if (pm == nullptr || pv == nullptr || pm->value.empty() || pv->value.empty())
        return false;
    if (config->get_variant(preset->vendor->id, pm->value, pv->value))
        return false;                       // 已裝＝什麼都不要動（含使用者自己勾的情形）

    config->set_variant(preset->vendor->id, pm->value, pv->value, true);
    bundle->load_installed_printers(*config);
    config->save();
    BOOST_LOG_TRIVIAL(info) << "Photo tile: installed printer on demand: " << preset_name;
    return true;
}

/* 這台機的**家族**有沒有任何照片磚機。進門當下還不知道口徑與 dual/quad
   （那要等工作室回報），所以只問到家族這一層——口徑對不上的情形仍由匯出時的解析器
   誠實回報（「找不到符合這個模式與口徑的…」）。 */
static bool ping_family_has_photo_tile_printer(const std::string& family)
{
    if (family.empty())
        return false;
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return false;
    for (const Preset& preset : bundle->printers) {
        if (!preset.is_system)
            continue;
        const PhotoTileCapability cap = photo_tile_capability_of(preset);
        if (cap.is_photo_tile && cap.family == family)
            return true;
    }
    return false;
}

/* 進門守門（Eric 2026-09-07 裁 #99 Q2 乙）：**入口保持可點，點下去給一個有出口的指引**。
   回傳空字串＝這台機做得到，直接進工作室；非空＝擋下來的理由（給對話框顯示）。
   為什麼是「說明」不是「藏起來」：入口神秘地不存在，是最難問出口的一種壞掉。
   ⚠ 上方列那顆鈕只在同進機顯示，但**拖圖片直入**這條路任何機型都走得到 ⇒ 守門要放在這裡
     （兩條路都經過 open_photo_tile()），不能只靠鈕的顯示條件。 */
static std::string ping_photo_tile_entry_block_reason()
{
    const PhotoTileCapability cap = photo_tile_capability_of_selected_printer();
    const std::string model = cap.printer_model.empty() ? std::string("（未知）") : cap.printer_model;

    if (cap.is_classic)
        return "「" + model + "」是 Classic 前代機型，韌體不支援照片磚。\n\n"
               "照片磚是靠 M6051／M6052 這組逐段混色指令印出來的，前代韌體沒有這組指令"
               "（前代的 M6050 只能整支設一個固定混色比例，做不出一張圖需要的逐段變化）。";
    if (!cap.is_mixing)
        return "照片磚需要「同進」機型才印得出來（兩料或四料同時進同一個噴頭，靠混色比例做出色階）。\n\n"
               "你目前選的是「" + model + "」。";
    if (!ping_family_has_photo_tile_printer(cap.family))
        return "「" + model + "」目前還沒有對應的照片磚機型。\n\n"
               "照片磚現在支援 FD300（雙料）與 FF600／FF800（四料）。"
               "它需要一組專屬設定（64 個虛擬料槽＋零回抽），不是換個參數就能用同一台機器跑。";
    return std::string();
}

void GUI_App::open_photo_tile(const wxString& image_path)
{
    if (!mainframe || !mainframe->m_webview)
        return;

    /* #99 Q2 乙：做不到就在**進門當下**說，並給兩個出口（去選機／還是先進去看看）——
       不要讓人做完一整張磚，到上盤那一步才知道沒有機器可以放。 */
    const std::string block_reason = ping_photo_tile_entry_block_reason();
    if (!block_reason.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Photo tile: entry guidance shown: " << block_reason;
        MessageDialog dlg(mainframe,
                          wxString::FromUTF8(block_reason + "\n\n"
                              "你可以先到「選擇 3D 列印機」把機型加進來，或直接用左上角的印表機下拉切換。"),
                          wxString::FromUTF8("照片磚"),
                          wxOK | wxCANCEL | wxICON_INFORMATION);
        dlg.SetButtonLabel(wxID_OK, wxString::FromUTF8("開啟「選擇 3D 列印機」"));
        dlg.SetButtonLabel(wxID_CANCEL, wxString::FromUTF8("還是先進去看看"));
        if (dlg.ShowModal() == wxID_OK) {
            CallAfter([this] { run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS); });
            return;                         // 去換機型＝這次不進工作室
        }
        // 選「先進去看看」＝照樣開（入口保持可點），只是他已經知道最後會卡在哪。
    }

    /* PING(2026-09-09，Eric「如果這是設計上的問題，那是否不要去清除它呢？」)：**沒帶新圖＝續用工作室**。
       實錄：產生→回列印板→按側欄「照片磚」要改圈數，圖與設定全沒了——因為這裡把來源清掉、
       WebViewPanel::ShowPhotoTile 又把頁面重載。續用時：不取消現役 job、不清來源／風格化／AI 紀錄、
       不刪記帳暫存、頁面不重載（見 ShowPhotoTile）。真的換圖（帶路徑進來，或頁內開檔／貼上走
       phototile_image_*）才照原規則作廢舊圖。已知邊界：中途去過首頁＝頁面已被頂掉，還是會空（要救得靠從 3MF 讀回，另案）。 */
    const bool resume_studio = image_path.IsEmpty();
    if (resume_studio)
        BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：續用（沒帶新圖，不清狀態、不重載）source=" << m_photo_tile_source_path;

    /* 覆審 I-3（配套）：進工作室（含拖圖直入）＝任何現役生成作廢——同 phototile_home，
       遲到的結果不得上盤。（續用時不作廢：頁面還在顯示它的進度。） */
    if (!resume_studio && !m_photo_tile_active_job.empty()) {
        const std::string leaving = m_photo_tile_active_job;
        m_photo_tile_active_job.clear();
        if (m_photo_tile_host)
            m_photo_tile_host->cancel(leaving);
        BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：重新進入，取消現役 job=" << leaving;
    }

    /* C-2 接線設計（0804）：C++ 端持有「目前這張圖」的路徑。有路徑的入口（拖放/開檔）
       記在這裡；頁內選檔/貼上由 phototile_image_begin|chunk|end 落暫存檔後更新。
       空路徑入口（首頁按鈕）＝清掉——避免舊圖殘留造成「預覽是新圖、生成用舊圖」。 */
    if (!resume_studio) {                       // PING 0909：續用不清（見上）
    m_photo_tile_source_path = into_u8(image_path);
    m_photo_tile_origin_path.clear();          // 甲案：同上，換圖即作廢風格化原圖記錄
    m_photo_tile_ai_path.clear();              // 另存 AI 圖：同上

    /* 覆審 I-7（記帳制順手清，🟡「覆蓋不刪」）：來源已改指別處＝記帳那顆暫存檔沒人用了。
       只刪自己記過帳的；上面剛清掉 active（真實路徑永不入帳＝這裡刪不到使用者的檔）。 */
    if (!m_photo_tile_owned_temp.empty() && m_photo_tile_owned_temp != m_photo_tile_source_path) {
        try { boost::filesystem::remove(boost::filesystem::path(m_photo_tile_owned_temp)); }
        catch (...) {}
        m_photo_tile_owned_temp.clear();
    }
    }

    /* C-2 第 3 項 W3：進工作室＝立即預熱。使用者已經打開照片磚工作室＝比「選中照片磚機」
       更強的意圖訊號（而且這條路接住「app 一開就拖照片」——W1 的 15 秒還沒到）。
       不設機型條件：工作室本來就能在非照片磚機上用（上盤時 resolver 會自動切機）。
       非阻塞：引擎建立全程非同步，頁面照常立刻顯示。 */
    photo_tile_warmup_now("進入工作室");

    BOOST_LOG_TRIVIAL(info) << "Opening embedded photo tile studio";
    mainframe->select_tab(size_t(MainFrame::tpHome));
    mainframe->m_webview->ShowPhotoTile(image_path);
}

bool GUI_App::open_step_repair()
{
    if (!mainframe || !mainframe->m_webview)
        return false;

    /* 【2026-08-24・牌 c-0824-VIBE-02】平台 guard（契約 §7）。
       修補頁靠虛擬主機 https://ping-resources.invalid 供應 10.8MB 的 wasm，
       而那個映射只在 WebView.cpp 的 __WIN32__ 分支裡設（SetVirtualHostMapping）。
       非 Windows 上這個頁面會走進 0823 查到的同一條死路：頁面開得起來、
       wasm 抓不到、任何檔一秒就回「無法自動修補」——**壞得像功能有問題，不像平台不支援**。
       與其讓人再查一次那個坑，不如在入口就說清楚。 */
#ifndef __WIN32__
    MessageDialog dlg(mainframe,
                      _L("The STEP repair tool is only available on Windows in this version."),
                      _L("STEP repair"), wxOK | wxICON_INFORMATION);
    dlg.ShowModal();
    BOOST_LOG_TRIVIAL(info) << "STEP repair blocked: virtual host mapping is Windows-only";
    return false;
#endif

    m_step_repair_export_buffer.clear();
    m_step_repair_export_expected_size = 0;
    m_step_repair_export_expected_chunks = 0;
    m_step_repair_export_next_chunk = 0;
    m_step_repair_export_active = false;
    m_step_repair_suggested_name.clear();

    BOOST_LOG_TRIVIAL(info) << "Opening vendored VibeCAD STEP repair core " << step_repair_pin().core_version;
    mainframe->select_tab(size_t(MainFrame::tpHome));
    mainframe->m_webview->ShowStepRepair();
    return true;
}

void GUI_App::request_remove_project(std::string project_id)
{
    mainframe->remove_recent_project(-1, wxString::FromUTF8(project_id));
}

void GUI_App::handle_http_error(unsigned int status, std::string body)
{
    // tips body size must less than 1024
    auto evt = new wxCommandEvent(EVT_HTTP_ERROR);
    evt->SetInt(status);
    evt->SetString(wxString(body));
    wxQueueEvent(this, evt);
}

void GUI_App::on_http_error(wxCommandEvent &evt)
{
    int status = evt.GetInt();

    int code = 0;
    std::string error;
    wxString result;
    if (status >= 400 && status < 500) {
        try {
        auto evt_str = evt.GetString();
        if (!evt_str.empty()) {
            json j = json::parse(evt_str.utf8_string());
            if (j.contains("code")) {
                if (!j["code"].is_null())
                    code = j["code"].get<int>();
            }
            if (j.contains("error"))
                if (!j["error"].is_null())
                    error = j["error"].get<std::string>();
        }
        } catch (...) {}
    }

    // Version limit
    if (code == HttpErrorVersionLimited) {
        MessageDialog msg_dlg(nullptr, _L("The version of PING Slicer is too low and needs to be updated to the latest version before it can be used normally."), "", wxAPPLY | wxOK);
        if (msg_dlg.ShowModal() == wxOK) {
        }

    }

    // request login
    if (status == 401) {
        if (m_agent) {
            if (m_agent->is_user_login()) {
                this->request_user_logout();

                if (!m_show_http_errpr_msgdlg) {
                    MessageDialog msg_dlg(nullptr, _L("Login information expired. Please login again."), "", wxAPPLY | wxOK);
                    m_show_http_errpr_msgdlg = true;
                    auto modal_result = msg_dlg.ShowModal();
                    if (modal_result == wxOK || modal_result == wxCLOSE) {
                        m_show_http_errpr_msgdlg = false;
                        return;
                    }
                }
            }
        }
        return;
    }
}

void GUI_App::enable_user_preset_folder(bool enable)
{
    if (enable) {
        std::string user_id = m_agent->get_user_id();
        app_config->set("preset_folder", user_id);
        GUI::wxGetApp().preset_bundle->update_user_presets_directory(user_id);
    } else {
        BOOST_LOG_TRIVIAL(info) << "preset_folder: set to empty";
        app_config->set("preset_folder", "");
        GUI::wxGetApp().preset_bundle->update_user_presets_directory(DEFAULT_USER_FOLDER_NAME);
    }
}

void GUI_App::on_update_machine_list(wxCommandEvent &evt)
{
    /* DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
     if (dev) {
         dev->add_user_subscribe();
     }*/
}

void GUI_App::on_user_login_handle(wxCommandEvent &evt)
{
    if (!m_agent) { return; }

    int online_login = evt.GetInt();
    m_agent->connect_server();
    // get machine list
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    boost::thread update_thread = boost::thread([this, dev] {
        dev->update_user_machine_list_info();
    });

    if (online_login) {
        remove_user_presets();
        enable_user_preset_folder(true);
        preset_bundle->load_user_presets(m_agent->get_user_id(), ForwardCompatibilitySubstitutionRule::Enable);
        mainframe->update_side_preset_ui();

        GUI::wxGetApp().mainframe->show_sync_dialog();
    }
}


void GUI_App::check_track_enable()
{
    // Orca: alaways disable track event
    if (m_agent) {
        m_agent->track_enable(false);
        m_agent->track_remove_files();
    }
}

void GUI_App::on_user_login(wxCommandEvent &evt)
{
    if (!m_agent) { return; }
    int online_login = evt.GetInt();
    // check privacy before handle
    check_privacy_version(online_login);
    check_track_enable();
}

bool GUI_App::is_studio_active()
{
    auto curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_active_point);
    if (diff.count() < STUDIO_INACTIVE_TIMEOUT) {
        return true;
    }
    return false;
}

void GUI_App::reset_to_active()
{
    last_active_point = std::chrono::system_clock::now();
}

void GUI_App::check_update(bool show_tips, int by_user)
{
    if (version_info.version_str.empty()) return;
    if (version_info.url.empty()) return;

    auto curr_version = Semver::parse(SLIC3R_VERSION);
    auto remote_version = Semver::parse(version_info.version_str);
    if (curr_version && remote_version && (*remote_version > *curr_version)) {
        if (version_info.force_upgrade) {
            wxGetApp().app_config->set_bool("force_upgrade", version_info.force_upgrade);
            wxGetApp().app_config->set("upgrade", "force_upgrade", true);
            wxGetApp().app_config->set("upgrade", "description", version_info.description);
            wxGetApp().app_config->set("upgrade", "version", version_info.version_str);
            wxGetApp().app_config->set("upgrade", "url", version_info.url);
            GUI::wxGetApp().enter_force_upgrade();
        }
        else {
            GUI::wxGetApp().request_new_version(by_user);
        }
    } else {
        wxGetApp().app_config->set("upgrade", "force_upgrade", false);
        if (show_tips)
            this->no_new_version();
    }
}

void GUI_App::check_new_version(bool show_tips, int by_user)
{
    return; // orca: not used, see check_new_version_sf
    std::string platform = "windows";

#ifdef __WINDOWS__
    platform = "windows";
#endif
#ifdef __APPLE__
    platform = "macos";
#endif
#ifdef __LINUX__
    platform = "linux";
#endif
    std::string query_params = (boost::format("?name=slicer&version=%1%&guide_version=%2%")
        % VersionInfo::convert_full_version(SLIC3R_VERSION)
        % VersionInfo::convert_full_version("0.0.0.1")
        ).str();

    std::string url = get_http_url(app_config->get_country_code()) + query_params;
    Slic3r::Http http = Slic3r::Http::get(url);

    http.header("accept", "application/json")
        .timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, show_tips, by_user](std::string body, unsigned) {
        try {
            json j = json::parse(body);
            if (j.contains("message")) {
                if (j["message"].get<std::string>() == "success") {
                    if (j.contains("software")) {
                        if (j["software"].empty() && show_tips) {
                            this->no_new_version();
                        }
                        else {
                            if (j["software"].contains("url")
                                && j["software"].contains("version")
                                && j["software"].contains("description")) {
                                version_info.url = j["software"]["url"].get<std::string>();
                                version_info.version_str = j["software"]["version"].get<std::string>();
                                version_info.description = j["software"]["description"].get<std::string>();
                            }
                            if (j["software"].contains("force_update")) {
                                version_info.force_upgrade = j["software"]["force_update"].get<bool>();
                            }
                            CallAfter([this, show_tips, by_user](){
                                this->check_update(show_tips, by_user);
                            });
                        }
                    }
                }
            }
        }
        catch (...) {
            ;
        }
            })
        .on_error([this](std::string body, std::string error, unsigned int status) {
            handle_http_error(status, body);
            BOOST_LOG_TRIVIAL(error) << "check new version error" << body;
    }).perform();
}

//parse the string, if it doesn't contain a valid version string, return invalid version.
Semver get_version(const std::string& str, const std::regex& regexp) {
    std::smatch match;
    if (std::regex_match(str, match, regexp)) {
        std::string version_cleaned = match[0];
        const boost::optional<Semver> version = Semver::parse(version_cleaned);
        if (version.has_value()) {
            return *version;
        }
    }
    return Semver::invalid();
}

namespace
{

struct UpdaterQuery
{
    std::string iid;
    std::string version;
    std::string os;
    std::string arch;
    std::string os_info;
};

std::string detect_updater_os()
{
#if defined(_WIN32)
    return "win";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__) || defined(__LINUX__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string detect_updater_arch()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "i386";
#else
    std::string arch = wxPlatformInfo::Get().GetArchName().ToStdString();
    boost::algorithm::to_lower(arch);
    if (arch.find("aarch64") != std::string::npos || arch.find("arm64") != std::string::npos)
        return "arm64";
    if (arch.find("x86_64") != std::string::npos || arch.find("amd64") != std::string::npos)
        return "x86_64";
    if (arch.find("i686") != std::string::npos || arch.find("i386") != std::string::npos || arch.find("x86") != std::string::npos)
        return "i386";
    return "unknown";
#endif
}

std::string detect_updater_os_info()
{
    wxString description = wxPlatformInfo::Get().GetOperatingSystemDescription();
#if defined(__LINUX__) || defined(__linux__)
    wxLinuxDistributionInfo distro = wxGetLinuxDistributionInfo();
    if (!distro.Id.empty()) {
        wxString normalized = distro.Id;
        if (!distro.Release.empty())
            normalized << " " << distro.Release;
        normalized.Trim(true);
        normalized.Trim(false);
        if (!normalized.empty())
            description = normalized;
    }
#endif
    if (description.empty())
        description = wxGetOsDescription();

    //Orca: workaround: wxGetOsVersion can't recognize Windows 11
    // For Windows, use actual version numbers to properly detect Windows 11
    // Windows 11 starts at build 22000
#if defined(_WIN32)
    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);
    if (micro >= 22000) {
        // replace Windows 10 with Windows 11
        description.Replace("Windows 10", "Windows 11");
    }
#endif
    std::string os_info = description.ToStdString();
    boost::replace_all(os_info, "\r", " ");
    boost::replace_all(os_info, "\n", " ");
    boost::algorithm::trim(os_info);
    if (os_info.size() > 120)
        os_info.resize(120);
    boost::algorithm::to_lower(os_info);
    return os_info;
}

std::string detect_updater_version()
{
    return SoftFever_VERSION;
}

std::string detect_updater_iid(AppConfig* config)
{
    if (config == nullptr)
        return {};
    return instance_id::ensure(*config);
}

std::string encode_uri_component(const std::string& value)
{
    static constexpr const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
            ch == '!' || ch == '*' || ch == '(' || ch == ')' || ch == '\'') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4) & 0xF]);
            out.push_back(hex[ch & 0xF]);
        }
    }
    return out;
}

std::string build_updater_query(const UpdaterQuery& query)
{
    std::vector<std::pair<std::string, std::string>> params;

    auto add_param = [&params](const char* key, const std::string& value) {
        if (!value.empty())
            params.emplace_back(key, encode_uri_component(value));
    };

    add_param("iid", query.iid);
    add_param("v", query.version);
    add_param("os", query.os);
    add_param("arch", query.arch);
    add_param("os_info", query.os_info);

    std::sort(params.begin(), params.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    if (params.empty())
        return {};

    std::string encoded;
    for (size_t idx = 0; idx < params.size(); ++idx) {
        if (idx > 0)
            encoded.push_back('&');
        encoded += params[idx].first;
        encoded.push_back('=');
        encoded += params[idx].second;
    }
    return encoded;
}

std::string base64url_encode(const unsigned char* data, std::size_t length)
{
    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(length));
    encoded.resize(boost::beast::detail::base64::encode(encoded.data(), data, length));
    std::replace(encoded.begin(), encoded.end(), '+', '-');
    std::replace(encoded.begin(), encoded.end(), '/', '_');
    while (!encoded.empty() && encoded.back() == '=')
        encoded.pop_back();
    return encoded;
}

std::optional<std::vector<unsigned char>> load_signature_key()
{
#if ORCA_UPDATER_SIG_KEY_AVAILABLE
    std::string key = ORCA_UPDATER_SIG_KEY_B64;
    boost::algorithm::trim(key);
    if (key.empty())
        return std::nullopt;

    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) { return std::isspace(ch); }), key.end());
    std::replace(key.begin(), key.end(), '-', '+');
    std::replace(key.begin(), key.end(), '_', '/');
    while (key.size() % 4 != 0)
        key.push_back('=');

    std::string decoded;
    decoded.resize(boost::beast::detail::base64::decoded_size(key.size()));
    auto decode_result = boost::beast::detail::base64::decode(decoded.data(), key.data(), key.size());
    if (!decode_result.second)
        return std::nullopt;
    decoded.resize(decode_result.first);

    return std::vector<unsigned char>(decoded.begin(), decoded.end());
#else
    return std::nullopt;
#endif
}

const std::optional<std::vector<unsigned char>>& get_signature_key()
{
    static std::optional<std::vector<unsigned char>> cached;
    static bool loaded = false;
    if (!loaded) {
        cached = load_signature_key();
        loaded = true;
    }
    return cached;
}

std::string extract_path_from_url(const std::string& url)
{
    if (url.empty())
        return "/latest";

    std::string path;
    const auto scheme_pos = url.find("://");
    if (scheme_pos != std::string::npos) {
        const auto path_pos = url.find('/', scheme_pos + 3);
        if (path_pos != std::string::npos)
            path = url.substr(path_pos);
        else
            path = "/";
    } else {
        path = url;
    }

    const auto fragment_pos = path.find('#');
    if (fragment_pos != std::string::npos)
        path = path.substr(0, fragment_pos);

    const auto query_pos = path.find('?');
    if (query_pos != std::string::npos)
        path = path.substr(0, query_pos);

    if (path.empty())
        path = "/";
    return path;
}

void maybe_attach_updater_signature(Http& http, const std::string& canonical_query, const std::string& request_url)
{
    if (canonical_query.empty())
        return;

    const auto& key = get_signature_key();
    if (!key || key->empty())
        return;

    const auto now   = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const std::string timestamp = std::to_string(now.time_since_epoch().count());
    const std::string path      = extract_path_from_url(request_url);

    std::string string_to_sign = "GET\n";
    string_to_sign += path;
    string_to_sign += "\n";
    string_to_sign += canonical_query;
    string_to_sign += "\n";
    string_to_sign += timestamp;

    unsigned int digest_length = 0;
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    if (HMAC(EVP_sha256(), key->data(), static_cast<int>(key->size()),
             reinterpret_cast<const unsigned char*>(string_to_sign.data()),
             string_to_sign.size(), digest, &digest_length) == nullptr || digest_length == 0)
        return;

    const std::string signature = base64url_encode(digest, digest_length);
    http.header("X-Orca-Ts", timestamp);
    http.header("X-Orca-Sig", "v1:" + signature);
}

} // namespace

void GUI_App::check_new_version_sf(bool show_tips, int by_user)
{
    return; // PING: 停用線上更新——本 fork 無自有更新伺服器，避免 startup(GUI_App L1089) 與手動選單(MainFrame) 打原 Orca SourceForge 通道、給同事無效/誤導的更新提示
    AppConfig* app_config = wxGetApp().app_config;
    bool       check_stable_only = app_config->get_bool("check_stable_update_only");
    auto version_check_url = app_config->version_check_url();

    UpdaterQuery query{
        detect_updater_iid(app_config),
        detect_updater_version(),
        detect_updater_os(),
        detect_updater_arch(),
        detect_updater_os_info()
    };

    const std::string query_string = build_updater_query(query);
    if (!query_string.empty()) {
        const bool has_query = version_check_url.find('?') != std::string::npos;
        if (!has_query)
            version_check_url.push_back('?');
        else if (!version_check_url.empty() && version_check_url.back() != '&' && version_check_url.back() != '?')
            version_check_url.push_back('&');
        version_check_url += query_string;
    }

    auto http = Http::get(version_check_url);
    maybe_attach_updater_signature(http, query_string, version_check_url);

    http.header("accept", "application/vnd.github.v3+json")
        .timeout_connect(5)
        .timeout_max(10)
        .on_error([&](std::string body, std::string error, unsigned http_status) {
          (void)body;
          BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%", "check_new_version_sf", http_status,
                                             error);
        })
        .on_complete([this, by_user, check_stable_only](std::string body, unsigned http_status) {
          if (http_status != 200)
            return;
          try {
            boost::trim(body);
            if (body.empty()) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            boost::property_tree::ptree root;
            std::stringstream           json_stream(body);
            boost::property_tree::read_json(json_stream, root);

            std::regex matcher("[0-9]+\\.[0-9]+(\\.[0-9]+)*(-[A-Za-z0-9]+)?(\\+[A-Za-z0-9]+)?");
            Semver    current_version = get_version(SoftFever_VERSION, matcher);
            Semver    best_pre(0, 0, 0);
            Semver    best_release(0, 0, 0);
            bool      best_pre_valid = false;
            bool      best_release_valid = false;
            std::string best_pre_url;
            std::string best_release_url;
            std::string best_release_content;
            std::string best_pre_content;

            auto consider_release = [&](const boost::property_tree::ptree& node) {
                auto tag_opt = node.get_optional<std::string>("tag_name");
                if (!tag_opt)
                    return;

                std::string tag = *tag_opt;
                if (!tag.empty() && tag.front() == 'v')
                    tag.erase(0, 1);

                Semver tag_version = get_version(tag, matcher);
                if (!tag_version.valid())
                    return;

                const bool is_prerelease = node.get_optional<bool>("prerelease").get_value_or(false);
                const std::string html_url = node.get_optional<std::string>("html_url").get_value_or(std::string());
                const std::string body_copy = node.get_optional<std::string>("body").get_value_or(std::string());

                if (is_prerelease) {
                    if (!best_pre_valid || best_pre < tag_version) {
                        best_pre        = tag_version;
                        best_pre_url    = html_url;
                        best_pre_content = body_copy;
                        best_pre_valid  = true;
                    }
                } else {
                    if (!best_release_valid || best_release < tag_version) {
                        best_release         = tag_version;
                        best_release_url     = html_url;
                        best_release_content = body_copy;
                        best_release_valid   = true;
                    }
                }
            };

            if (root.get_optional<std::string>("tag_name")) {
                consider_release(root);
            } else {
                for (const auto& child : root)
                    consider_release(child.second);
            }

            if (!best_release_valid && !best_pre_valid) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            if (best_pre_valid && best_release_valid && best_pre < best_release) {
                best_pre        = best_release;
                best_pre_url    = best_release_url;
                best_pre_content = best_release_content;
                best_pre_valid  = true;
            }

            const bool        prefer_release = check_stable_only || !best_pre_valid;
            const Semver&     chosen_version = prefer_release ? best_release : best_pre;
            const bool        chosen_valid   = prefer_release ? best_release_valid : best_pre_valid;

            if (!chosen_valid) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            if (current_version.valid() && chosen_version <= current_version) {
                if (by_user != 0)
                    this->no_new_version();
                return;
            }

            version_info.url           = prefer_release ? best_release_url : best_pre_url;
            version_info.version_str   = prefer_release ? best_release.to_string_sf() : best_pre.to_string_sf();
            version_info.description   = prefer_release ? best_release_content : best_pre_content;
            version_info.force_upgrade = false;

            wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
            evt->SetString((prefer_release ? best_release : best_pre).to_string());
            GUI::wxGetApp().QueueEvent(evt);
          } catch (...) {}
        });

    http.perform();
}

// return true if handled
bool GUI_App::process_network_msg(std::string dev_id, std::string msg)
{
    if (dev_id.empty()) {
        if (msg == "wait_info") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, wait_info";
            Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev)
                return true;
            MachineObject* obj = dev->get_selected_machine();
            if (obj && m_agent)
                m_agent->install_device_cert(obj->get_dev_id(), obj->is_lan_mode_printer());
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Retrieving printer information, please try again later."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "update_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, update_studio";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Please try updating PING Slicer and then try again."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "update_fixed_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, update_fixed_studio";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Please try updating PING Slicer and then try again."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "cert_expired") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, cert_expired";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("The certificate has expired. Please check the time settings or update PING Slicer and try again."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "cert_revoked") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, cert_revoked";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("The certificate is no longer valid and the printing functions are unavailable."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "update_firmware_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, firmware internal error";
            if (!m_show_error_msgdlg) {
                MessageDialog msg_dlg(nullptr, _L("Internal error. Please try upgrading the firmware and PING Slicer version. If the issue persists, contact support."), "", wxAPPLY | wxOK);
                m_show_error_msgdlg = true;
                msg_dlg.ShowModal();
                m_show_error_msgdlg = false;
            }
            return true;
        }
        else if (msg == "unsigned_studio") {
            BOOST_LOG_TRIVIAL(info) << "process_network_msg, unsigned_studio";
            MessageDialog
                msg_dlg(nullptr,
                        _L("To use PING Slicer with Bambu Lab printers, you need to enable LAN mode and Developer mode on your printer.\n\n"
                           "Please go to your printer's settings and:\n"
                           "1. Turn on LAN mode\n"
                           "2. Enable Developer mode\n\n"
                           "Developer mode allows the printer to work exclusively through local network access, "
                           "enabling full functionality with PING Slicer."),
                        _L("Network Plug-in Restriction"), wxAPPLY | wxOK);
            m_show_error_msgdlg = true;
            msg_dlg.ShowModal();
            m_show_error_msgdlg = false;
            return true;
        }
    }
    else if (msg == "device_cert_installed") {
        BOOST_LOG_TRIVIAL(info) << "process_network_msg, device_cert_installed";
        if (Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager()) {
            if (MachineObject* obj = dev->get_my_machine(dev_id)) {
                obj->update_device_cert_state(true);
            }
        }
        return true;
    }
    else if (msg == "device_cert_uninstalled") {
        BOOST_LOG_TRIVIAL(info) << "process_network_msg, device_cert_uninstalled";
        if (Slic3r::DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager()) {
            if (MachineObject* obj = dev->get_my_machine(dev_id)){
                obj->update_device_cert_state(false);
            }
        }
        return true;
    }

    return false;
}

//BBS pop up a dialog and download files
void GUI_App::request_new_version(int by_user)
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
    evt->SetString(GUI::from_u8(version_info.version_str));
    evt->SetInt(by_user);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::enter_force_upgrade()
{
    wxCommandEvent *evt = new wxCommandEvent(EVT_ENTER_FORCE_UPGRADE);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::set_skip_version(bool skip)
{
    BOOST_LOG_TRIVIAL(info) << "set_skip_version, skip = " << skip << ", version = " <<version_info.version_str;
    if (skip) {
        app_config->set("skip_version", version_info.version_str);
    }else {
        app_config->set("skip_version", "");
    }
}

void GUI_App::show_check_privacy_dlg(wxCommandEvent& evt)
{
    int online_login = evt.GetInt();
    PrivacyUpdateDialog privacy_dlg(this->mainframe, wxID_ANY, _L("Privacy Policy Update"));
    privacy_dlg.Bind(EVT_PRIVACY_UPDATE_CONFIRM, [this, online_login](wxCommandEvent &e) {
        app_config->set("privacy_version", privacy_version_info.version_str);
        app_config->set_bool("privacy_update_checked", true);
        request_user_handle(online_login);
        });
    privacy_dlg.Bind(EVT_PRIVACY_UPDATE_CANCEL, [this](wxCommandEvent &e) {
            app_config->set_bool("privacy_update_checked", false);
            if (m_agent) {
                m_agent->user_logout();
            }
        });

    privacy_dlg.set_text(privacy_version_info.description);
    privacy_dlg.on_show();
}

void GUI_App::on_show_check_privacy_dlg(int online_login)
{
    auto evt = new wxCommandEvent(EVT_CHECK_PRIVACY_SHOW);
    evt->SetInt(online_login);
    wxQueueEvent(this, evt);
}

bool GUI_App::check_privacy_update()
{
    if (privacy_version_info.version_str.empty() || privacy_version_info.description.empty()
        || privacy_version_info.url.empty()) {
        return false;
    }

    std::string local_privacy_ver = app_config->get("privacy_version");
    auto curr_version = Semver::parse(local_privacy_ver);
    auto remote_version = Semver::parse(privacy_version_info.version_str);
    if (curr_version && remote_version) {
        if (*remote_version > *curr_version || app_config->get("privacy_update_checked") != "true") {
            return true;
        }
    }
    return false;
}

void GUI_App::on_check_privacy_update(wxCommandEvent& evt)
{
    int online_login = evt.GetInt();
    bool result = check_privacy_update();
    if (result)
        on_show_check_privacy_dlg(online_login);
    else
        request_user_handle(online_login);
}

void GUI_App::check_privacy_version(int online_login)
{
    update_http_extra_header();
    std::string query_params = "?policy/privacy=00.00.00.00";
    std::string url = get_http_url(app_config->get_country_code()) + query_params;
    Slic3r::Http http = Slic3r::Http::get(url);

    http.header("accept", "application/json")
        .timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, online_login](std::string body, unsigned) {
            try {
                json j = json::parse(body);
                if (j.contains("message")) {
                    if (j["message"].get<std::string>() == "success") {
                        if (j.contains("resources")) {
                            for (auto it = j["resources"].begin(); it != j["resources"].end(); it++) {
                                if (it->contains("type")) {
                                    if ((*it)["type"] == std::string("policy/privacy")
                                        && it->contains("version")
                                        && it->contains("description")
                                        && it->contains("url")
                                        && it->contains("force_update")) {
                                        privacy_version_info.version_str = (*it)["version"].get<std::string>();
                                        privacy_version_info.description = (*it)["description"].get<std::string>();
                                        privacy_version_info.url = (*it)["url"].get<std::string>();
                                        privacy_version_info.force_upgrade = (*it)["force_update"].get<bool>();
                                        break;
                                    }
                                }
                            }
                            CallAfter([this, online_login]() {
                                auto evt = new wxCommandEvent(EVT_CHECK_PRIVACY_VER);
                                evt->SetInt(online_login);
                                wxQueueEvent(this, evt);
                            });
                        }
                    }
                }
            }
            catch (...) {
                request_user_handle(online_login);
            }
        })
        .on_error([this, online_login](std::string body, std::string error, unsigned int status) {
            request_user_handle(online_login);
            BOOST_LOG_TRIVIAL(error) << "check privacy version error" << body;
    }).perform();
}

void GUI_App::no_new_version()
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_NO_NEW_VERSION);
    GUI::wxGetApp().QueueEvent(evt);
}

std::string GUI_App::version_display = "";
std::string GUI_App::format_display_version()
{
    if (!version_display.empty()) return version_display;

    version_display = SoftFever_VERSION;
    return version_display;
}

std::string GUI_App::format_IP(const std::string& ip)
{
    std::string format_ip = ip;
    size_t pos_st = 0;
    size_t pos_en = 0;

    for (int i = 0; i < 2; i++) {
        pos_en = format_ip.find('.', pos_st + 1);
        if (pos_en == std::string::npos) {
            return ip;
        }
        format_ip.replace(pos_st, pos_en - pos_st, "***");
        pos_st = pos_en + 1;
    }

    return format_ip;
}

void GUI_App::show_dialog(wxString msg)
{
    if (m_info_dialog_content.empty()) {
        wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
        evt->SetString(msg);
        GUI::wxGetApp().QueueEvent(evt);
        m_info_dialog_content = msg;
    }
}

void  GUI_App::push_notification(const MachineObject* obj, wxString msg, wxString title, UserNotificationStyle style)
{
    if (this->is_enable_multi_machine())
    {
        if (m_device_manager && (obj != m_device_manager->get_selected_machine()))
        {
            return;
        }
    }

    if (style == UserNotificationStyle::UNS_NORMAL)
    {
        if (m_info_dialog_content.empty())
        {
            wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
            evt->SetString(msg);
            GUI::wxGetApp().QueueEvent(evt);
            m_info_dialog_content = msg;
        }
    }
    else if (style == UserNotificationStyle::UNS_WARNING_CONFIRM)
    {
        GUI::wxGetApp().CallAfter([msg, title]
            {
                GUI::MessageDialog msg_dlg(nullptr, msg, title, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
            });
    }
}

void GUI_App::reload_settings()
{
    if (preset_bundle && m_agent) {
        std::map<std::string, std::map<std::string, std::string>> user_presets;
        m_agent->get_user_presets(&user_presets);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << " cloud user preset number is: " << user_presets.size();
        preset_bundle->load_user_presets(*app_config, user_presets, ForwardCompatibilitySubstitutionRule::Enable);
        preset_bundle->save_user_presets(*app_config, get_delete_cache_presets());
        mainframe->update_side_preset_ui();
    }
}

//BBS reload when logout
void GUI_App::remove_user_presets()
{
    if (preset_bundle && m_agent) {
        preset_bundle->remove_users_preset(*app_config);

        // Not remove user preset cache
        //std::string user_id = m_agent->get_user_id();
        //preset_bundle->remove_user_presets_directory(user_id);

        //update ui
        mainframe->update_side_preset_ui();
    }
}

void GUI_App::sync_preset(Preset* preset)
{
    int result = -1;
    unsigned int http_code = 200;
    std::string updated_info;
    long long update_time = 0;
    // only sync user's preset
    if (!preset->is_user()) return;

    auto setting_id = preset->setting_id;
    std::map<std::string, std::string> values_map;
    if (setting_id.empty() && preset->sync_info.empty()) {
        if (m_create_preset_blocked[preset->type])
            return;
        int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
        if (!ret) {
            std::string new_setting_id = m_agent->request_setting_id(preset->name, &values_map, &http_code);
            if (!new_setting_id.empty()) {
                setting_id = new_setting_id;
                result = 0;
                auto update_time_str = values_map[ORCA_JSON_KEY_UPDATE_TIME];
                if (!update_time_str.empty())
                    update_time = std::atoll(update_time_str.c_str());
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]init: request_setting_id failed, http code "<<http_code;
                // do not post new preset this time if http code >= 400
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                } else
                    result = -1;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "[sync_preset]init: can not generate differed key-values";
            result = 0;
            updated_info = "hold";
        }
    }
    else if (preset->sync_info.compare("create") == 0) {
        if (m_create_preset_blocked[preset->type])
            return;
        int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
        if (!ret) {
            std::string new_setting_id = m_agent->request_setting_id(preset->name, &values_map, &http_code);
            if (!new_setting_id.empty()) {
                setting_id = new_setting_id;
                result = 0;
                auto update_time_str = values_map[ORCA_JSON_KEY_UPDATE_TIME];
                if (!update_time_str.empty())
                    update_time = std::atoll(update_time_str.c_str());
            } else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]create: request_setting_id failed, http code "<<http_code;
                // do not post new preset this time if http code >= 400
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                }
                else
                    result = -1;
            }
        } else {
            BOOST_LOG_TRIVIAL(trace) << "[sync_preset]create: can not generate differed preset";
        }
    } else if (preset->sync_info.compare("update") == 0) {
        if (!setting_id.empty()) {
            int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
            if (!ret) {
                if (auto iter = values_map.find(BBL_JSON_KEY_BASE_ID); iter != values_map.end() && iter->second == setting_id) {
                    //clear the setting_id in this case ???
                    setting_id.clear();
                    result = 0;
                }
                else {
                result = m_agent->put_setting(setting_id, preset->name, &values_map, &http_code);
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                    BOOST_LOG_TRIVIAL(error) << "[sync_preset] put setting_id = " << setting_id << " failed, http_code = " << http_code;
                } else {
                        auto update_time_str = values_map[ORCA_JSON_KEY_UPDATE_TIME];
                        if (!update_time_str.empty())
                            update_time = std::atoll(update_time_str.c_str());
                }
                }

            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]update: can not generate differed key-values, we need to skip this preset "<< preset->name;
                result = 0;
            }
        }
        else {
            //clear the sync_info
            result = 0;
        }
    }

    if (http_code >= 400 && values_map["code"] == "14") { // Limit
        m_create_preset_blocked[preset->type] = true;
        CallAfter([this] {
            plater()->get_notification_manager()->push_notification(NotificationType::BBLUserPresetExceedLimit);
            static bool dialog_notified = false;
            if (dialog_notified)
                return;
            dialog_notified = true;
            if (mainframe == nullptr)
                return;
            auto msg = _L("The number of user presets cached in the cloud has exceeded the upper limit, newly created user presets can only be used locally.");
            MessageDialog(mainframe, msg, _L("Sync user presets"), wxICON_WARNING | wxOK).ShowModal();
        });
        return; // this error not need hold, and should not hold
    }

    // update sync_info preset info in file
    if (result == 0) {
        //PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        if (!this->preset_bundle) return;

        BOOST_LOG_TRIVIAL(trace) << "sync_preset: sync operation: " << preset->sync_info << " success! preset = " << preset->name;
        if (preset->type == Preset::Type::TYPE_FILAMENT) {
            preset_bundle->filaments.set_sync_info_and_save(preset->name, setting_id, updated_info, update_time);
        } else if (preset->type == Preset::Type::TYPE_PRINT) {
            preset_bundle->prints.set_sync_info_and_save(preset->name, setting_id, updated_info, update_time);
        } else if (preset->type == Preset::Type::TYPE_PRINTER) {
            preset_bundle->printers.set_sync_info_and_save(preset->name, setting_id, updated_info, update_time);
        }
    }
}

void GUI_App::start_sync_user_preset(bool with_progress_dlg)
{
    if (app_config->get_stealth_mode())
        return;

    if (!m_agent || !m_agent->is_user_login()) return;
    if(!m_agent->get_cloud_agent())
        return;

    // has already start sync
    if (m_user_sync_token) return;

    ProgressFn progressFn;
    WasCancelledFn cancelFn;
    std::function<void(bool)> finishFn;

    BOOST_LOG_TRIVIAL(info) << "start_sync_service...";
    // BBS
    m_user_sync_token.reset(new int(0));
    if (with_progress_dlg) {
        auto dlg = new ProgressDialog(_L("Loading"), "", 100, this->mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
        dlg->Update(0, _L("Loading user preset"));
        progressFn = [this, dlg](int percent) {
            CallAfter([=]{
                dlg->Update(percent, _L("Loading user preset"));
            });
        };
        cancelFn = [this, dlg]() {
            return is_closing() || dlg->WasCanceled();
        };
        finishFn = [this, userid = m_agent->get_user_id(), dlg, t = std::weak_ptr<int>(m_user_sync_token)](bool ok) {
            CallAfter([=]{
                dlg->Destroy();
                if (ok && m_agent && t.lock() == m_user_sync_token && userid == m_agent->get_user_id()) reload_settings();
            });
        };
    }
    else {
        finishFn = [this, userid = m_agent->get_user_id(), t = std::weak_ptr<int>(m_user_sync_token)](bool ok) {
            CallAfter([=] {
                if (ok && m_agent && t.lock() == m_user_sync_token && userid == m_agent->get_user_id()) reload_settings();
            });
        };
        cancelFn = [this]() {
            return is_closing();
        };
    }

    m_sync_update_thread = Slic3r::create_thread(
        [this, progressFn, cancelFn, finishFn, t = std::weak_ptr<int>(m_user_sync_token)] {
            // get setting list, update setting list
            std::string version = preset_bundle->get_vendor_profile_version(PresetBundle::ORCA_DEFAULT_BUNDLE).to_string();
            if(!m_agent) return;
            int ret = m_agent->get_setting_list2(version, [this](auto info) {
                auto type = info[BBL_JSON_KEY_TYPE];
                auto name = info[BBL_JSON_KEY_NAME];
                auto setting_id = info[BBL_JSON_KEY_SETTING_ID];
                auto update_time_str = info[ORCA_JSON_KEY_UPDATE_TIME];
                long long update_time = 0;
                if (!update_time_str.empty())
                    update_time = std::atoll(update_time_str.c_str());
                if (type == "filament") {
                    return preset_bundle->filaments.need_sync(name, setting_id, update_time);
                } else if (type == "print") {
                    return preset_bundle->prints.need_sync(name, setting_id, update_time);
                } else if (type == "printer") {
                    return preset_bundle->printers.need_sync(name, setting_id, update_time);
                } else {
                    return true;
                }
            }, progressFn, cancelFn);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << __LINE__ << " get_setting_list2 ret = " << ret << " m_is_closing = " << m_is_closing;
            finishFn(ret == 0);

            int count = 0, sync_count = 0;
            std::vector<Preset> presets_to_sync;
            while (!t.expired()) {
                count++;
                if (count % 20 == 0) {
                    if (m_agent) {
                        if (!m_agent->is_user_login()) {
                            continue;
                        }
                        //sync preset
                        if (!preset_bundle) continue;

                        int total_count = 0;
                        sync_count = preset_bundle->prints.get_user_presets(preset_bundle, presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_preset(&preset);
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                            }
                        }
                        total_count += sync_count;

                        sync_count = preset_bundle->filaments.get_user_presets(preset_bundle, presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_preset(&preset);
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                            }
                        }
                        total_count += sync_count;

                        sync_count = preset_bundle->printers.get_user_presets(preset_bundle, presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_preset(&preset);
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                            }
                        }
                        total_count += sync_count;

                        if (total_count == 0) {
                            CallAfter([this] {
                                if (!is_closing())
                                    plater()->get_notification_manager()->close_notification_of_type(NotificationType::BBLUserPresetExceedLimit);
                            });
                        }

                        unsigned int http_code = 200;

                        /* get list witch need to be deleted*/
                        std::vector<string> delete_cache_presets = get_delete_cache_presets_lock();
                        for (auto it = delete_cache_presets.begin(); it != delete_cache_presets.end();) {
                            if ((*it).empty()) continue;
                            std::string del_setting_id = *it;
                            int result = m_agent->delete_setting(del_setting_id);
                            if (result == 0) {
                                preset_deleted_from_cloud(del_setting_id);
                                it = delete_cache_presets.erase(it);
                                m_create_preset_blocked = { false, false, false, false, false, false };
                                BOOST_LOG_TRIVIAL(trace) << "sync_preset: sync operation: delete success! setting id = " << del_setting_id;
                            }
                            else {
                                BOOST_LOG_TRIVIAL(info) << "delete setting = " <<del_setting_id << " failed";
                                it++;
                            }
                        }
                    }
                } else {
                    boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                }
            }
        });
}

void GUI_App::stop_sync_user_preset()
{
    if (!m_user_sync_token)
        return;

    m_user_sync_token.reset();
    if (m_sync_update_thread.joinable()) {
        if (is_closing())
            m_sync_update_thread.join();
        else
            m_sync_update_thread.detach();
    }
}

void GUI_App::start_http_server()
{
    if (!m_http_server.is_started())
        m_http_server.start();
}

void GUI_App::start_http_server(int port)
{
    if (port <= 0) {
        start_http_server();
        return;
    }

    if (m_http_server.is_started()) {
        if (m_http_server.get_port() == static_cast<boost::asio::ip::port_type>(port)) {
            return;
        }
        m_http_server.stop();
    }

    m_http_server.set_port(static_cast<boost::asio::ip::port_type>(port));
    m_http_server.start();
}

void GUI_App::stop_http_server()
{
    m_http_server.stop();
}

void GUI_App::switch_staff_pick(bool on)
{
    mainframe->m_webview->SendDesignStaffpick(on);
}

bool GUI_App::switch_language()
{
    if (select_language()) {
        recreate_GUI(_L("Switching application language") + dots);
        return true;
    } else {
        return false;
    }
}

#ifdef __linux__
static const wxLanguageInfo* linux_get_existing_locale_language(const wxLanguageInfo* language,
                                                                const wxLanguageInfo* system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = into_u8(language->CanonicalName.BeforeFirst('_'));

    // Call locale -a so we can parse the output to get the list of available locales
    // We expect lines such as "en_US.utf8". Pick ones starting with the language code
    // we are switching to. Lines with different formatting will be removed later.
    FILE* fp = popen("locale -a", "r");
    if (fp != NULL) {
        while (fgets(path, max_len, fp) != NULL) {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    // locales now contain all candidates for this language.
    // Sort them so ones containing anything about UTF-8 are at the end.
    std::sort(locales.begin(), locales.end(), [](const std::string& a, const std::string& b)
    {
        auto has_utf8 = [](const std::string & s) {
            auto S = boost::to_upper_copy(s);
            return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
        };
        return ! has_utf8(a) && has_utf8(b);
    });

    // Remove the suffix behind a dot, if there is one.
    for (std::string& s : locales)
        s = s.substr(0, s.find("."));

    // We just hope that dear Linux "locale -a" returns country codes
    // in ISO 3166-1 alpha-2 code (two letter) format.
    // https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
    // To be sure, remove anything not looking as expected
    // (any number of lowercase letters, underscore, two uppercase letters).
    locales.erase(std::remove_if(locales.begin(),
                                 locales.end(),
                                 [](const std::string& s) {
                                     return ! std::regex_match(s,
                                         std::regex("^[a-z]+_[A-Z]{2}$"));
                                 }),
                   locales.end());

    // Is there a candidate matching a country code of a system language? Move it to the end,
    // while maintaining the order of matches, so that the best match ends up at the very end.
    std::string temp_local = into_u8(system_language->CanonicalName.AfterFirst('_'));
    if (temp_local.size() >= 2) {
        temp_local = temp_local.substr(0, 2);
    }
    std::string system_country = "_" + temp_local;
    int cnt = locales.size();
    for (int i=0; i<cnt; ++i)
        if (locales[i].find(system_country) != std::string::npos) {
            locales.emplace_back(std::move(locales[i]));
            locales[i].clear();
        }

    // Now try them one by one.
    for (auto it = locales.rbegin(); it != locales.rend(); ++ it)
        if (! it->empty()) {
            const std::string &locale = *it;
            const wxLanguageInfo* lang = wxLocale::FindLanguageInfo(from_u8(locale));
            if (lang != nullptr && wxLocale::IsAvailable(lang->Language))
                return lang;
        }
    return language;
}
#endif

int GUI_App::GetSingleChoiceIndex(const wxString& message,
                                const wxString& caption,
                                const wxArrayString& choices,
                                int initialSelection)
{
#ifdef _WIN32
    wxSingleChoiceDialog dialog(nullptr, message, caption, choices);
    dialog.SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDlgDarkUI(&dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
#else
    return wxGetSingleChoiceIndex(message, caption, choices, initialSelection);
#endif
}

// select language from the list of installed languages
bool GUI_App::select_language()
{
	wxArrayString translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo*> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++ i) {
	    const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);
        if (langinfo != nullptr)
            language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
	std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo* l, const wxLanguageInfo* r) { return l->Description < r->Description; });

    wxArrayString names;
    names.Alloc(language_infos.size());

    // Some valid language should be selected since the application start up.
    const wxString active_language_code = current_language_code();
    const wxLanguageInfo* active_language_info = wxLocale::FindLanguageInfo(active_language_code);
    const wxLanguage current_language = active_language_info != nullptr ? wxLanguage(active_language_info->Language) : wxLanguage(m_wxLocale->GetLanguage());
    const wxString active_lang_prefix = active_language_code.BeforeFirst('_');
    int 		     init_selection   		= -1;
    int 			 init_selection_alt     = -1;
    int 			 init_selection_default = -1;
    for (size_t i = 0; i < language_infos.size(); ++ i) {
        if (wxLanguage(language_infos[i]->Language) == current_language)
        	// The dictionary matches the active language and country.
            init_selection = i;
        else if ((language_infos[i]->CanonicalName.BeforeFirst('_') == active_lang_prefix) ||
        		 // if the active language is Slovak, mark the Czech language as active.
        	     (language_infos[i]->CanonicalName.BeforeFirst('_') == "cs" && active_lang_prefix == "sk"))
        	// The dictionary matches the active language, it does not necessarily match the country.
        	init_selection_alt = i;
        if (language_infos[i]->CanonicalName.BeforeFirst('_') == "en")
        	// This will be the default selection if the active language does not match any dictionary.
        	init_selection_default = i;
        names.Add(language_infos[i]->Description);
    }
    if (init_selection == -1)
    	// This is the dictionary matching the active language.
    	init_selection = init_selection_alt;
    if (init_selection != -1)
    	// This is the language to highlight in the choice dialog initially.
    	init_selection_default = init_selection;

    const long index = GetSingleChoiceIndex(_L("Select the language"), _L("Language"), names, init_selection_default);
	// Try to load a new language.
    if (index != -1 && (init_selection == -1 || init_selection != index)) {
    	const wxLanguageInfo *new_language_info = language_infos[index];
    	if (this->load_language(new_language_info->CanonicalName, false)) {
			// Save language at application config.
            // Which language to save as the selected dictionary language?
            // 1) Hopefully the language set to wxTranslations by this->load_language(), but that API is weird and we don't want to rely on its
            //    stability in the future:
            //    wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
            // 2) Current locale language may not match the dictionary name, see GH issue #3901
            //    m_wxLocale->GetCanonicalName()
            // 3) new_language_info->CanonicalName is a safe bet. It points to a valid dictionary name.
			app_config->set("language", new_language_info->CanonicalName.ToUTF8().data());
    		return true;
        }
    }

    return false;
}

// Load gettext translation files and activate them at the start of the application,
// based on the "language" key stored in the application config.
bool GUI_App::load_language(wxString language, bool initial)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: language %2%, initial: %3%") %__FUNCTION__ %language %initial;
    if (initial) {
    	// There is a static list of lookup path prefixes in wxWidgets. Add ours.
	    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(from_u8(localization_dir()));
    	// Get the active language from PrusaSlicer.ini, or empty string if the key does not exist.
        language = app_config->get("language");
        if (language.empty()) {
            // PING Slicer: default UI language to Traditional Chinese (zh_TW) on first run.
            language = "zh_TW";
            app_config->set("language", "zh_TW");
        }
        if (! language.empty())
        	BOOST_LOG_TRIVIAL(info) << boost::format("language provided by OrcaSlicer.conf: %1%") % language;
        else {
            // Get the system language.
            const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
            if (lang_system != wxLANGUAGE_UNKNOWN) {
                m_language_info_system = wxLocale::GetLanguageInfo(lang_system);
#ifdef __WXMSW__
                WCHAR wszLanguagesBuffer[LOCALE_NAME_MAX_LENGTH];
                ::LCIDToLocaleName(LOCALE_USER_DEFAULT, wszLanguagesBuffer, LOCALE_NAME_MAX_LENGTH, 0);
                wxString lang(wszLanguagesBuffer);
                lang.Replace('-', '_');
                if (auto info = wxLocale::FindLanguageInfo(lang))
                    m_language_info_system = info;
#endif
                BOOST_LOG_TRIVIAL(info) << boost::format("System language detected (user locales and such): %1%") % m_language_info_system->CanonicalName.ToUTF8().data();
                // BBS set language to app config
                app_config->set("language", m_language_info_system->CanonicalName.ToUTF8().data());
            } else {
                {
                    // Allocating a temporary locale will switch the default wxTranslations to its internal wxTranslations instance.
                    wxLocale temp_locale;
                    temp_locale.Init();
                    // Set the current translation's language to default, otherwise GetBestTranslation() may not work (see the wxWidgets source code).
                    wxTranslations::Get()->SetLanguage(wxLANGUAGE_DEFAULT);
                    // Let the wxFileTranslationsLoader enumerate all translation dictionaries for PrusaSlicer
                    // and try to match them with the system specific "preferred languages".
                    // There seems to be a support for that on Windows and OSX, while on Linuxes the code just returns wxLocale::GetSystemLanguage().
                    // The last parameter gets added to the list of detected dictionaries. This is a workaround
                    // for not having the English dictionary. Let's hope wxWidgets of various versions process this call the same way.
                    wxString best_language = wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
                    if (!best_language.IsEmpty()) {
                        m_language_info_best = wxLocale::FindLanguageInfo(best_language);
                        BOOST_LOG_TRIVIAL(info) << boost::format("Best translation language detected (may be different from user locales): %1%") %
                                                        m_language_info_best->CanonicalName.ToUTF8().data();
                        app_config->set("language", m_language_info_best->CanonicalName.ToUTF8().data());
                    }
#ifdef __linux__
                    wxString lc_all;
                    if (wxGetEnv("LC_ALL", &lc_all) && !lc_all.IsEmpty()) {
                        // Best language returned by wxWidgets on Linux apparently does not respect LC_ALL.
                        // Disregard the "best" suggestion in case LC_ALL is provided.
                        m_language_info_best = nullptr;
                    }
#endif
                }
            }
        }
    }

	const wxLanguageInfo *language_info = language.empty() ? nullptr : wxLocale::FindLanguageInfo(language);
	if (! language.empty() && (language_info == nullptr || language_info->CanonicalName.empty())) {
		// Fix for wxWidgets issue, where the FindLanguageInfo() returns locales with undefined ANSII code (wxLANGUAGE_KONKANI or wxLANGUAGE_MANIPURI).
		language_info = nullptr;
    	BOOST_LOG_TRIVIAL(error) << boost::format("Language code \"%1%\" is not supported") % language.ToUTF8().data();
	}

	if (language_info != nullptr && language_info->LayoutDirection == wxLayout_RightToLeft) {
    	BOOST_LOG_TRIVIAL(trace) << boost::format("The following language code requires right to left layout, which is not supported by OrcaSlicer: %1%") % language_info->CanonicalName.ToUTF8().data();
		language_info = nullptr;
	}

    if (language_info == nullptr) {
        // PrusaSlicer does not support the Right to Left languages yet.
        if (m_language_info_system != nullptr && m_language_info_system->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_system;
        if (m_language_info_best != nullptr && m_language_info_best->LayoutDirection != wxLayout_RightToLeft)
        	language_info = m_language_info_best;
	    if (language_info == nullptr)
			language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US);
    }

    const wxLanguageInfo *translation_language_info = language_info;
    const wxString requested_language_code = translation_language_info->CanonicalName;
    const wxLanguageInfo *locale_language_info = translation_language_info;
    BOOST_LOG_TRIVIAL(trace) << boost::format("Requested translation language %1%") % requested_language_code.ToUTF8().data();

    // Select language for locales. This language may be different from the language of the dictionary.
    //if (language_info == m_language_info_best || language_info == m_language_info_system) {
    //    // The current language matches user's default profile exactly. That's great.
    //} else if (m_language_info_best != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_best->CanonicalName.BeforeFirst('_')) {
    //    // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
    //    // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
    //    language_info = m_language_info_best;
    //} else if (m_language_info_system != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_system->CanonicalName.BeforeFirst('_'))
    //    language_info = m_language_info_system;

    // Alternate language code.
    wxLanguage language_dict = wxLanguage(translation_language_info->Language);
    if (translation_language_info->CanonicalName.BeforeFirst('_') == "sk") {
    	// Slovaks understand Czech well. Give them the Czech translation.
    	language_dict = wxLANGUAGE_CZECH;
		BOOST_LOG_TRIVIAL(info) << "Using Czech dictionaries for Slovak language";
    }

#ifdef __linux__
    // If we can't find this locale , try to use different one for the language
    // instead of just reporting that it is impossible to switch.
    if (!wxLocale::IsAvailable(locale_language_info->Language) && m_language_info_system) {
        std::string original_lang = into_u8(locale_language_info->CanonicalName);
        locale_language_info = linux_get_existing_locale_language(locale_language_info, m_language_info_system);
        if (locale_language_info != nullptr && locale_language_info != translation_language_info) {
            BOOST_LOG_TRIVIAL(info) << boost::format("Can't use locale %1% directly (missing locales). Using locale %2% instead.")
                                        % original_lang % locale_language_info->CanonicalName.ToUTF8().data();
        }
    }

    if (locale_language_info == nullptr || !wxLocale::IsAvailable(locale_language_info->Language)) {
        auto try_locale = [](const wxLanguageInfo* candidate) -> const wxLanguageInfo* {
            return (candidate && wxLocale::IsAvailable(candidate->Language)) ? candidate : nullptr;
        };
        const wxLanguageInfo* fallback_locale_info =
            try_locale(m_wxLocale ? wxLocale::GetLanguageInfo(wxLanguage(m_wxLocale->GetLanguage())) : nullptr);
        if (!fallback_locale_info) fallback_locale_info = try_locale(m_language_info_system);
        if (!fallback_locale_info) fallback_locale_info = try_locale(m_language_info_best);
        if (!fallback_locale_info) fallback_locale_info = try_locale(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US));
        if (!fallback_locale_info) fallback_locale_info = try_locale(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_UK));
        if (fallback_locale_info != nullptr) {
            BOOST_LOG_TRIVIAL(info) << boost::format("Using fallback locale %1% while keeping translation dictionary %2%.")
                                        % fallback_locale_info->CanonicalName.ToUTF8().data() % requested_language_code.ToUTF8().data();
            locale_language_info = fallback_locale_info;
        }
    }
#endif

    if (initial) {
        // bbs supported languages
        //TODO: use a global one with Preference
        //wxLanguage supported_languages[]{
        //    wxLANGUAGE_ENGLISH,
        //    wxLANGUAGE_CHINESE_SIMPLIFIED,
        //    wxLANGUAGE_GERMAN,
        //    wxLANGUAGE_FRENCH,
        //    wxLANGUAGE_SPANISH,
        //    wxLANGUAGE_SWEDISH,
        //    wxLANGUAGE_DUTCH,
        //    wxLANGUAGE_HUNGARIAN,
        //    wxLANGUAGE_JAPANESE,
        //    wxLANGUAGE_ITALIAN
        //};
        //std::string cur_language = app_config->get("language");
        //if (cur_language != "") {
        //    //cleanup the language wrongly set before
        //    const wxLanguageInfo *langinfo = nullptr;
        //    bool embedded_language = false;
        //    int language_num = sizeof(supported_languages) / sizeof(supported_languages[0]);
        //    for (auto index = 0; index < language_num; index++) {
        //        langinfo = wxLocale::GetLanguageInfo(supported_languages[index]);
        //        std::string temp_lan = langinfo->CanonicalName.ToUTF8().data();
        //        if (cur_language == temp_lan) {
        //            embedded_language = true;
        //            break;
        //        }
        //    }
        //    if (!embedded_language)
        //        app_config->erase("app", "language");
        //}
    }

	BOOST_LOG_TRIVIAL(trace) << boost::format("Switching wxLocales to %1%") % locale_language_info->CanonicalName.ToUTF8().data();

    if (!wxLocale::IsAvailable(locale_language_info->Language)) {
    	// Loading the language dictionary failed.
	    wxString message = "Switching PING Slicer to language " + requested_language_code + " failed.";
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message += "\nYou may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n";
#endif
        if (initial)
        	message + "\n\nApplication will close.";
        wxMessageBox(message, "PING Slicer - Switching language failed", wxOK | wxICON_ERROR);
        if (initial)
			std::exit(EXIT_FAILURE);
		else
			return false;
    }

    // Release the old locales, create new locales.
    //FIXME wxWidgets cause havoc if the current locale is deleted. We just forget it causing memory leaks for now.
    m_wxLocale.release();
    m_wxLocale = Slic3r::make_unique<wxLocale>();
    m_wxLocale->Init(locale_language_info->Language);
    // Override language at the active wxTranslations class (which is stored in the active m_wxLocale)
    // to load possibly different dictionary, for example, load Czech dictionary for Slovak language.
    wxTranslations::Get()->SetLanguage(language_dict);
    m_wxLocale->AddCatalog(SLIC3R_APP_KEY);
    m_active_language_code = requested_language_code;
    m_imgui->set_language(into_u8(requested_language_code));

    //FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
    //wxSetlocale(LC_NUMERIC, "C");
    Preset::update_suffix_modified((_L("*") + " ").ToUTF8().data());
    HintDatabase::get_instance().reinit();
	return true;
}

Tab* GUI_App::get_tab(Preset::Type type)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab->completed() ? tab : nullptr; // To avoid actions with no-completed Tab
    return nullptr;
}

Tab* GUI_App::get_plate_tab()
{
    return plate_tab;
}

Tab* GUI_App::get_model_tab(bool part)
{
    return model_tabs_list[part ? 1 : 0];
}

Tab* GUI_App::get_layer_tab()
{
    return model_tabs_list[2];
}

ConfigOptionMode GUI_App::get_mode()
{
    if (!app_config->has("user_mode"))
        return comSimple;
    //BBS
    const auto mode = app_config->get("user_mode");
    return mode == "advanced" ? comAdvanced :
           mode == "simple" ? comSimple :
           mode == "develop" ? comDevelop : comSimple;
}

std::string GUI_App::get_mode_str()
{
    if (!app_config->has("user_mode"))
        return "simple";
    return app_config->get("user_mode");
}

void GUI_App::save_mode(const /*ConfigOptionMode*/int mode)
{
    //BBS
    const std::string mode_str = mode == comAdvanced ? "advanced" :
                                 mode == comSimple ? "simple" :
                                 mode == comDevelop ? "develop" : "simple";
    app_config->set("user_mode", mode_str);
    update_mode();
}

// Update view mode according to selected menu
void GUI_App::update_mode()
{
    sidebar().update_mode();

    //BBS: GUI refactor
    if (mainframe->m_param_panel)
        mainframe->m_param_panel->update_mode();
    if (mainframe->m_param_dialog)
        mainframe->m_param_dialog->panel()->update_mode();
    if (mainframe->m_printer_view)
        mainframe->m_printer_view->update_mode();
    mainframe->m_webview->update_mode();

#ifdef _MSW_DARK_MODE
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(mainframe->m_tabpanel)->UpdateMode();
#endif

    for (auto tab : tabs_list)
        tab->update_mode();
    for (auto tab : model_tabs_list)
        tab->update_mode();

    //BBS plater()->update_menus();

    plater()->canvas3D()->update_gizmos_on_off_state();
}

void GUI_App::update_internal_development() {
    mainframe->m_webview->update_mode();
    if (mainframe->m_printer_view)
        mainframe->m_printer_view->update_mode();
}

void GUI_App::show_ip_address_enter_dialog(wxString title)
{
    auto evt = new wxCommandEvent(EVT_SHOW_IP_DIALOG);
    evt->SetString(title);
    evt->SetInt(-1);
    wxQueueEvent(this, evt);
}

bool GUI_App::show_modal_ip_address_enter_dialog(bool input_sn, wxString title)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return false;
    if (!dev->get_selected_machine()) return false;
    auto obj = dev->get_selected_machine();

    InputIpAddressDialog dlg(nullptr);
    dlg.m_need_input_sn = input_sn;
    dlg.set_machine_obj(obj);
    if (!title.empty()) dlg.update_title(title);

    dlg.Bind(EVT_ENTER_IP_ADDRESS, [this, obj](wxCommandEvent& e) {
        auto selection_data_arr = wxSplit(e.GetString().ToStdString(), '|');

        if (selection_data_arr.size() == 2) {
            auto ip_address = selection_data_arr[0];
            auto access_code = selection_data_arr[1];

            BOOST_LOG_TRIVIAL(info) << "User enter IP address is " << ip_address;
            if (!ip_address.empty()) {
                wxGetApp().app_config->set_str("ip_address", obj->get_dev_id(), ip_address.ToStdString());
                wxGetApp().app_config->save();

                obj->set_dev_ip(ip_address.ToStdString());
                obj->set_user_access_code(access_code.ToStdString());
            }
        }
    });

    if (dlg.ShowModal() == wxID_YES) {
        return true;
    }
    return false;
}

void  GUI_App::show_ip_address_enter_dialog_handler(wxCommandEvent& evt)
{
    wxString title = evt.GetString();
    int mode = evt.GetInt();
    show_modal_ip_address_enter_dialog(mode == -1?false:true, title);
}

//void GUI_App::add_config_menu(wxMenuBar *menu)
//void GUI_App::add_config_menu(wxMenu *menu)
//{
//    auto local_menu = new wxMenu();
//    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt));
//
//    const auto config_wizard_name = _(ConfigWizard::name(true));
//    const auto config_wizard_tooltip = from_u8((boost::format(_utf8(L("Open %s"))) % config_wizard_name).str());
//    // Cmd+, is standard on OS X - what about other operating systems?
//    if (is_editor()) {
//        local_menu->Append(config_id_base + ConfigMenuWizard, config_wizard_name + dots, config_wizard_tooltip);
//        local_menu->Append(config_id_base + ConfigMenuUpdate, _L("Check for Configuration Updates"), _L("Check for configuration updates"));
//        local_menu->AppendSeparator();
//    }
//    local_menu->Append(config_id_base + ConfigMenuPreferences, _L("Preferences") + dots +
//#ifdef __APPLE__
//        "\tCtrl+,",
//#else
//        "\tCtrl+P",
//#endif
//        _L("Application preferences"));
//    wxMenu* mode_menu = nullptr;
//    if (is_editor()) {
//        local_menu->AppendSeparator();
//        mode_menu = new wxMenu();
//        mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeSimple, _L("Simple"), _L("Simple Mode"));
//        mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeAdvanced, _L("Advanced"), _L("Advanced Mode"));
//        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if (get_mode() == comSimple) evt.Check(true); }, config_id_base + ConfigMenuModeSimple);
//        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if (get_mode() == comAdvanced) evt.Check(true); }, config_id_base + ConfigMenuModeAdvanced);
//
//        local_menu->AppendSubMenu(mode_menu, _L("Mode"), wxString::Format(_L("%s Mode"), SLIC3R_APP_NAME));
//    }
//    local_menu->AppendSeparator();
//    local_menu->Append(config_id_base + ConfigMenuLanguage, _L("Language"));
//    if (is_editor()) {
//        local_menu->AppendSeparator();
//    }
//
//    local_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent &event) {
//        switch (event.GetId() - config_id_base) {
//        case ConfigMenuWizard:
//            run_wizard(ConfigWizard::RR_USER);
//            break;
//		case ConfigMenuUpdate:
//			check_updates(true);
//			break;
//#ifdef __linux__
//        case ConfigMenuDesktopIntegration:
//            show_desktop_integration_dialog();
//            break;
//#endif
//        case ConfigMenuSnapshots:
//            //BBS do not support task snapshot
//            break;
//        case ConfigMenuPreferences:
//        {
//            //BBS GUI refactor: remove unuse layout logic
//            //bool app_layout_changed = false;
//            {
//                // the dialog needs to be destroyed before the call to recreate_GUI()
//                // or sometimes the application crashes into wxDialogBase() destructor
//                // so we put it into an inner scope
//                PreferencesDialog dlg(mainframe);
//                dlg.ShowModal();
//                //BBS GUI refactor: remove unuse layout logic
//                //app_layout_changed = dlg.settings_layout_changed();
//                if (dlg.seq_top_layer_only_changed())
//                    this->plater_->refresh_print();
//
//                if (dlg.recreate_GUI()) {
//                    recreate_GUI(_L("Restart application") + dots);
//                    return;
//                }
//#ifdef _WIN32
//                if (is_editor()) {
//                    if (app_config->get("associate_3mf") == "true")
//                        associate_3mf_files();
//                    if (app_config->get("associate_stl") == "true")
//                        associate_stl_files();
//                }
//                else {
//                    if (app_config->get("associate_gcode") == "true")
//                        associate_gcode_files();
//                }
//#endif // _WIN32
//            }
//            //BBS GUI refactor: remove unuse layout logic
//            /*if (app_layout_changed) {
//                // hide full main_sizer for mainFrame
//                mainframe->GetSizer()->Show(false);
//                mainframe->update_layout();
//                mainframe->select_tab(size_t(0));
//            }*/
//            break;
//        }
//        case ConfigMenuLanguage:
//        {
//            /* Before change application language, let's check unsaved changes on 3D-Scene
//             * and draw user's attention to the application restarting after a language change
//             */
//            {
//                // the dialog needs to be destroyed before the call to switch_language()
//                // or sometimes the application crashes into wxDialogBase() destructor
//                // so we put it into an inner scope
//                wxString title = is_editor() ? wxString(SLIC3R_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
//                title += " - " + _L("Choose language");
//                //wxMessageDialog dialog(nullptr,
//                MessageDialog dialog(nullptr,
//                    _L("Switching the language requires application restart.\n") + "\n\n" +
//                    _L("Do you want to continue?"),
//                    title,
//                    wxICON_QUESTION | wxOK | wxCANCEL);
//                if (dialog.ShowModal() == wxID_CANCEL)
//                    return;
//            }
//
//            switch_language();
//            break;
//        }
//        case ConfigMenuFlashFirmware:
//            //BBS FirmwareDialog::run(mainframe);
//            break;
//        default:
//            break;
//        }
//    });
//
//    using std::placeholders::_1;
//
//    if (mode_menu != nullptr) {
//        auto modfn = [this](int mode, wxCommandEvent&) { if (get_mode() != mode) save_mode(mode); };
//        mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comSimple, _1), config_id_base + ConfigMenuModeSimple);
//        mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comAdvanced, _1), config_id_base + ConfigMenuModeAdvanced);
//    }
//
//    // BBS
//    //menu->Append(local_menu, _L("Configuration"));
//    menu->AppendSubMenu(local_menu, _L("Configuration"));
//}

void GUI_App::open_preferences(size_t open_on_tab, const std::string& highlight_option)
{
    bool need_recreate_gui = false;
    std::string pending_language;
    {
        // the dialog needs to be destroyed before the call to recreate_GUI()
        // or sometimes the application crashes into wxDialogBase() destructor
        // so we put it into an inner scope
        PreferencesDialog dlg(mainframe, open_on_tab, highlight_option);
        dlg.ShowModal();
        need_recreate_gui = dlg.recreate_GUI();
        pending_language = dlg.pending_language();
        if (!need_recreate_gui) {
            this->plater_->get_current_canvas3D()->force_set_focus();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
            if (dlg.seq_top_layer_only_changed())
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                this->plater_->reload_print();
#ifdef _WIN32
            if (is_editor()) {
                if (app_config->get("associate_3mf") == "true")
                    associate_files(L"3mf");
                if (app_config->get("associate_stl") == "true")
                    associate_files(L"stl");
                if (app_config->get("associate_step") == "true") {
                    associate_files(L"step");
                    associate_files(L"stp");
                }
                associate_url(L"orcaslicer");
            }
            else {
                if (app_config->get("associate_gcode") == "true")
                    associate_files(L"gcode");
            }
#endif // _WIN32
        }
    }

    if (!pending_language.empty()) {
        const std::string previous_language = app_config->get("language");
        app_config->set("language", pending_language);
        if (!load_language(wxString::FromUTF8(pending_language), false)) {
            app_config->set("language", previous_language);
            if (this->plater_)
                this->plater_->get_current_canvas3D()->force_set_focus();
            return;
        }
    }

    if (need_recreate_gui)
        recreate_GUI(_L("Changing application language"));
}

bool GUI_App::has_unsaved_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->saved_preset_is_dirty())
            return true;
    }
    return false;
}

bool GUI_App::has_current_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            return true;
    }
    return false;
}

void GUI_App::update_saved_preset_from_current_preset()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->update_saved_preset_from_current_preset();
    }
}

std::vector<std::pair<unsigned int, std::string>> GUI_App::get_selected_presets() const
{
    std::vector<std::pair<unsigned int, std::string>> ret;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology)) {
            const PresetCollection* presets = tab->get_presets();
            ret.push_back({ static_cast<unsigned int>(presets->type()), presets->get_selected_preset_name() });
        }
    }
    return ret;
}

// To notify the user whether he is aware that some preset changes will be lost,
// UnsavedChangesDialog: "Discard / Save / Cancel"
// This is called when:
// - Close Application & Current project isn't saved
// - Load Project      & Current project isn't saved
// - Undo / Redo with change of print technologie
// - Loading snapshot
// - Loading config_file/bundle
// UnsavedChangesDialog: "Don't save / Save / Cancel"
// This is called when:
// - Exporting config_bundle
// - Taking snapshot
bool GUI_App::check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice/* = true*/, bool dont_save_insted_of_discard/* = false*/)
{
    if (has_current_preset_changes()) {
        int act_buttons = ActionButtons::SAVE;
        if (dont_save_insted_of_discard)
            act_buttons |= ActionButtons::DONT_SAVE;
        if (remember_choice)
            act_buttons |= ActionButtons::REMEMBER_CHOISE;
        UnsavedChangesDialog dlg(caption, header, "", act_buttons);
        bool no_need_change = dlg.getUpdateItemCount() == 0 ? true : false;
        if (!no_need_change && dlg.ShowModal() == wxID_CANCEL)
            return false;

        if (dlg.save_preset())  // save selected changes
        {
            //BBS: add project embedded preset relate logic
            for (const UnsavedChangesDialog::PresetData& nt : dlg.get_names_and_types())
                preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);
            //for (const std::pair<std::string, Preset::Type>& nt : dlg.get_names_and_types())
            //    preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

            load_current_presets(false);

            // if we saved changes to the new presets, we should to
            // synchronize config.ini with the current selections.
            preset_bundle->export_selections(*app_config);

            //MessageDialog(nullptr, _L_PLURAL("Modifications to the preset have been saved",
            //                                 "Modifications to the presets have been saved", dlg.get_names_and_types().size())).ShowModal();
        }
    }

    return true;
}

void GUI_App::apply_keeped_preset_modifications()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->apply_config_from_cache();
    }
    load_current_presets(false);
}

// This is called when creating new project or load another project
// OR close ConfigWizard
// to ask the user what should we do with unsaved changes for presets.
// New Project          => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Cancel"
//                      => Current project isn't saved => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Close ConfigWizard   => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Note: no_nullptr postponed_apply_of_keeped_changes indicates that thie function is called after ConfigWizard is closed
bool GUI_App::check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes/* = nullptr*/)
{
    if (has_current_preset_changes()) {
        bool is_called_from_configwizard = postponed_apply_of_keeped_changes != nullptr;

        UnsavedChangesDialog dlg(caption, header, "", action_buttons);
        bool no_need_change = dlg.getUpdateItemCount() == 0 ? true : false;
        if (!no_need_change && dlg.ShowModal() == wxID_CANCEL)
            return false;

        auto reset_modifications = [this, is_called_from_configwizard]() {
            //if (is_called_from_configwizard)
            //    return; // no need to discared changes. It will be done fromConfigWizard closing

            PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
            for (const Tab* const tab : tabs_list) {
                if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        };

        if (dlg.discard() || no_need_change)
            reset_modifications();
        else  // save selected changes
        {
            //BBS: add project embedded preset relate logic
            const auto& preset_names_and_types = dlg.get_names_and_types();
            if (dlg.save_preset()) {
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types)
                    preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);

                // if we saved changes to the new presets, we should to
                // synchronize config.ini with the current selections.
                preset_bundle->export_selections(*app_config);

                //wxString text = _L_PLURAL("Modifications to the preset have been saved",
                //    "Modifications to the presets have been saved", preset_names_and_types.size());
                //if (!is_called_from_configwizard)
                //    text += "\n\n" + _L("All modifications will be discarded for new project.");

                //MessageDialog(nullptr, text).ShowModal();
                reset_modifications();
            }
            else if (dlg.transfer_changes() && (dlg.has_unselected_options() || is_called_from_configwizard)) {
                // execute this part of code only if not all modifications are keeping to the new project
                // OR this function is called when ConfigWizard is closed and "Keep modifications" is selected
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types) {
                    Preset::Type type = nt.type;
                    Tab* tab = get_tab(type);
                    std::vector<std::string> selected_options = dlg.get_selected_options(type);
                    if (type == Preset::TYPE_PRINTER) {
                        auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                        if (it != selected_options.end()) {
                            // erase "extruders_count" option from the list
                            selected_options.erase(it);
                            // cache the extruders count
                            static_cast<TabPrinter*>(tab)->cache_extruder_cnt();
                        }
                    }
                    std::vector<std::string> selected_options2;
                    std::transform(selected_options.begin(), selected_options.end(), std::back_inserter(selected_options2), [](auto & o) {
                        auto i = o.find('#');
                        return i != std::string::npos ? o.substr(0, i) : o;
                    });
                    tab->cache_config_diff(selected_options2);
                    if (!is_called_from_configwizard)
                        tab->m_presets->discard_current_changes();
                }
                if (is_called_from_configwizard)
                    *postponed_apply_of_keeped_changes = true;
                else
                    apply_keeped_preset_modifications();
            }
        }
    }

    return true;
}

bool GUI_App::can_load_project()
{
    return true;
}

bool GUI_App::check_print_host_queue()
{
    wxString dirty;
    std::vector<std::pair<std::string, std::string>> jobs;
    // Get ongoing jobs from dialog
    mainframe->m_printhost_queue_dlg->get_active_jobs(jobs);
    if (jobs.empty())
        return true;
    // Show dialog
    wxString job_string = wxString();
    for (const auto& job : jobs) {
        job_string += format_wxstr("   %1% : %2% \n", job.first, job.second);
    }
    wxString message;
    message += _(L("The uploads are still ongoing")) + ":\n\n" + job_string +"\n" + _(L("Stop them and continue anyway?"));
    //wxMessageDialog dialog(mainframe,
    MessageDialog dialog(mainframe,
        message,
        wxString(SLIC3R_APP_NAME) + " - " + _(L("Ongoing uploads")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    if (dialog.ShowModal() == wxID_YES)
        return true;

    // TODO: If already shown, bring forward
    mainframe->m_printhost_queue_dlg->Show();
    return false;
}

bool GUI_App::checked_tab(Tab* tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end() &&
        find(model_tabs_list.begin(), model_tabs_list.end(), tab) == model_tabs_list.end())
        ret = false;
    return ret;
}

// Update UI / Tabs to reflect changes in the currently loaded presets
//BBS: add preset combo box re-activate logic
void GUI_App::load_current_presets(bool active_preset_combox/*= false*/, bool check_printer_presets_ /*= true*/)
{
    // check printer_presets for the containing information about "Print Host upload"
    // and create physical printer from it, if any exists
    if (check_printer_presets_)
        check_printer_presets();

    auto& edited_printer_preset = preset_bundle->printers.get_edited_preset();
    PrinterTechnology printer_technology = edited_printer_preset.printer_technology();
    // PING(2026-07-23)：記下同步前槽數——下方兩個分支只改 bundle，UI 槽位要另外重建（見後）。
    const size_t old_filament_count = preset_bundle->filament_presets.size();
    // ORCA: Sync filament count with the printer's nozzle count before loading presets for multi-tool printers.
    // This ensures filament_presets vector is properly sized when combo boxes are created/updated.
    if (printer_technology == ptFFF && !edited_printer_preset.config.opt_bool("single_extruder_multi_material")) {
        auto* nozzle_diameter = edited_printer_preset.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nozzle_diameter) {
            preset_bundle->set_num_filaments(nozzle_diameter->values.size());
        }
    }
    // PING: SEMM 多材料機（FD=2料、FF=4料）依 default_filament_profile 數量初始化線材槽，
    // 還原 PING Slicer V3.0 行為。Orca 2.3.2 預設對 single_extruder_multi_material 機種不同步槽數（視為 AMS 變動數量），
    // 導致 nozzle_diameter=1 + SEMM=1 的 PING 雙料機選機後只剩 1 槽。
    // 2026-06-10 改「完全同步」（原為只增不減）：啟動載入即吻合該機 1/2/4 規格，不沿用舊快照數量。
    else if (printer_technology == ptFFF) {
        auto* def_fil = edited_printer_preset.config.option<ConfigOptionStrings>("default_filament_profile");
        if (def_fil && def_fil->values.size() >= 1 && def_fil->values.size() != preset_bundle->filament_presets.size()) {
            preset_bundle->set_num_filaments((unsigned int) def_fil->values.size());
        }
    }
    // PING(2026-08-14 批3 R5-2)：擴槽的新槽填該機 default_filament_profile（治本 back-fill）。
    // 與 Tab::select_preset 那處**共用同一支函式、同一條規則**（規格點名的兩個槽數同步點之一）；
    // 這裡涵蓋精靈選機頁收尾與啟動載入路徑。只填新槽、舊槽一律不動。
    // ⚠ 這裡**刻意不做製程收斂**——R5-4：開 app 載入不主動改使用者/專案已留下的選擇，
    //   舊的不一致快照等他第一次材料或切機手勢時由 R3／R5-1 自然收斂。
    if (preset_bundle->filament_presets.size() > old_filament_count)
        ping_backfill_new_slots(old_filament_count);
    // PING(2026-07-23)：槽數有變就重建側欄槽位 UI。選機頁（精靈）路徑收尾走的是本函式，
    // 先前只同步 bundle 沒重建 UI → 切到 FP300 畫面仍留上一台的 2 槽（殘影槽操作全被
    // 越界守衛擋掉、看似全無反應）；左上機器下拉路徑（Tab::select_preset）本就有重建，
    // 兩路徑自此對稱。啟動路徑同數量時不觸發（守衛擋掉）、數量有變時提早重建亦冪等。
    if (Plater *pl = this->plater(); pl && preset_bundle->filament_presets.size() != old_filament_count)
        pl->on_filament_count_change(preset_bundle->filament_presets.size());
	this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Preset::TYPE_PRINTER) {
				static_cast<TabPrinter*>(tab)->update_pages();
				// Mark the plater to update print bed by tab->load_current_preset() from Plater::on_config_change().
				this->plater()->force_print_bed_update();
			}
			tab->load_current_preset();
			//BBS: add preset combox re-active logic
			if (active_preset_combox)
				tab->reactive_preset_combo_box();
		}
    // BBS: model config
    for (Tab *tab : model_tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
            tab->rebuild_page_tree();
        }
}

static std::mutex mutex_delete_cache_presets;

std::vector<std::string> & GUI_App::get_delete_cache_presets()
{
    return need_delete_presets;
}

std::vector<std::string> GUI_App::get_delete_cache_presets_lock()
{
    std::scoped_lock l(mutex_delete_cache_presets);
    return need_delete_presets;
}

void GUI_App::delete_preset_from_cloud(std::string setting_id)
{
    std::scoped_lock l(mutex_delete_cache_presets);
    need_delete_presets.push_back(setting_id);
}

void GUI_App::preset_deleted_from_cloud(std::string setting_id)
{
    std::scoped_lock l(mutex_delete_cache_presets);
    need_delete_presets.erase(std::remove(need_delete_presets.begin(), need_delete_presets.end(), setting_id), need_delete_presets.end());
}

wxString GUI_App::filter_string(wxString str)
{
    std::string result = str.utf8_string();
    std::string input = str.utf8_string();


    std::regex domainRegex(R"(([a-zA-Z0-9.-]+\.[a-zA-Z]{2,}(?:\.[a-zA-Z]{2,})?))");
    std::sregex_iterator it(input.begin(), input.end(), domainRegex);
    std::sregex_iterator end;

    while (it != end) {
        std::smatch match = *it;
        std::string domain = match.str();
        result.replace(match.position(), domain.length(), "[***]");
        ++it;
    }

    return wxString::FromUTF8(result);
}

bool GUI_App::OnExceptionInMainLoop()
{
    generic_exception_handle();
    return false;
}

#ifdef __APPLE__
// This callback is called from wxEntry()->wxApp::CallOnInit()->NSApplication run
// that is, before GUI_App::OnInit(), so we have a chance to switch GUI_App
// to a G-code viewer.
void GUI_App::OSXStoreOpenFiles(const wxArrayString &fileNames)
{
    //BBS: remove GCodeViewer as seperate APP logic
    /*size_t num_gcodes = 0;
    for (const wxString &filename : fileNames)
        if (is_gcode_file(into_u8(filename)))
            ++ num_gcodes;
    if (fileNames.size() == num_gcodes) {
        // Opening PrusaSlicer by drag & dropping a G-Code onto PING Slicer icon in Finder,
        // just G-codes were passed. Switch to G-code viewer mode.
        m_app_mode = EAppMode::GCodeViewer;
        unlock_lockfile(get_instance_hash_string() + ".lock", data_dir() + "/cache/");
        if(app_config != nullptr)
            delete app_config;
        app_config = nullptr;
        init_app_config();
    }*/
    wxApp::OSXStoreOpenFiles(fileNames);
}

void GUI_App::MacOpenURL(const wxString& url)
{
    if (url.empty())
        return;
    start_download(into_u8(url));
}

// wxWidgets override to get an event on open files.
void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    bool single_instance = app_config->get("app", "single_instance") == "true";
    if (m_post_initialized && !single_instance) {
        bool has3mf = false;
        std::vector<wxString> names;
        for (auto & n : fileNames) {
            has3mf |= n.EndsWith(".3mf");
            names.push_back(n);
        }
        if (has3mf) {
            start_new_slicer(names);
            return;
        }
    }
    std::vector<std::string> files;
    std::vector<wxString>    gcode_files;
    std::vector<wxString>    non_gcode_files;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", open files, size " << fileNames.size();
    for (const auto& filename : fileNames) {
        if (is_gcode_file(into_u8(filename)))
            gcode_files.emplace_back(filename);
        else {
            files.emplace_back(into_u8(filename));
            non_gcode_files.emplace_back(filename);
        }
    }
    //BBS: remove GCodeViewer as seperate APP logic
    /*if (m_app_mode == EAppMode::GCodeViewer) {
        // Running in G-code viewer.
        // Load the first G-code into the G-code viewer.
        // Or if no G-codes, send other files to slicer.
        if (! gcode_files.empty())
            this->plater()->load_gcode(gcode_files.front());
        if (!non_gcode_files.empty())
            start_new_slicer(non_gcode_files, true);
    } else*/
    {
        if (! files.empty()) {
            if (m_post_initialized) {
                wxArrayString input_files;
                for (size_t i = 0; i < non_gcode_files.size(); ++i) {
                    input_files.push_back(non_gcode_files[i]);
                }
                this->plater()->load_files(input_files);
            } else {
                for (size_t i = 0; i < files.size(); ++i) {
                    this->init_params->input_files.emplace_back(files[i]);
                }
            }
        } else {
            if (m_post_initialized) {
                this->plater()->load_gcode(gcode_files.front());
            } else {
                this->init_params->input_gcode = true;
                this->init_params->input_files = { into_u8(gcode_files.front()) };
            }
        }
        /*for (const wxString &filename : gcode_files)
            start_new_gcodeviewer(&filename);*/
    }
}

#endif /* __APPLE */

Sidebar& GUI_App::sidebar()
{
    return plater_->sidebar();
}

GizmoObjectManipulation *GUI_App::obj_manipul()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return (plater_ != nullptr) ? &plater_->get_view3D_canvas3D()->get_gizmos_manager().get_object_manipulation() : nullptr;
}

ObjectSettings* GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList* GUI_App::obj_list()
{
    return sidebar().obj_list();
}

ObjectLayers* GUI_App::obj_layers()
{
    return sidebar().obj_layers();
}

Plater* GUI_App::plater()
{
    return plater_;
}

const Plater* GUI_App::plater() const
{
    return plater_;
}

ParamsPanel* GUI_App::params_panel()
{
    if (mainframe)
        return mainframe->m_param_panel;
    return nullptr;
}

ParamsDialog* GUI_App::params_dialog()
{
    if (mainframe)
        return mainframe->m_param_dialog;
    return nullptr;
}

Model& GUI_App::model()
{
    return plater_->model();
}

Downloader* GUI_App::downloader()
{
    return m_downloader.get();
}

void GUI_App::load_url(wxString url)
{
    if (mainframe)
        return mainframe->load_url(url);
}

void GUI_App::open_mall_page_dialog()
{
    std::string host_url;
    std::string model_url;
    std::string link_url;

    int result = -1;

    //model api url
    host_url = get_model_http_url(app_config->get_country_code());

    //model url

    wxString language_code = this->current_language_code().BeforeFirst('_');
    model_url = language_code.ToStdString();

    if (getAgent() && mainframe) {

        //login already
        if (getAgent()->is_user_login()) {
            std::string ticket;
            result = getAgent()->request_bind_ticket(&ticket);

            if(result == 0){
                link_url = host_url + "api/sign-in/ticket?to=" + host_url + url_encode(model_url) + "&ticket=" + ticket;
            }
        }
    }

    if (result < 0) {
       link_url = host_url + model_url;
    }

    if (link_url.find("?") != std::string::npos) {
        link_url += "&from=orcaslicer";
    } else {
        link_url += "?from=orcaslicer";
    }

    wxLaunchDefaultBrowser(link_url);
}

void GUI_App::open_publish_page_dialog()
{
    std::string host_url;
    std::string model_url;
    std::string link_url;

    int result = -1;

    //model api url
    host_url = get_model_http_url(app_config->get_country_code());

    //publish url
    wxString language_code = this->current_language_code().BeforeFirst('_');
    model_url += (language_code.ToStdString() + "/my/models/publish");

    if (getAgent() && mainframe) {

        //login already
        if (getAgent()->is_user_login()) {
            std::string ticket;
            result = getAgent()->request_bind_ticket(&ticket);

            if (result == 0) {
                link_url = host_url + "api/sign-in/ticket?to=" + host_url + url_encode(model_url) + "&ticket=" + ticket;
            }
        }
    }

    if (result < 0) {
        link_url = host_url + model_url;
    }

    wxLaunchDefaultBrowser(link_url);
}

char GUI_App::from_hex(char ch) {
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

std::string GUI_App::url_decode(std::string value) {
    return Http::url_decode(value);
}

std::string GUI_App::url_encode(std::string value) {
    return Http::url_encode(value);
}

void GUI_App::popup_ping_bind_dialog()
{
    if (m_ping_code_binding_dialog == nullptr) {
        m_ping_code_binding_dialog = new PingCodeBindDialog();
        m_ping_code_binding_dialog->ShowModal();
        remove_ping_bind_dialog();
    }
}

void GUI_App::remove_ping_bind_dialog()
{
    if (m_ping_code_binding_dialog != nullptr) {
        m_ping_code_binding_dialog->Destroy();
        delete m_mall_publish_dialog;
        m_ping_code_binding_dialog = nullptr;
    }
}


void GUI_App::remove_mall_system_dialog()
{
    if (m_mall_publish_dialog != nullptr) {
        m_mall_publish_dialog->Destroy();
        delete m_mall_publish_dialog;
    }
}

void GUI_App::run_script(wxString js)
{
    if (mainframe)
        return mainframe->RunScript(js);
}

Notebook* GUI_App::tab_panel() const
{
    if (mainframe)
        return mainframe->m_tabpanel;
    return nullptr;
}

NotificationManager * GUI_App::notification_manager()
{
    if (plater_)
        return plater_->get_notification_manager();
    return nullptr;
}

// extruders count from selected printer preset
int GUI_App::extruders_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_selected_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// extruders count from edited printer preset
int GUI_App::extruders_edited_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_edited_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// BBS
int GUI_App::filaments_cnt() const
{
    return preset_bundle->filament_presets.size();
}

PrintSequence GUI_App::global_print_sequence() const
{
    PrintSequence global_print_seq = PrintSequence::ByDefault;
    auto curr_preset_config = preset_bundle->prints.get_edited_preset().config;
    if (curr_preset_config.has("print_sequence"))
        global_print_seq = curr_preset_config.option<ConfigOptionEnum<PrintSequence>>("print_sequence")->value;
    return global_print_seq;
}

wxString GUI_App::current_language_code_safe() const
{
	// Translate the language code to a code, for which Prusa Research maintains translations.
	const std::map<wxString, wxString> mapping {
		{ "cs", 	"cs_CZ", },
		{ "sk", 	"cs_CZ", },
		{ "de", 	"de_DE", },
		{ "nl", 	"nl_NL", },
		{ "sv", 	"sv_SE", },
		{ "es", 	"es_ES", },
		{ "fr", 	"fr_FR", },
		{ "it", 	"it_IT", },
		{ "ja", 	"ja_JP", },
		{ "ko", 	"ko_KR", },
		{ "pl", 	"pl_PL", },
		{ "uk", 	"uk_UA", },
		{ "zh", 	"zh_CN", },
		{ "ru", 	"ru_RU", },
        { "tr", 	"tr_TR", },
        { "pt", 	"pt_BR", },
        { "lt", 	"lt_LT", },
        { "vi", 	"vi_VN", },
	};
	wxString language_code = this->current_language_code().BeforeFirst('_');
	auto it = mapping.find(language_code);
	if (it != mapping.end())
		language_code = it->second;
	else
		language_code = "en_US";
	return language_code;
}

void GUI_App::open_web_page_localized(const std::string &http_address)
{
    open_browser_with_warning_dialog(http_address + "&lng=" + this->current_language_code_safe());
}

// If we are switching from the FFF-preset to the SLA, we should to control the printed objects if they have a part(s).
// Because of we can't to print the multi-part objects with SLA technology.
bool GUI_App::may_switch_to_SLA_preset(const wxString& caption)
{
    if (model_has_multi_part_objects(model())) {
        // BBS: remove SLA related message
        return false;
    }
    return true;
}

bool GUI_App::run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page)
{
    wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");

#ifdef __APPLE__
     if (is_adding_script_handler()) {
        BOOST_LOG_TRIVIAL(info) << "run_wizard: Script handler is being added, delaying wizard creation";
        auto timer = new wxTimer();
        timer->Bind(wxEVT_TIMER, [this, reason, start_page, timer](wxTimerEvent &) {
            timer->Stop();
            run_wizard(reason, start_page);
            delete timer;
        });
        timer->StartOnce(200);

        return true;
    }
#endif

    //if (reason == ConfigWizard::RR_USER) {
    //    //TODO: turn off it currently, maybe need to turn on in the future
    //    if (preset_updater->config_update(app_config->orig_version(), PresetUpdater::UpdateParams::FORCED_BEFORE_WIZARD) == PresetUpdater::R_ALL_CANCELED)
    //        return false;
    //}

    //auto wizard_t = new ConfigWizard(mainframe);
    //const bool res = wizard_t->run(reason, start_page);

    std::string strFinish = wxGetApp().app_config->get("firstguide", "finish");
    long        pStyle    = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU;
    if (strFinish == "false" || strFinish.empty())
        pStyle = wxCAPTION | wxTAB_TRAVERSAL;

    GuideFrame wizard(this, pStyle);
    auto page = start_page == ConfigWizard::SP_WELCOME ? GuideFrame::BBL_WELCOME :
                start_page == ConfigWizard::SP_FILAMENTS ? GuideFrame::BBL_FILAMENT_ONLY :
                start_page == ConfigWizard::SP_PRINTERS ? GuideFrame::BBL_MODELS_ONLY :
                GuideFrame::BBL_MODELS;
    wizard.SetStartPage(page);
    bool       res = wizard.run();

    if (res) {
        load_current_presets();
        update_publish_status();
        mainframe->refresh_plugin_tips();
        // BBS: remove SLA related message
    }

    return res;
}

void GUI_App::show_desktop_integration_dialog()
{
#ifdef __linux__
    //wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");
    DesktopIntegrationDialog dialog(mainframe);
    dialog.ShowModal();
#endif //__linux__
}

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
void GUI_App::gcode_thumbnails_debug()
{
    const std::string BEGIN_MASK = "; thumbnail begin";
    const std::string END_MASK = "; thumbnail end";
    std::string gcode_line;
    bool reading_image = false;
    unsigned int width = 0;
    unsigned int height = 0;

    wxFileDialog dialog(GetTopWindow(), _L("Select a G-code file:"), "", "", "G-code files (*.gcode)|*.gcode;*.GCODE;", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::string in_filename = into_u8(dialog.GetPath());
    std::string out_path = boost::filesystem::path(in_filename).remove_filename().append(L"thumbnail").string();

    boost::nowide::ifstream in_file(in_filename.c_str());
    std::vector<std::string> rows;
    std::string row;
    if (in_file.good())
    {
        while (std::getline(in_file, gcode_line))
        {
            if (in_file.good())
            {
                if (boost::starts_with(gcode_line, BEGIN_MASK))
                {
                    reading_image = true;
                    gcode_line = gcode_line.substr(BEGIN_MASK.length() + 1);
                    std::string::size_type x_pos = gcode_line.find('x');
                    std::string width_str = gcode_line.substr(0, x_pos);
                    width = (unsigned int)::atoi(width_str.c_str());
                    std::string height_str = gcode_line.substr(x_pos + 1);
                    height = (unsigned int)::atoi(height_str.c_str());
                    row.clear();
                }
                else if (reading_image && boost::starts_with(gcode_line, END_MASK))
                {
                    std::string out_filename = out_path + std::to_string(width) + "x" + std::to_string(height) + ".png";
                    boost::nowide::ofstream out_file(out_filename.c_str(), std::ios::binary);
                    if (out_file.good())
                    {
                        std::string decoded;
                        decoded.resize(boost::beast::detail::base64::decoded_size(row.size()));
                        decoded.resize(boost::beast::detail::base64::decode((void*)&decoded[0], row.data(), row.size()).first);

                        out_file.write(decoded.c_str(), decoded.size());
                        out_file.close();
                    }

                    reading_image = false;
                    width = 0;
                    height = 0;
                    rows.clear();
                } else if (reading_image)
                    row += gcode_line.substr(2);
            }
        }

        in_file.close();
    }
}
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    app_config->save();
}

bool GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized)
{
    if (name.empty()) { return false; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) {
        //window->Maximize(default_maximized);
        return false;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) {
        window->Maximize(default_maximized);
        return true;
    }

    const wxRect& rect = metrics->get_rect();
    window->SetPosition(rect.GetPosition());
    window->SetSize(rect.GetSize());
    window->Maximize(metrics->get_maximized());
    return true;
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow* window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

void GUI_App::window_pos_center(wxTopLevelWindow *window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.center_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

bool GUI_App::config_wizard_startup()
{
    if (!m_app_conf_exists || preset_bundle->printers.only_default_printers()) {
        BOOST_LOG_TRIVIAL(info) << "run wizard...";
        run_wizard(ConfigWizard::RR_DATA_EMPTY);
        BOOST_LOG_TRIVIAL(info) << "finished run wizard";
        return true;
    } /*else if (get_app_config()->legacy_datadir()) {
        // Looks like user has legacy pre-vendorbundle data directory,
        // explain what this is and run the wizard

        MsgDataLegacy dlg;
        dlg.ShowModal();

        run_wizard(ConfigWizard::RR_DATA_LEGACY);
        return true;
    }*/
    return false;
}

void GUI_App::check_updates(const bool verbose)
{
	PresetUpdater::UpdateResult updater_result;
	try {
		updater_result = preset_updater->config_update(app_config->orig_version(), verbose ? PresetUpdater::UpdateParams::SHOW_TEXT_BOX : PresetUpdater::UpdateParams::SHOW_NOTIFICATION);
		if (updater_result == PresetUpdater::R_INCOMPAT_EXIT) {
			mainframe->Close();
		}
		else if (updater_result == PresetUpdater::R_INCOMPAT_CONFIGURED) {
            m_app_conf_exists = true;
		}
		else if (verbose && updater_result == PresetUpdater::R_NOOP) {
			MsgNoUpdates dlg;
			dlg.ShowModal();
		}
	}
	catch (const std::exception & ex) {
		show_error(nullptr, ex.what());
	}
}


FilamentColorCodeQuery* GUI_App::get_filament_color_code_query()
{
    if (!m_filament_color_code_query)
    {
        m_filament_color_code_query = new FilamentColorCodeQuery();
    }

    return m_filament_color_code_query;
}

bool GUI_App::open_browser_with_warning_dialog(const wxString& url, int flags/* = 0*/)
{
    return wxLaunchDefaultBrowser(url, flags);
}

// static method accepting a wxWindow object as first parameter
// void warning_catcher{
//     my($self, $message_dialog) = @_;
//     return sub{
//         my $message = shift;
//         return if $message = ~/ GLUquadricObjPtr | Attempt to free unreferenced scalar / ;
//         my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
//         $message_dialog
//             ? $message_dialog->(@params)
//             : Wx::MessageDialog->new($self, @params)->ShowModal;
//     };
// }

// Do we need this function???
// void GUI_App::notify(message) {
//     auto frame = GetTopWindow();
//     // try harder to attract user attention on OS X
//     if (!frame->IsActive())
//         frame->RequestUserAttention(defined(__WXOSX__/*&Wx::wxMAC */)? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO);
//
//     // There used to be notifier using a Growl application for OSX, but Growl is dead.
//     // The notifier also supported the Linux X D - bus notifications, but that support was broken.
//     //TODO use wxNotificationMessage ?
// }


#ifdef __WXMSW__
static bool set_into_win_registry(HKEY hkeyHive, const wchar_t* pszVar, const wchar_t* pszValue)
{
    // see as reference: https://stackoverflow.com/questions/20245262/c-program-needs-an-file-association
    wchar_t szValueCurrent[1000];
    DWORD dwType;
    DWORD dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        // an error occurred
        return false;

    if (!bDidntExist) {
        if (dwType != REG_SZ)
            // invalid type
            return false;

        if (::wcscmp(szValueCurrent, pszValue) == 0)
            // value already set
            return false;
    }

    DWORD dwDisposition;
    HKEY hkey;
    iRC = ::RegCreateKeyExW(hkeyHive, pszVar, 0, 0, 0, KEY_ALL_ACCESS, nullptr, &hkey, &dwDisposition);
    bool ret = false;
    if (iRC == ERROR_SUCCESS) {
        iRC = ::RegSetValueExW(hkey, L"", 0, REG_SZ, (BYTE*)pszValue, (::wcslen(pszValue) + 1) * sizeof(wchar_t));
        if (iRC == ERROR_SUCCESS)
            ret = true;
    }

    RegCloseKey(hkey);
    return ret;
}

static bool del_win_registry(HKEY hkeyHive, const wchar_t *pszVar, const wchar_t *pszValue)
{
    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        return false;

    if (!bDidntExist) {
        iRC      = ::RegDeleteKeyExW(hkeyHive, pszVar, KEY_ALL_ACCESS, 0);
        if (iRC == ERROR_SUCCESS) {
            return true;
        }
    }

    return false;
}

#endif // __WXMSW__

void GUI_App::associate_files(std::wstring extend)
{
#ifdef WIN32
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Orca.Slicer.1";
    std::wstring prog_desc = L"PING Slicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    if (is_new)
        // notify Windows only when any of the values gets changed
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif // WIN32
}

void GUI_App::disassociate_files(std::wstring extend)
{
#ifdef WIN32
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Orca.Slicer.1";
    std::wstring prog_desc = L"PING Slicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= del_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());

    bool is_associate_3mf  = app_config->get("associate_3mf") == "true";
    bool is_associate_stl  = app_config->get("associate_stl") == "true";
    bool is_associate_step = app_config->get("associate_step") == "true";
    if (!is_associate_3mf && !is_associate_stl && !is_associate_step)
    {
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    }

    if (is_new)
       ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif // WIN32
}

bool GUI_App::check_url_association(std::wstring url_prefix, std::wstring& reg_bin)
{
    reg_bin = L"";
#ifdef WIN32
    wxRegKey key_full(wxRegKey::HKCU, "Software\\Classes\\" + url_prefix + "\\shell\\open\\command");
    if (!key_full.Exists()) {
        return false;
    }
    reg_bin = key_full.QueryDefaultValue().ToStdWstring();

    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    std::wstring key_string = L"\"" + binary_path.wstring() + L"\" \"%1\"";
    return key_string == reg_bin;
#else
    return false;
#endif // WIN32
}

void GUI_App::associate_url(std::wstring url_prefix)
{
#ifdef WIN32
    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    // the path to binary needs to be correctly saved in string with respect to localized characters
    wxString wbinary = wxString::FromUTF8(binary_path.string());
    std::string binary_string = (boost::format("%1%") % wbinary).str();
    BOOST_LOG_TRIVIAL(info) << "Downloader registration: Path of binary: " << binary_string;

    std::string key_string = "\"" + binary_string + "\" \"%1\"";

    wxRegKey key_first(wxRegKey::HKCU, "Software\\Classes\\" + url_prefix);
    wxRegKey key_full(wxRegKey::HKCU, "Software\\Classes\\" + url_prefix + "\\shell\\open\\command");
    if (!key_first.Exists()) {
        key_first.Create(false);
    }
    key_first.SetValue("URL Protocol", "");

    if (!key_full.Exists()) {
        key_full.Create(false);
    }
    key_full = key_string;
#elif defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
    DesktopIntegrationDialog::perform_downloader_desktop_integration(into_u8(url_prefix));
#endif // WIN32
}

void GUI_App::disassociate_url(std::wstring url_prefix)
{
#ifdef WIN32
    wxRegKey key_full(wxRegKey::HKCU, "Software\\Classes\\" + url_prefix + "\\shell\\open\\command");
    if (!key_full.Exists()) {
        return;
    }
    key_full = "";
#endif // WIN32
}


void GUI_App::start_download(std::string url)
{
    if (!plater_) {
        BOOST_LOG_TRIVIAL(error) << "Could not start URL download: plater is nullptr.";
        return;
    }
    //lets always init so if the download dest folder was changed, new dest is used
    boost::filesystem::path dest_folder(app_config->get("download_path"));
    if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder)) {
        std::string msg = _u8L("Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return;
    }
    m_downloader->init(dest_folder);
    m_downloader->start_download(url);

}

bool is_soluble_filament(int extruder_id)
{
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;

    if (extruder_id >= filament_presets.size()) return false;

    Slic3r::Preset *filament = filaments.find_preset(filament_presets[extruder_id]);
    if (filament == nullptr) return false;

    Slic3r::ConfigOptionBools *support_option = dynamic_cast<Slic3r::ConfigOptionBools *>(filament->config.option("filament_soluble"));
    if (support_option == nullptr) return false;

    return support_option->get_at(0);
};

bool has_filaments(const std::vector<string>& model_filaments) {
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    if (!Slic3r::GUI::wxGetApp().plater()) return false;
    auto model_objects = Slic3r::GUI::wxGetApp().plater()->model().objects;
    const Slic3r::DynamicPrintConfig &config = wxGetApp().preset_bundle->full_config();
    Model::setExtruderParams(config, filament_presets.size());

    auto get_filament_name = [](int id) { return Model::extruderParamsMap.find(id) != Model::extruderParamsMap.end() ? Model::extruderParamsMap.at(id).materialName : "PLA"; };
    for (const ModelObject *mo : model_objects) {
        for (auto vol : mo->volumes) {
            auto ve = vol->get_extruders();
            for (auto id : ve) {
                auto name = get_filament_name(id);
                if (find(model_filaments.begin(), model_filaments.end(), name) != model_filaments.end()) return true;
            }
        }
    }
    return false;
}

bool is_support_filament(int extruder_id, bool strict_check)
{
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;

    if (extruder_id >= filament_presets.size()) return false;

    Slic3r::Preset *filament = filaments.find_preset(filament_presets[extruder_id]);
    if (filament == nullptr) return false;

    std::string filament_type = filament->config.option<ConfigOptionStrings>("filament_type")->values[0];

    Slic3r::ConfigOptionBools *support_option = dynamic_cast<Slic3r::ConfigOptionBools *>(filament->config.option("filament_is_support"));

    if(!strict_check &&(filament_type == "PETG" || filament_type == "PLA")) {
        std::vector<string> model_filaments;
        if (filament_type == "PETG")
            model_filaments.emplace_back("PLA");
        else {
            model_filaments = {"PETG", "TPU", "TPU-AMS"};
        }
        if (has_filaments(model_filaments)) return true;
    }
    if (support_option == nullptr) return false;
    return support_option->get_at(0);
};

// =====================================================================
// C-2 第 1 項：工作室 → 隱形宿主（PhotoTileEngineHost）的接線（2026-08-04）
// 頁面（resources/web/phototile/index.html）只送參數；影像來源、生成、進度、
// 交付全在 C++。這四支＝這條鏈的全部接點；宣告在 GUI_App.hpp。
// 執行緒：宿主 handler 一律在 UI 執行緒回呼（COM 回呼／CallAfter），這裡不需再 marshal。
// =====================================================================

void GUI_App::photo_tile_ensure_host()
{
    if (m_photo_tile_host)
        return;

    /* runtime 檢測：缺 WebView2 也照樣建宿主——generate 排隊後 start_engine 同步失敗
       會走 fail_active_and_queued（覆審 I-1 才補上；先前這句註解描述的失敗路其實只存在
       非 Windows stub）＝誠實回 engine_unavailable → result handler → 頁面顯示原因。
       這裡的狀態推播是輔助：頁面 engineStatus 在生成中也會顯示原因（I-1 頁面半邊）。 */
    const PhotoTileEngineHost::Availability avail = PhotoTileEngineHost::check_runtime();
    if (!avail.available)
        photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.engineStatus && "
            "window.PINGPhotoTile.engineStatus({status:\"unavailable\",detail:\"") +
            ping_js_escape(avail.reason) + "\"});");

    m_photo_tile_host.reset(new PhotoTileEngineHost());

    /* 【C-2 第 2 項・一輪 #9】env 蓋章來源：納入 host API ⇒ 每個 generate 自動帶
       「使用者按下產生那一刻」的環境快照，呼叫端忘不掉；上盤入口用同一支函式
       取「此刻」再比對＝同一把尺。 */
    m_photo_tile_host->set_current_env_provider([this]() { return photo_tile_current_env_json(); });

    m_photo_tile_host->set_progress_handler([this](const std::string& job_id, const std::string& stage,
                                                   const std::string& stage_label, double pct) {
        if (job_id != m_photo_tile_active_job)
            return;                                   // 舊 job 殘響：不回推
        char pct_buf[32];
        ::snprintf(pct_buf, sizeof(pct_buf), "%.4f", pct);
        photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.jobProgress && "
            "window.PINGPhotoTile.jobProgress({jobId:\"") + ping_js_escape(job_id) +
            "\",stage:\"" + ping_js_escape(stage) +
            "\",stageLabel:\"" + ping_js_escape(stage_label) +
            "\",pct:" + pct_buf + "});");
    });

    m_photo_tile_host->set_status_handler([this](const std::string& status, const std::string& detail) {
        // 頁面拿來顯示 pct 還是 0 的階段說明（引擎啟動中/重建中/不可用）
        photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.engineStatus && "
            "window.PINGPhotoTile.engineStatus({status:\"") + ping_js_escape(status) +
            "\",detail:\"" + ping_js_escape(detail) + "\"});");
    });

    m_photo_tile_host->set_result_handler([this](const PhotoTileEngineResult& r) {
        if (r.job_id != m_photo_tile_active_job) {
            /* 取消後遲到的結果／被 supersede 的舊 job／引擎未就緒期間排隊補跑完的：
               一律丟棄（A 案：取消當下頁面已自行恢復、盤上舊磚不動）。 */
            BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：丟棄非現役 job 的結果 " << r.job_id
                                    << "（active=" << m_photo_tile_active_job << "）";
            return;
        }
        const std::string mode   = m_photo_tile_active_mode;
        const std::string nozzle = m_photo_tile_active_nozzle;
        m_photo_tile_active_job.clear();
        BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：result 回推 job=" << r.job_id << ", ok=" << r.ok
                                << (r.ok ? std::string() : ("，error=" + r.error_code));

        if (!r.ok) {
            photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.jobResult && "
                "window.PINGPhotoTile.jobResult({jobId:\"") + ping_js_escape(r.job_id) +
                "\",ok:false,errorCode:\"" + ping_js_escape(r.error_code) +
                "\",errorMessage:\"" + ping_js_escape(r.error_message) + "\"});");
            return;
        }

        // 成功文案素材（零件數等）從引擎 result 訊息撈；撈不到就 0，頁面自動用簡版文案
        int  parts = 0, extruders = 0;
        bool pillar = false;
        try {
            std::stringstream result_ss(r.result_json);
            pt::ptree t;
            pt::read_json(result_ss, t);
            parts     = t.get<int>("stats.parts", 0);
            extruders = t.get<int>("stats.extruders", 0);
            pillar    = t.get<bool>("stats.pillar", false);
        } catch (...) {}

        /* 覆審 I-2：上盤是非同步動作（CallAfter＋切片中可能拒載）——「✓ 已產生並載入
           列印板」只准在上盤動作真的執行後說；寫檔失敗／切片中拒載誠實回錯誤，
           不再有「頁面說成功、盤上停在上一顆磚」。 */
        const std::string job_id  = r.job_id;
        const int         wall_ms = r.wall_ms;
        /* env＝引擎原封回傳的請求快照（#9 guard 用）；deliver 內同步取值複製，
           不會留住這個指標。echo 掉了＝空字串＝guard fail-closed 誠實棄。 */
        photo_tile_deliver_3mf(r.three_mf, mode, nozzle, &r.env_json,
            [this, job_id, wall_ms, parts, extruders, pillar](bool ok, const std::string& err_code,
                                                              const std::string& err_msg) {
                if (!ok) {
                    photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.jobResult && "
                        "window.PINGPhotoTile.jobResult({jobId:\"") + ping_js_escape(job_id) +
                        "\",ok:false,errorCode:\"" + ping_js_escape(err_code) +
                        "\",errorMessage:\"" + ping_js_escape(err_msg) + "\"});");
                    return;
                }
                photo_tile_page_script(std::string("window.PINGPhotoTile && window.PINGPhotoTile.jobResult && "
                    "window.PINGPhotoTile.jobResult({jobId:\"") + ping_js_escape(job_id) +
                    "\",ok:true,wallMs:" + std::to_string(wall_ms) +
                    ",parts:" + std::to_string(parts) +
                    ",extruders:" + std::to_string(extruders) +
                    ",pillar:" + (pillar ? "true" : "false") + "});");
            });
    });
}

void GUI_App::photo_tile_shutdown_host()
{
    m_photo_tile_active_job.clear();
    /* C-2 第 3 項：預熱 timer 跟宿主同生共死。先收 timer 再收宿主——否則收攤途中
       到點的 tick 會把剛拆掉的宿主又建回來（本函式由 MainFrame 關窗與 OnExit 各呼叫
       一次，中間主迴圈還活著＝timer 真的還會跳）。這裡不是在 Notify() 內，delete 安全。 */
    m_photo_tile_warmup_fired = true;
    if (m_photo_tile_warmup_timer) {
        m_photo_tile_warmup_timer->Stop();
        delete m_photo_tile_warmup_timer;
        m_photo_tile_warmup_timer = nullptr;
    }
    if (!m_photo_tile_host)
        return;
    m_photo_tile_host->shutdown();
    m_photo_tile_host.reset();
}

// =====================================================================
// C-2 第 3 項：閒置預熱（Eric 2026-08-02 裁 A・2026-08-04 落地）
//
// 要解的問題（C-1 閘門①實錄，不是臆測）：開 app 後第一次生成要 6,579ms、UI 漂移
// 4,503ms；同一顆引擎穩態只要 900ms／漂移 17ms。差額＝「建 WebView2 環境＋controller
// ＋導頁」的冷啟動成本撞上 app 自己的初始化。它完全可以在使用者還沒動作的閒置期先付掉，
// ⇒ 閘門① 的 startup 才有資格轉正（`smoke_gate1_nodelay_20260801.json` 的 productImplication）。
//
// 兩個點火點（**刻意只有兩個**）：
//   W1 閒置預熱＝post_init 尾端排 timer，到點才判「選中的是不是照片磚機」（Eric 裁 A 的條件）。
//   W3 進工作室＝`open_photo_tile()` 立即點火（使用者已經開了照片磚工作室＝比機型選擇
//      更強的意圖訊號；也接住「app 一開就拖照片」那條 W1 還沒到點的動線）。
//   W2（切換機型時預熱）**刻意不做**：它只多賺「切到照片磚機後、還沒進工作室」那段，
//      而 W3 已經在進工作室當下就點火了；為它去改 preset 切換路徑（多個呼叫點、與別線共用）
//      不划算。要補的話單獨一刀、單獨驗。
//
// 不點火的兩種情況（都要留 log，不可靜默）：
//   ①閘門／守夜模式（`PING_PHOTOTILE_SMOKE` 有設）——各閘門自己建宿主並量測記憶體與漂移，
//     多一顆長命宿主會污染數字；黃金基準也維持「閘門路徑零擾動」的保命索。
//   ②kill switch `PING_PHOTOTILE_NO_WARMUP`——對照組（驗預熱有效必須跑的 A/B）與緊急關閉。
// =====================================================================

namespace {
/* 自帶 Notify() 的 timer：預設建構的 wxTimer 不掛 owner、不走事件佇列，到點直接回呼。
   **刻意不用 `wxTimer(this)`＋Bind**——GUI_App 既有那個 `Bind(wxEVT_TIMER, …)`
   （on_start_subscribe_again，GUI_App.cpp:2654）沒有 id 過濾，共用 owner 會兩邊互收
   對方的 tick（對方 handler 會去動 subscribe_counter）。 */
class PingPhotoTileWarmupTimer : public wxTimer
{
public:
    explicit PingPhotoTileWarmupTimer(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void Notify() override { if (m_fn) m_fn(); }
private:
    std::function<void()> m_fn;
};

// 預熱被關掉了嗎（回 true 並填 why）
bool ping_photo_tile_warmup_disabled(std::string& why)
{
    if (::getenv("PING_PHOTOTILE_SMOKE") != nullptr)     { why = "閘門/守夜模式"; return true; }
    if (::getenv("PING_PHOTOTILE_NO_WARMUP") != nullptr) { why = "kill switch PING_PHOTOTILE_NO_WARMUP"; return true; }
    return false;
}

/* 延遲取值：預設 15 秒——這不是拍腦袋的數字，是 C-1 閘門①實證過的靜置點
   （`PING_PHOTOTILE_SMOKE_DELAY_MS=15000` 那輪冷啟動漂移只剩 63ms＝那時 app 已經靜下來）。
   `PING_PHOTOTILE_WARMUP_DELAY_MS` 可覆寫＝**驗證用**（不必為了看一次預熱等 15 秒）。 */
int ping_photo_tile_warmup_delay_ms()
{
    if (const char* env = ::getenv("PING_PHOTOTILE_WARMUP_DELAY_MS")) {
        const int v = ::atoi(env);
        if (v >= 0)
            return v;
    }
    return 15000;
}

const int PING_PHOTOTILE_WARMUP_DEFER_MS   = 10000;  // 切片中＝延後再問
const int PING_PHOTOTILE_WARMUP_MAX_DEFERS = 3;      // 有上限＝不無限期空轉（放棄後仍有 W3）
} // namespace

void GUI_App::photo_tile_schedule_warmup(int delay_ms)
{
    if (m_photo_tile_warmup_fired || is_closing())
        return;
    if (delay_ms < 0)
        delay_ms = ping_photo_tile_warmup_delay_ms();      // 預設 15 秒／env 覆寫（宣告處有說明）
    std::string why;
    if (ping_photo_tile_warmup_disabled(why)) {
        BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：不排程（" << why << "）";
        m_photo_tile_warmup_fired = true;                 // 這個 app 生命週期內不再問
        return;
    }
    if (!m_photo_tile_warmup_timer) {
        m_photo_tile_warmup_timer = new PingPhotoTileWarmupTimer([this]() {
            /* 判斷放在**到點時**而不是排程時：使用者可能在這段時間內換了機型。 */
            if (m_photo_tile_warmup_fired || is_closing() || mainframe == nullptr)
                return;
            /* 切片中就別去搶 CPU／記憶體（引擎樹＝5 個 WebView2 行程）：延後重排。
               有上限，用完就放棄——放棄不等於沒有預熱，進工作室那條路（W3）仍會點火。 */
            Plater* p = plater();
            if (p != nullptr && p->is_background_process_slicing() &&
                m_photo_tile_warmup_defers < PING_PHOTOTILE_WARMUP_MAX_DEFERS) {
                ++m_photo_tile_warmup_defers;
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：切片中，延後 " << PING_PHOTOTILE_WARMUP_DEFER_MS
                                        << "ms 再問（第 " << m_photo_tile_warmup_defers << " 次）";
                /* ⚠ 重排走 CallAfter，**不在 Notify() 內直接 StartOnce**：一次性 timer 的
                   Stop() 與 Notify() 的先後順序各 wx 版本／平台不一致，在自己的回呼裡重啟
                   有機會被隨後的 Stop() 吃掉 ⇒ 延後這條路就此靜默死掉（而且不會有人發現，
                   因為「沒預熱」看起來跟「還沒到點」一模一樣）。丟回事件圈就沒有這個疑慮。 */
                CallAfter([this]() {
                    if (m_photo_tile_warmup_fired || is_closing() || !m_photo_tile_warmup_timer)
                        return;
                    m_photo_tile_warmup_timer->StartOnce(PING_PHOTOTILE_WARMUP_DEFER_MS);
                });
                return;
            }
            /* Eric 0802 裁 A 的條件本體：**只有目前選中的機器是照片磚機才預建**。
               不是照片磚機就不付那份記憶體（判定走 capability 單一來源，不自己比字串）。
               沒中就不再重排——真的要用時 W3 會即時點火。 */
            const PhotoTileCapability cap = photo_tile_capability_of_selected_printer();
            if (!cap.is_photo_tile) {
                m_photo_tile_warmup_fired = true;
                BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：跳過——目前機型不是照片磚機（preset="
                                        << cap.preset_name << "）";
                return;
            }
            photo_tile_warmup_now("閒置預熱・選中照片磚機 " + cap.preset_name);
        });
    }
    BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：排程 " << delay_ms << "ms 後判定點火（C-2 第 3 項）";
    m_photo_tile_warmup_timer->StartOnce(delay_ms);
}

void GUI_App::photo_tile_warmup_now(const std::string& why)
{
    if (is_closing() || mainframe == nullptr)
        return;
    std::string disabled_why;
    if (ping_photo_tile_warmup_disabled(disabled_why)) {
        BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：不點火（" << disabled_why << "）";
        m_photo_tile_warmup_fired = true;
        return;
    }
    if (m_photo_tile_warmup_timer)
        m_photo_tile_warmup_timer->Stop();        // 已由這條路點火＝排程中那次不用再跑
    m_photo_tile_warmup_fired = true;

    photo_tile_ensure_host();
    if (!m_photo_tile_host)
        return;
    if (m_photo_tile_host->is_ready()) {
        BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：引擎已就緒，無需點火（why=" << why << "）";
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "PhotoTile 預熱：點火（why=" << why
                            << "）——第一次生成不必再等引擎冷啟動";
    /* start() 冪等（狀態機設計約束 D）：Ready／建立中都直接回 true，不會建第二棵引擎樹。
       同步失敗（runtime 缺、CreateEnv 立刻 FAILED）只是預熱沒成，**不回推頁面錯誤**——
       使用者根本還沒要求生成，這時候彈原因是噪音；真的按下產生時 generate() 會誠實回報
       （覆審 I-1 那條路）。宿主自己已把 stage/status 寫進 log，診斷追得到。 */
    m_photo_tile_host->start();
}

void GUI_App::photo_tile_page_script(const std::string& js)
{
    /* webview 不在（app 收攤中、GUI 重建中）＝安靜跳過；每段腳本自帶
       `window.PINGPhotoTile &&` 守門＝使用者已離開工作室頁時落在別頁也只是 no-op。 */
    if (!mainframe || !mainframe->m_webview)
        return;
    mainframe->m_webview->RunScript(from_u8(js));
}

std::string GUI_App::photo_tile_current_env_json()
{
    /* 【C-2 第 2 項・一輪 #9】「此刻」的環境快照：只放覆審點名、會讓遲到結果變質的
       兩樣——使用者選中的印表機 preset 與目前專案檔。生成期間使用者切機／開別的專案，
       遲到的結果就不准再強行切機＋蓋盤。
       刻意不放線材／盤面編輯狀態：上盤本來就整組切機（製程線材隨機型預設）；
       未存檔的盤面編輯在 request_open_project 有既有「儲存變更？」對話框保護
       （I-2 case b 判例＝使用者親眼決定，不算靜默）。欄位少而確定＝正常沒動的
       生成不可能被誤判成過期（誤殺比漏殺更傷這條動線）。
       取不到（收攤中）＝回空 ⇒ 蓋章端 log 提示、上盤端 fail-closed。 */
    if (!preset_bundle || plater() == nullptr)
        return std::string();
    return "{" + jfield("printer", preset_bundle->printers.get_selected_preset_name()) +
           "," + jfield("project", into_u8(plater()->get_project_filename())) + "}";
}

void GUI_App::photo_tile_deliver_3mf(const std::vector<unsigned char>& bytes,
                                     const std::string& mode, const std::string& nozzle,
                                     const std::string* result_env_json,
                                     std::function<void(bool ok, const std::string& err_code,
                                                        const std::string& err_msg)> done)
{
    /* 【C-2 第 2 項】env 紀律只適用引擎路徑（request 蓋章→引擎原封回傳）；指標當場
       取值複製＝不留呼叫端的參考跨越 CallAfter。null＝舊 export 鏈豁免（見 .hpp）。 */
    const bool        enforce_env = result_env_json != nullptr;
    const std::string result_env  = enforce_env ? *result_env_json : std::string();
    const boost::filesystem::path output_path = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("PING_photo_tile_%%%%-%%%%-%%%%.3mf");
    boost::nowide::ofstream output(output_path.string(), std::ios::binary);
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output.good() || bytes.empty()) {
        // 覆審 I-2：TEMP 滿／唯讀／防毒鎖檔不再靜默——回報 deliver_failed，盤上停在
        // 上一顆磚是「已知失敗」不是「假成功」。
        BOOST_LOG_TRIVIAL(warning) << "Unable to write generated photo tile 3MF: " << output_path.string();
        if (done)
            done(false, "deliver_failed",
                 "產生結果無法寫入暫存檔（" + output_path.string() + "），請檢查磁碟空間與權限後再試。");
        return;
    }

    const std::string project_path = output_path.string();
    // 先切機再開檔（順序鐵律）：載入端會把線材槽縮成配方數，反序會被機器預設 64 槽蓋回。
    const auto target = ping_resolve_photo_tile_printer(mode, nozzle);
    BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：3MF 落地 " << project_path
                            << "，目標機型=" << (target.preset_name.empty() ? std::string("(不切機)") : target.preset_name);
    CallAfter([this, project_path, target, done, enforce_env, result_env] {
        /* 【原子上盤 guard・#9 的真正守門點（Eric 0801 裁）】比對與「切機＋開檔」在
           同一個 UI 事件回呼內一氣呵成＝原子：比完到動手之間插不進任何使用者事件
           （host 端結果交付與這裡之間隔著一次事件佇列，使用者點擊可能排在前面——
           所以比對必須放這裡、不能放 result handler）。fail-closed：env 缺失＝視為
           過期（「缺快照＝不得預設放行」既有規則）。放在 busy_slicing 之前：過期的
           結果連「等等再載」都不該建議。 */
        if (enforce_env) {
            const std::string env_now = photo_tile_current_env_json();
            if (!PhotoTileEngineHost::env_is_fresh(result_env, env_now)) {
                BOOST_LOG_TRIVIAL(warning) << "PhotoTile 工作室：結果過期即棄（#9 原子上盤 guard）"
                                           << " env(request)=" << (result_env.empty() ? "(空)" : result_env)
                                           << " env(now)="     << (env_now.empty()    ? "(空)" : env_now);
                if (done)
                    done(false, "protocol_stale_env",
                         "產生期間您已切換機器或專案，為避免蓋掉目前的工作，這片照片磚沒有自動載入"
                         "（檔案保留在 " + project_path + "）。請回到照片磚頁再產生一次。");
                return;
            }
            BOOST_LOG_TRIVIAL(info) << "PhotoTile 工作室：env 新鮮，上盤放行（#9 guard）env=" << env_now;
        }
        /* 覆審 I-2：切片中 request_open_project 會拒載，但機型若先切就已被污染
           （「跳提示、機型被切、盤上沒磚、頁面說成功」）⇒ 這裡整組擋下：不切機、
           不開檔、誠實回報並附 3MF 路徑（檔案留著、不是孤兒）。 */
        if (plater() != nullptr && plater()->is_background_process_slicing()) {
            BOOST_LOG_TRIVIAL(warning) << "PhotoTile 工作室：切片進行中，暫不載入 " << project_path;
            if (done)
                done(false, "busy_slicing",
                     "切片進行中，暫時無法載入照片磚。請等切片完成後再按一次產生（已生成的檔案在 " +
                     project_path + "）。");
            return;
        }
        if (!target.preset_name.empty()) {
            /* #99 Q3 甲：照片磚機不在選機清單裡 ⇒ 切過去之前先把**這一台**裝上。
               只裝要用的那一台（不是 0907 之前的整組八個變體），而且已裝就不動。 */
            const bool just_installed = ping_install_photo_tile_printer(target.preset_name);
            Tab* printer_tab = get_tab(Preset::TYPE_PRINTER);
            if (printer_tab != nullptr && printer_tab->select_preset(target.preset_name) &&
                plater() != nullptr) {
                if (just_installed) {
                    // 系統代替使用者做了事就要說（否則印表機清單莫名多一台，不知道哪來的）
                    plater()->sidebar().update_presets(Preset::TYPE_PRINTER);
                    printer_tab->update_tab_ui();
                }
                /* PING(2026-09-12，牌 c-0912-PTI-01)：下面那句「線材隨機型預設」原本只是通知文案，程式沒有真的套——
                   Orca 切機時把不相容的舊料換成**同型別的 Generic**（Eric 0912 實錄：FD800 Pro＋PETG 切到
                   FD300 同進照片磚 0.4 nozzle ⇒ 四槽 Generic PETG，並被記進該機的 orca_presets 從此固定）。
                   這裡把「不相容、或屬別家 vendor（Generic 系）」的槽強制套回機型的 default_filament_profile；
                   使用者刻意選的相容料（user preset 或另一支照片磚專用支）不動。 */
                if (PresetBundle* bundle = preset_bundle) {
                    const Preset& printer_now = bundle->printers.get_selected_preset();
                    const auto*   dfp = printer_now.config.option<ConfigOptionStrings>("default_filament_profile");
                    if (dfp != nullptr && !dfp->values.empty()) {
                        int fixed = 0;
                        for (size_t i = 0; i < bundle->filament_presets.size(); ++i) {
                            const Preset* cur = bundle->filaments.find_preset(bundle->filament_presets[i], false);
                            const bool foreign = cur != nullptr && cur->vendor != nullptr && cur->vendor != printer_now.vendor;
                            if (cur != nullptr && cur->is_compatible && !foreign)
                                continue;
                            const std::string& want = dfp->values[std::min(i, dfp->values.size() - 1)];
                            if (want == bundle->filament_presets[i] || bundle->filaments.find_preset(want, false) == nullptr)
                                continue;
                            bundle->set_filament_preset(i, want);
                            ++fixed;
                        }
                        if (fixed > 0) {
                            plater()->sidebar().update_presets(Preset::TYPE_FILAMENT);
                            auto& combos = plater()->sidebar().combos_filament();
                            for (size_t i = 0; i < combos.size(); ++i)
                                combos[i]->update_ams_color();
                            BOOST_LOG_TRIVIAL(info) << "PhotoTile 切機：" << fixed << " 槽不相容／Generic 線材已套回機型預設";
                        }
                    }
                }
                std::string note = std::string("照片磚：已自動切換機型「") + target.preset_name +
                                   "」，製程與線材隨機型預設。";
                if (just_installed)
                    note += "（這台是照片磚專用機型，剛為你加入印表機清單。）";
                if (target.cross_series)
                    note += " ⚠ 你目前的機型沒有專屬的照片磚版本，這是同家族裡最接近的一台"
                            "——列印範圍與起始 G-code 會以它為準，請確認尺寸放得下。";
                // 2026-09-13 Eric 裁「甲」（牌 c-0913-PTI-01）：系統替使用者換掉了他選的機器，一定要講。
                if (!target.detached_from.empty())
                    note += " ⚠ 你原本選的「" + target.detached_from + "」是一台已跟系統脫鉤的自訂機器設定，"
                            "配不到照片磚專用線材與製程（料槽會顯示「不支援」），已改用系統內建的照片磚機型。";
                const bool important = target.cross_series || !target.detached_from.empty();
                plater()->get_notification_manager()->push_notification(
                    NotificationType::CustomNotification,
                    important ? NotificationManager::NotificationLevel::ImportantNotificationLevel
                              : NotificationManager::NotificationLevel::RegularNotificationLevel,
                    note);
            }
        } else if (target.known_mode && !target.already_selected) {
            /* 【2026-08-15 A 案・Eric 裁】配不到「已加入」的照片磚機 ⇒ **誠實報錯、不載入**，
               並保留 3MF 路徑（與 protocol_stale_env 同一種處置）。
               修正前是「跳個警告通知、照樣載入」——3MF 會落在當前那台不對的機器上被錯誤切片，
               而通知很容易被略過。寧可不載入，也不要靜默切錯機。
               兩種原因分開講，因為使用者能做的事不同：
                 ・有這台機但沒加入 → 去「選擇 3D 列印機 → 照片磚」加進來（做得到）
                 ・根本沒有這個組合（例：雙料×1.0 口徑，FD 家族無 1.0 機）→ 換口徑或換模式 */
            /* 🗑 2026-09-07（#99 Q3 甲）：原本這裡分兩種訊息，其中「有這台機但你還沒加入 ⇒
               請到『選擇 3D 列印機 → 照片磚』把它加進來」那一支已經**刪掉**——照片磚分頁沒了，
               那句話會把人送到一個不存在的地方。現在配不到就只有一種原因：這個模式×口徑
               真的沒有對應機（例：雙料×1.0，FD 家族沒有 1.0 的照片磚機）。 */
            const std::string msg =
                std::string("找不到符合這個模式與口徑的同進照片磚機型，為避免切到錯的機器，"
                            "這片照片磚沒有自動載入（檔案保留在 ") + project_path + "）。";
            BOOST_LOG_TRIVIAL(warning)
                << "PhotoTile 工作室：找不到對應的照片磚機型，不載入 " << project_path;
            if (done)
                done(false, "no_target_printer", msg);
            return;
        }
        request_open_project(project_path);
        // PING 0909 Q4-2 甲：載入排在上一個 CallAfter，這個排在它後面 ⇒ 板上有物件後才渲染縮圖寫回 3MF
        CallAfter([this, project_path] { ping_phototile_write_plate_thumbnail(project_path, 20); });   // 20 × 500 ms（載入在巢狀迴圈裡，立刻重排會全部撲空）
        // 覆審 I-2：成功回推排在上盤動作之後。未存變更提示按取消（case b）這層攔不到
        // ——那是使用者親眼看著對話框做的決定，不算靜默失敗。
        if (done)
            done(true, std::string(), std::string());
    });
}

} // GUI
} //Slic3r
