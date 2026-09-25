///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 KARBOWSKI Piotr
///|/ Copyright (c) 2022 ole00 @ole00
///|/ Copyright (c) 2020 Tuomas Kuosmanen @tigert
///|/ Copyright (c) 2020 Klaus Naumann
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Technologies.hpp"
#include "GUI_App.hpp"
#include "GUI_Init.hpp" // IWYU pragma: keep
#include "Widgets/UIColors.hpp"
#include "ThemePalette.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_Factories.hpp"
#include "luminary/layer/settings_spec/SettingsSpec.hpp"
#include "ConfigManipulation.hpp"
#include "TopBar.hpp"
#include "UpdatesUIManager.hpp"
#include "format.hpp"

// Localization headers: include the engine version first so everything in this file
// uses the DSKY/GUI version (the macros will take precedence over the functions).
// Also, there is a check that the former is not included from the DSKY module.
// This is the only place where we want to allow that, so define an override macro.
#define PREFLIGHT_ALLOW_ENGINE_I18N_IN_DSKY
#include "luminary/core/I18N.hpp"
#undef PREFLIGHT_ALLOW_ENGINE_I18N_IN_DSKY
#include "DSKY/GUI/I18N.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <exception>
#include <cstdlib>
#include <regex>
#include <string_view>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/richmsgdlg.h>
#include <wx/log.h>
#include <wx/intl.h>
#include <wx/hyperlink.h>

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/splash.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/fontutil.h>
#include <wx/weakref.h>
#ifdef __APPLE__
#include <wx/renderer.h>
#include <wx/treectrl.h>
#endif

#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

#include "luminary/platform/concurrency/WorkerPolicy.hpp"
#include "luminary/platform/files/FileIO.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/platform/concurrency/CpuAffinity.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/colour/rgb/Color.hpp"
#include "luminary/platform/paths/Paths.hpp"

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "3DScene.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PresetUpdaterWrapper.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Process.hpp"
#include "TD1SDialog.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/AppUpdater.hpp"
#include "../Utils/WinRegistry.hpp"
#include "DSKY/Config/Snapshot.hpp"
#include "ConfigSnapshotDialog.hpp"
#include "DSKY/GUI/Preferences.hpp" // IWYU pragma: keep
#include "Tab.hpp"
#include "SysInfoDialog.hpp"
#include "KBShortcutsDialog.hpp"
#include "UpdateDialogs.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp" // IWYU pragma: keep
#include "NotificationManager.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "PrintHostDialogs.hpp" // IWYU pragma: keep
#include "DesktopIntegrationDialog.hpp"
// #include "SendSystemInfoDialog.hpp"
#include "Downloader.hpp"
#include "PhysicalPrinterDialog.hpp"
// #include "UserAccount.hpp"
// #include "UserAccountUtils.hpp"
// #include "LoginDialog.hpp"
#include "PresetArchiveDatabase.hpp"

#include "BitmapCache.hpp"
//#include "Notebook.hpp"
#include "TopBar.hpp"

#ifdef __WXMSW__
#include <dbt.h>
#include <shlobj.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
// Define _MSW_DARK_MODE to enable dark mode code paths with our implementation
#define _MSW_DARK_MODE
#include "DarkMode.hpp"
#endif
#ifdef _WIN32
#include <boost/dll/runtime_symbol_info.hpp>
#endif
#ifdef __APPLE__
#include <unistd.h>
#endif

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
#include <boost/beast/core/detail/base64.hpp>
#include <boost/nowide/fstream.hpp>
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

// Needed for forcing menu icons back under gtk2 and gtk3
#if defined(__WXGTK20__) || defined(__WXGTK3__)
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#endif

using namespace std::literals;

namespace DSKY
{
using namespace Luminary;

class MainFrame;

#ifdef __WXMSW__
// DirectComposition splash - renders through a DWM pipeline immune to nVidia's OpenGL ICD.
// Uses a raw Win32 popup (WS_EX_NOREDIRECTIONBITMAP at CreateWindowEx time) + DComp surface.
MIDL_INTERFACE("C37EA93A-E7AA-450D-B16F-9746CB0407F3") IDCompositionDevice : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Commit() = 0;
    virtual HRESULT STDMETHODCALLTYPE WaitForCommitCompletion() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTargetForHwnd(HWND, BOOL, void **) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVisual(void **) = 0;
};
MIDL_INTERFACE("EACDD04C-117E-4E17-88F4-D1B12B0E3D89") IDCompositionTarget : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetRoot(IUnknown *) = 0;
};
MIDL_INTERFACE("4D93059D-097B-4651-9A60-F0F25116E2F3") IDCompositionVisual : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetOffsetX_1(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOffsetX_2(float) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOffsetY_1(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOffsetY_2(float) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransform_1(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransform_2(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransformParent(IUnknown *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEffect(IUnknown *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBitmapInterpolationMode(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBorderMode(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClip_1(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClip_2(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetContent(IUnknown *) = 0;
};

typedef HRESULT(WINAPI *PFN_DCompositionCreateDevice)(IDXGIDevice *, REFIID, void **);

struct DCompSplash
{
    HWND hwnd{NULL};
    IDCompositionDevice *device{nullptr};
    IDCompositionTarget *target{nullptr};
    IDCompositionVisual *visual{nullptr};

    // Create the splash at physical pixel dimensions. The caller provides the bitmap
    // already rendered at the correct DPI scale (the SVG rasterizer handles this).
    bool Create(HINSTANCE hInst, int x, int y, int w, int h, const wxImage &img)
    {
        // Guard against double-Create without Close
        if (hwnd)
            Close();

        static bool s_class_registered = false;
        if (!s_class_registered)
        {
            WNDCLASSW wc = {};
            wc.hInstance = hInst;
            wc.lpszClassName = L"pfSplash";
            wc.lpfnWndProc = DefWindowProcW;
            RegisterClassW(&wc);
            s_class_registered = true;
        }

        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"pfSplash", L"",
                               WS_POPUP | WS_VISIBLE, x, y, w, h, NULL, NULL, hInst, NULL);
        if (!hwnd)
            return false;

        ID3D11Device *d3d = nullptr;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0,
                          D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
        if (!d3d)
        {
            DestroyWindow(hwnd);
            hwnd = NULL;
            return false;
        }

        IDXGIDevice *dxgi = nullptr;
        d3d->QueryInterface(__uuidof(IDXGIDevice), (void **) &dxgi);

        // LOAD_LIBRARY_SEARCH_SYSTEM32 prevents DLL planting from CWD
        HMODULE hDcomp = LoadLibraryExA("dcomp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto pfnCreate = hDcomp ? (PFN_DCompositionCreateDevice) GetProcAddress(hDcomp, "DCompositionCreateDevice")
                                : nullptr;

        if (pfnCreate && dxgi)
        {
            pfnCreate(dxgi, __uuidof(IDCompositionDevice), (void **) &device);
            if (device)
            {
                HRESULT hr1 = device->CreateTargetForHwnd(hwnd, TRUE, (void **) &target);
                HRESULT hr2 = device->CreateVisual((void **) &visual);

                if (FAILED(hr1) || FAILED(hr2) || !target || !visual)
                {
                    // Cleanup partial state and bail
                    if (dxgi)
                        dxgi->Release();
                    d3d->Release();
                    Close();
                    return false;
                }

                // CreateSurface: vtable index 8 (IUnknown[3] + Commit,Wait,GetFrame,CreateTarget,CreateVisual,CreateSurface)
                // These offsets are frozen by COM binary compatibility for the interface GUID above.
                typedef HRESULT(STDMETHODCALLTYPE * PFN_CreateSurface)(void *, UINT, UINT, DXGI_FORMAT, UINT, void **);
                auto fnCreateSurface = (PFN_CreateSurface) (*(void ***) device)[8];
                IUnknown *surface = nullptr;
                fnCreateSurface(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 1 /*DXGI_ALPHA_MODE_PREMULTIPLIED*/,
                                (void **) &surface);

                if (surface)
                {
                    // BeginDraw/EndDraw: vtable index 3/4 (IUnknown[3] + BeginDraw, EndDraw)
                    typedef HRESULT(STDMETHODCALLTYPE * PFN_BeginDraw)(void *, const RECT *, REFIID, void **, POINT *);
                    typedef HRESULT(STDMETHODCALLTYPE * PFN_EndDraw)(void *);
                    auto fnBeginDraw = (PFN_BeginDraw) (*(void ***) surface)[3];
                    auto fnEndDraw = (PFN_EndDraw) (*(void ***) surface)[4];

                    POINT offset = {};
                    ID3D11Texture2D *tex = nullptr;
                    if (SUCCEEDED(fnBeginDraw(surface, nullptr, __uuidof(ID3D11Texture2D), (void **) &tex, &offset)))
                    {
                        std::vector<unsigned char> pixels(w * h * 4);
                        unsigned char *src_rgb = img.GetData();
                        unsigned char *src_a = img.HasAlpha() ? img.GetAlpha() : nullptr;
                        for (int py = 0; py < h; py++)
                            for (int px = 0; px < w; px++)
                            {
                                int si = py * w + px;
                                int di = si * 4;
                                unsigned char a = src_a ? src_a[si] : 255;
                                pixels[di + 0] = (src_rgb[si * 3 + 2] * a + 127) / 255; // B
                                pixels[di + 1] = (src_rgb[si * 3 + 1] * a + 127) / 255; // G
                                pixels[di + 2] = (src_rgb[si * 3 + 0] * a + 127) / 255; // R
                                pixels[di + 3] = a;
                            }

                        ID3D11DeviceContext *ctx = nullptr;
                        d3d->GetImmediateContext(&ctx);
                        D3D11_BOX box = {(UINT) offset.x,       (UINT) offset.y,       0,
                                         (UINT) (offset.x + w), (UINT) (offset.y + h), 1};
                        ctx->UpdateSubresource(tex, 0, &box, pixels.data(), w * 4, 0);
                        ctx->Release();
                        tex->Release();
                        fnEndDraw(surface);
                    }
                    visual->SetContent(surface);
                    surface->Release();
                }
                target->SetRoot(visual);
                device->Commit();
            }
        }

        if (dxgi)
            dxgi->Release();
        d3d->Release();

        // If DComp setup failed, destroy the orphaned window
        if (!device)
        {
            DestroyWindow(hwnd);
            hwnd = NULL;
            return false;
        }
        return true;
    }

    void Close()
    {
        if (visual)
        {
            visual->Release();
            visual = nullptr;
        }
        if (target)
        {
            target->Release();
            target = nullptr;
        }
        if (device)
        {
            device->Release();
            device = nullptr;
        }
        if (hwnd)
        {
            DestroyWindow(hwnd);
            hwnd = NULL;
        }
    }
};

static DCompSplash s_dcomp_splash;
#endif // __WXMSW__

// wxSplashScreen shows itself in its constructor before we can control it, causing flash on wrong monitor
// By using wxFrame directly, we control exactly when the window becomes visible
class SplashScreen : public wxFrame
{
public:
    static wxPoint CalculateCenteredPosition(const wxBitmap &bitmap, wxPoint hint_pos)
    {
        int display_idx = 0;
        if (hint_pos != wxDefaultPosition)
        {
            int idx = wxDisplay::GetFromPoint(hint_pos);
            if (idx != wxNOT_FOUND)
                display_idx = idx;
        }
        wxDisplay display(display_idx);
        wxRect display_rect = display.GetClientArea();
        int splash_x = display_rect.GetX() + (display_rect.GetWidth() - bitmap.GetWidth()) / 2;
        int splash_y = display_rect.GetY() + (display_rect.GetHeight() - bitmap.GetHeight()) / 2;
        return wxPoint(splash_x, splash_y);
    }

    SplashScreen(const wxBitmap &bitmap, long splashStyle, int milliseconds, wxPoint pos = wxDefaultPosition)
        : wxFrame(nullptr, wxID_ANY, wxEmptyString, CalculateCenteredPosition(bitmap, pos),
                  wxSize(bitmap.GetWidth(), bitmap.GetHeight()),
                  wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#if defined(__WXMSW__) || (defined(__linux__) && defined(__aarch64__))
                      | wxFRAME_SHAPED // RPi/VNC needs shaped window; desktop Linux RGBA visual conflicts with SHAPED
#endif
          )
    {
        wxASSERT(bitmap.IsOk());

#ifdef __WXMSW__
        // DirectComposition renders the splash through a pipeline immune to nVidia's OpenGL ICD.
        // The wxFrame stays hidden; the DComp raw popup provides the visible transparent splash.
        {
            wxPoint splash_pos = GetPosition();
            wxImage img = bitmap.ConvertToImage();
            m_dcomp_ok = s_dcomp_splash.Create(GetModuleHandle(NULL), splash_pos.x, splash_pos.y, bitmap.GetWidth(),
                                               bitmap.GetHeight(), img);
        }
        // Fallback: if DComp isn't available (RDP, no GPU), use the wxFrame with UpdateLayeredWindow.
        // The nVidia transparency glitch may occur, but the splash at least appears.
        if (!m_dcomp_ok)
        {
            HWND fhwnd = (HWND) this->GetHandle();
            if (fhwnd)
            {
                SetWindowLong(fhwnd, GWL_EXSTYLE, GetWindowLong(fhwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
                HDC screenDC = GetDC(NULL);
                HDC memDC = CreateCompatibleDC(screenDC);
                BITMAPINFO bmi = {0};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = bitmap.GetWidth();
                bmi.bmiHeader.biHeight = -bitmap.GetHeight();
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void *ppvBits;
                HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &ppvBits, NULL, 0);
                if (hBmp)
                {
                    wxImage img2 = bitmap.ConvertToImage();
                    if (img2.HasAlpha())
                    {
                        unsigned char *data = (unsigned char *) ppvBits;
                        unsigned char *rgb = img2.GetData();
                        unsigned char *alpha = img2.GetAlpha();
                        int count = img2.GetWidth() * img2.GetHeight();
                        for (int i = 0; i < count; i++)
                        {
                            unsigned char a = alpha[i];
                            data[i * 4 + 0] = (rgb[i * 3 + 2] * a + 127) / 255;
                            data[i * 4 + 1] = (rgb[i * 3 + 1] * a + 127) / 255;
                            data[i * 4 + 2] = (rgb[i * 3 + 0] * a + 127) / 255;
                            data[i * 4 + 3] = a;
                        }
                    }
                    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
                    wxPoint p = GetPosition();
                    POINT ptDst = {p.x, p.y};
                    SIZE sizeWnd = {bitmap.GetWidth(), bitmap.GetHeight()};
                    POINT ptSrc = {0, 0};
                    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                    UpdateLayeredWindow(fhwnd, screenDC, &ptDst, &sizeWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA);
                    SelectObject(memDC, oldBmp);
                    DeleteObject(hBmp);
                }
                DeleteDC(memDC);
                ReleaseDC(NULL, screenDC);
            }
        }
#else
        // Linux/Mac - paint the splash bitmap with transparency support
        m_bitmap = bitmap;
        SetBackgroundStyle(wxBG_STYLE_PAINT);

#ifdef __WXGTK3__
        // Enable RGBA visual for true window transparency.
        // Only works when a compositor is active; without one the RGBA visual
        // exists but transparency renders as black.
        {
            GtkWidget *widget = static_cast<GtkWidget *>(GetHandle());
            GdkScreen *screen = gtk_widget_get_screen(widget);
            if (gdk_screen_is_composited(screen))
            {
                GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
                m_has_alpha = (visual != nullptr);
                if (m_has_alpha)
                {
                    gtk_widget_set_visual(widget, visual);
                    gtk_widget_set_app_paintable(widget, TRUE);
                }
            }
        }
        // No compositor: use a shaped window so transparent areas are cut out
        if (!m_has_alpha && m_bitmap.IsOk())
        {
            wxImage shape_img = m_bitmap.ConvertToImage();
            if (shape_img.HasAlpha())
            {
                // Low threshold: only fully transparent pixels are removed from shape
                shape_img.ConvertAlphaToMask(1);
                SetShape(wxRegion(wxBitmap(shape_img)));
            }
        }
#elif defined(__WXOSX__)
        // Make the window non-opaque so Quartz composites alpha correctly
        DSKY::mac_set_window_transparent(GetHandle());
        m_has_alpha = true;
#endif

        Bind(wxEVT_PAINT,
             [this](wxPaintEvent &)
             {
                 wxAutoBufferedPaintDC dc(this);
#if defined(__WXGTK3__) || defined(__WXOSX__)
                 if (m_has_alpha)
                 {
                     // Clear to fully transparent, then composite the bitmap over it.
                     // GTK3 requires an RGBA visual (set above); macOS Quartz supports
                     // alpha compositing natively once the window is non-opaque.
                     auto gc = wxGraphicsContext::Create(dc);
                     if (gc)
                     {
                         wxSize cs = GetClientSize();
                         gc->SetCompositionMode(wxCOMPOSITION_SOURCE);
                         gc->SetBrush(wxBrush(wxColour(0, 0, 0, 0)));
                         gc->DrawRectangle(0, 0, cs.GetWidth(), cs.GetHeight());
                         gc->SetCompositionMode(wxCOMPOSITION_OVER);
                         gc->DrawBitmap(m_bitmap, 0, 0, cs.GetWidth(), cs.GetHeight());
                         delete gc;
                     }
                 }
                 else
#endif
                 {
                     // Shaped window fallback or non-GTK3 path:
                     // Just paint the bitmap; shaped window handles transparency
                     dc.SetBackground(wxBrush(wxColour(34, 34, 34)));
                     dc.Clear();
                     if (m_bitmap.IsOk())
                         dc.DrawBitmap(m_bitmap, 0, 0, true);
                 }
             });
#endif

        Bind(wxEVT_CLOSE_WINDOW,
             [this](wxCloseEvent &evt)
             {
#ifdef __WXMSW__
                 s_dcomp_splash.Close();
                 if (!m_dcomp_ok)
                 {
                     HWND fhwnd = (HWND) GetHandle();
                     if (fhwnd)
                         SetLayeredWindowAttributes(fhwnd, 0, 0, LWA_ALPHA);
                 }
#endif
                 Hide();
                 evt.Skip();
             });

        if (milliseconds > 0)
        {
            m_timer.SetOwner(this, wxID_HIGHEST + 2);
            Bind(wxEVT_TIMER, [this](wxTimerEvent &) { Close(); }, wxID_HIGHEST + 2);
            m_timer.StartOnce(milliseconds);
        }

#ifdef __WXMSW__
        if (!m_dcomp_ok)
            Show(); // Fallback: show the wxFrame with UpdateLayeredWindow
#else
        Show();
#endif
    }

    // API compatibility - text overlay not used in simplified splash
    void SetText(const wxString &text)
    {
#ifdef __WXOSX__
        wxYield();
#endif
    }

    // API compatibility - returns bitmap as-is
    static wxBitmap MakeBitmap(wxBitmap bmp) { return bmp.IsOk() ? bmp : wxNullBitmap; }

private:
    wxTimer m_timer;
    wxBitmap m_bitmap;
    bool m_has_alpha{false};
#ifdef __WXMSW__
    bool m_dcomp_ok{false};
#endif
};

#ifdef __linux__
bool static check_old_linux_datadir(const wxString &app_name)
{
    // If we are on Linux and the datadir does not exist yet, look into the old
    // location where the datadir was before version 2.3. If we find it there,
    // tell the user that he might wanna migrate to the new location.
    // (Fix for Linux datadir migration from pre-2.3 versions)
    // To be precise, the datadir should exist, it is created when single instance
    // lock happens. Instead of checking for existence, check the contents.

    std::string new_path = Luminary::data_dir();

    // If the config folder is redefined - do not check
    // This happens when the user specifies a custom --datadir.
    if (new_path != get_default_datadir())
        return true;

    namespace fs = boost::filesystem;

    fs::path data_dir = fs::path(new_path);
    if (!fs::is_directory(data_dir))
        return true; // This should not happen.

    int file_count = std::distance(fs::directory_iterator(data_dir), fs::directory_iterator());

    if (file_count <= 1)
    { // just cache dir with an instance lock
        std::string old_path = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();

        if (fs::is_directory(old_path))
        {
            wxString msg = from_u8(
                (boost::format(_u8L(
                     "Starting with %1% 2.3, configuration "
                     "directory on Linux has changed (according to XDG Base Directory Specification) to \n%2%.\n\n"
                     "This directory did not exist yet (maybe you run the new version for the first time).\nHowever, "
                     "an old %1% configuration directory was detected in \n%3%.\n\n"
                     "Consider moving the contents of the old directory to the new location in order to access "
                     "your profiles, etc.\nNote that if you decide to downgrade %1% in future, it will use the old "
                     "location again.\n\n"
                     "What do you want to do now?")) %
                 PREFLIGHT_APP_NAME % new_path % old_path)
                    .str());
            wxString caption = from_u8((boost::format(_u8L("%s - BREAKING CHANGE")) % PREFLIGHT_APP_NAME).str());
            RichMessageDialog dlg(nullptr, msg, caption, wxYES_NO);
            dlg.SetYesNoLabels(_L("Quit, I will move my data now"), _L("Start the application"));
            if (dlg.ShowModal() != wxID_NO)
                return false;
        }
    }
    else
    {
        // If the new directory exists, be silent. The user likely already saw the message.
    }
    return true;
}
#endif

struct FileWildcards
{
    std::string_view title;
    std::vector<std::string_view> file_extensions;
};

static const FileWildcards file_wildcards_by_type[FT_SIZE] = {
    /* FT_STL */ {"STL files"sv, {".stl"sv}},
    /* FT_OBJ */ {"OBJ files"sv, {".obj"sv}},
    /* FT_OBJECT */ {"Object files"sv, {".stl"sv, ".obj"sv}},
    /* FT_STEP */ {"STEP files"sv, {".stp"sv, ".step"sv}},
    /* FT_AMF */ {"AMF files"sv, {".amf"sv, ".zip.amf"sv, ".xml"sv}},
    /* FT_3MF */ {"3MF files"sv, {".3mf"sv}},
    /* FT_GCODE */ {"G-code files"sv, {".gcode"sv, ".gco"sv, ".bgcode"sv, ".bgc"sv, ".g"sv, ".ngc"sv}},
    /* FT_MODEL */
    {"Known files"sv, {".stl"sv, ".obj"sv, ".3mf"sv, ".amf"sv, ".zip.amf"sv, ".xml"sv, ".step"sv, ".stp"sv, ".svg"sv}},
    /* FT_PROJECT */ {"Project files"sv, {".3mf"sv, ".amf"sv, ".zip.amf"sv}},
    /* FT_FONTS */ {"Font files"sv, {".ttc"sv, ".ttf"sv}},
    /* FT_GALLERY */ {"Known files"sv, {".stl"sv, ".obj"sv}},

    /* FT_INI */ {"INI files"sv, {".ini"sv}},
    /* FT_SVG */ {"SVG files"sv, {".svg"sv}},

    /* FT_TEX */ {"Texture"sv, {".png"sv, ".svg"sv}},

    /* FT_ZIP */ {"Zip files"sv, {".zip"sv}},
};

// This function produces a Win32 file dialog file template mask to be consumed by wxWidgets on all platforms.
// The function accepts a custom extension parameter. If the parameter is provided, the custom extension
// will be added as a fist to the list. This is important for a "file save" dialog on OSX, which strips
// an extension from the provided initial file name and substitutes it with the default extension (the first one in the template).
static wxString file_wildcards(const FileWildcards &wildcards, const std::string &custom_extension)
{
    std::string title;
    std::string mask;
    std::string custom_ext_lower;

    // Collects items for each of the extensions one by one.
    wxString out_one_by_one;
    auto add_single = [&out_one_by_one](const std::string_view title, const std::string_view ext)
    {
        out_one_by_one += DSKY::format_wxstr("|%s (*%s)|*%s", title, ext, ext);
    };

    if (!custom_extension.empty())
    {
        // Generate a custom extension into the title mask and into the list of extensions.
        // Add default version (upper, lower or mixed) first based on custom extension provided.
        title = std::string("*") + custom_extension;
        mask = title;
        add_single(wildcards.title, custom_extension);
        custom_ext_lower = boost::to_lower_copy(custom_extension);
        const std::string custom_ext_upper = boost::to_upper_copy(custom_extension);
        if (custom_ext_lower == custom_extension)
        {
            // Add one more variant - the upper case extension.
            mask += ";*";
            mask += custom_ext_upper;
            add_single(wildcards.title, custom_ext_upper);
        }
        else if (custom_ext_upper == custom_extension)
        {
            // Add one more variant - the lower case extension.
            mask += ";*";
            mask += custom_ext_lower;
            add_single(wildcards.title, custom_ext_lower);
        }
    }

    for (const std::string_view &ext : wildcards.file_extensions)
        // Only add an extension if it was not added first as the custom extension.
        if (ext != custom_ext_lower)
        {
            if (title.empty())
            {
                title = "*";
                title += ext;
                mask = title;
            }
            else
            {
                title += ", *";
                title += ext;
                mask += ";*";
                mask += ext;
            }
            mask += ";*";
            mask += boost::to_upper_copy(std::string(ext));
            add_single(wildcards.title, ext);
        }

    return DSKY::format_wxstr("%s (%s)|%s", wildcards.title, title, mask) + out_one_by_one;
}

wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    return file_wildcards(file_wildcards_by_type[file_type], custom_extension);
}

static std::string engine_translate_callback(const char *s)
{
    return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data();
}

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3, 1, 3)
static void register_win32_dpi_event()
{
    enum
    {
        WM_DPICHANGED_ = 0x02e0
    };

    wxWindow::MSWRegisterMessageHandler(WM_DPICHANGED_,
                                        [](wxWindow *win, WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
                                        {
                                            const int dpi = wParam & 0xffff;
                                            const auto rect = reinterpret_cast<PRECT>(lParam);
                                            const wxRect wxrect(wxPoint(rect->top, rect->left),
                                                                wxPoint(rect->bottom, rect->right));

                                            DpiChangedEvent evt(EVT_DPI_CHANGED_SLICER, dpi, wxrect);
                                            win->GetEventHandler()->AddPendingEvent(evt);

                                            return true;
                                        });
}
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

static GUID GUID_DEVINTERFACE_HID = {0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30};

static void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(
        WM_DEVICECHANGE,
        [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
        {
            // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
            auto main_frame = dynamic_cast<MainFrame *>(win);
            auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
            if (plater == nullptr)
                // Maybe some other top level window like a dialog or maybe a pop-up menu?
                return true;
            PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR) lParam;
            switch (wParam)
            {
            case DBT_DEVICEARRIVAL:
                if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                    plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
                else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
                {
                    PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE) lpdb;
                    //				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME) {
                    //					printf("DBT_DEVICEARRIVAL %d - Media has arrived: %ws\n", msg_count, lpdbi->dbcc_name);
                    if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
                        plater->GetEventHandler()->AddPendingEvent(
                            HIDDeviceAttachedEvent(EVT_HID_DEVICE_ATTACHED, into_u8(lpdbi->dbcc_name)));
                }
                break;
            case DBT_DEVICEREMOVECOMPLETE:
                if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                    plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
                else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
                {
                    PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE) lpdb;
                    //				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME)
                    //					printf("DBT_DEVICEARRIVAL %d - Media was removed: %ws\n", msg_count, lpdbi->dbcc_name);
                    if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
                        plater->GetEventHandler()->AddPendingEvent(
                            HIDDeviceDetachedEvent(EVT_HID_DEVICE_DETACHED, into_u8(lpdbi->dbcc_name)));
                }
                break;
            default:
                break;
            }
            return true;
        });

    wxWindow::MSWRegisterMessageHandler(
        MainFrame::WM_USER_MEDIACHANGED,
        [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
        {
            // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
            auto main_frame = dynamic_cast<MainFrame *>(win);
            auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
            if (plater == nullptr)
                // Maybe some other top level window like a dialog or maybe a pop-up menu?
                return true;
            wchar_t sPath[MAX_PATH];
            if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED)
            {
                struct _ITEMIDLIST *pidl = *reinterpret_cast<struct _ITEMIDLIST **>(wParam);
                if (!SHGetPathFromIDList(pidl, sPath))
                {
                    BOOST_LOG_TRIVIAL(error) << "MediaInserted: SHGetPathFromIDList failed";
                    return false;
                }
            }
            switch (lParam)
            {
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

    wxWindow::MSWRegisterMessageHandler(
        WM_INPUT,
        [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
        {
            auto main_frame = dynamic_cast<MainFrame *>(DSKY::find_toplevel_parent(win));
            auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
            //        if (wParam == RIM_INPUTSINK && plater != nullptr && main_frame->IsActive()) {
            if (wParam == RIM_INPUT && plater != nullptr && main_frame->IsActive())
            {
                RAWINPUT raw;
                UINT rawSize = sizeof(RAWINPUT);
                ::GetRawInputData((HRAWINPUT) lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
                if (raw.header.dwType == RIM_TYPEHID &&
                    plater->get_mouse3d_controller().handle_raw_input_win32(raw.data.hid.bRawData,
                                                                            raw.data.hid.dwSizeHid))
                    return true;
            }
            return false;
        });

    wxWindow::MSWRegisterMessageHandler(WM_COPYDATA,
                                        [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam)
                                        {
                                            COPYDATASTRUCT *copy_data_structure = {0};
                                            copy_data_structure = (COPYDATASTRUCT *) lParam;
                                            if (copy_data_structure->dwData == 1)
                                            {
                                                LPCWSTR arguments = (LPCWSTR) copy_data_structure->lpData;
                                                DSKY::wxGetApp().other_instance_message_handler()->handle_message(
                                                    into_u8(arguments));
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

    try
    {
        throw;
    }
    catch (const std::bad_alloc &ex)
    {
        // bad_alloc in main thread is most likely fatal. Report immediately to the user (wxLogError would be delayed)
        // and terminate the app so it is at least certain to happen now.
        wxString errmsg = wxString::Format(
            _L("%s has encountered an error. It was likely caused by running out of memory. "
               "If you are sure you have enough RAM on your system, this may also be a bug and we would "
               "be glad if you reported it.\n\nThe application will now terminate."),
            PREFLIGHT_APP_NAME);
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Fatal error"), wxOK | wxICON_ERROR);
        BOOST_LOG_TRIVIAL(error) << boost::format("std::bad_alloc exception: %1%") % ex.what();
        std::terminate();
    }
    catch (const boost::io::bad_format_string &ex)
    {
        wxString errmsg = _L("preFlight has encountered a localization error. "
                             "Please report to preFlight team, what language was active and in which scenario "
                             "this issue happened. Thank you.\n\nThe application will now terminate.");
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Critical error"), wxOK | wxICON_ERROR);
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        std::terminate();
        throw;
    }
    catch (const std::exception &ex)
    {
        wxLogError(format_wxstr(_L("Internal error: %1%"), ex.what()));
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        throw;
    }
}

void GUI_App::post_init()
{
    assert(initialized());
    if (!this->initialized())
        throw Luminary::RuntimeError("Calling post_init() while not yet initialized");

    // Initialize ImGui style now that app_config is ready
    if (m_imgui)
        m_imgui->refresh_style();

    if (this->is_gcode_viewer())
    {
        if (!this->init_params->input_files.empty())
            this->plater()->load_gcode(wxString::FromUTF8(this->init_params->input_files[0].c_str()));
    }
    else if (this->init_params->start_downloader)
    {
        start_download(this->init_params->download_url);
    }
    else
    {
        if (!this->init_params->preset_substitutions.empty())
            show_substitutions_info(this->init_params->preset_substitutions);

        if (!this->init_params->load_configs.empty())
            // Load the last config to give it a name at the UI. The name of the preset may be later
            // changed by loading an AMF or 3MF.
            // A print, filament or printer profile passed here is loaded as if it were a full config.
            this->mainframe->load_config_file(this->init_params->load_configs.back());
        // If loading a 3MF file, the config is loaded from the last one.
        if (!this->init_params->input_files.empty())
        {
            wxArrayString fns;
            for (const std::string &name : this->init_params->input_files)
                fns.Add(from_u8(name));
            if (plater()->load_files(fns) && this->init_params->input_files.size() == 1)
            {
                // Update application titlebar when opening a project file
                const std::string &filename = this->init_params->input_files.front();
                if (boost::algorithm::iends_with(filename, ".3mf"))
                    this->plater()->set_project_filename(from_u8(filename));
            }
            if (this->init_params->delete_after_load)
            {
                for (const std::string &p : this->init_params->input_files)
                {
                    boost::system::error_code ec;
                    boost::filesystem::remove(boost::filesystem::path(p), ec);
                    if (ec)
                    {
                        BOOST_LOG_TRIVIAL(error) << ec.message();
                    }
                }
            }
        }
        if (!this->init_params->extra_config.empty())
            this->mainframe->load_config(this->init_params->extra_config);

        if (this->init_params->selected_presets.has_valid_data())
        {
            if (Tab *printer_tab = get_tab(Preset::TYPE_PRINTER))
                printer_tab->select_preset(this->init_params->selected_presets.printer);

            const bool is_fff = preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF;
            if (Tab *print_tab = get_tab(Preset::TYPE_PRINT))
                print_tab->select_preset(this->init_params->selected_presets.print);

            if (Tab *print_tab = get_tab(Preset::TYPE_FILAMENT))
            {
                const auto &materials = this->init_params->selected_presets.materials;
                print_tab->select_preset(materials[0]);

                if (is_fff && materials.size() > 1)
                {
                    for (size_t idx = 1; idx < materials.size(); idx++)
                        preset_bundle->set_filament_preset(idx, materials[idx]);
                    sidebar().update_all_filament_comboboxes();
                }
            }
        }
    }

    // // show "Did you know" notification
    // if (app_config->get_bool("show_hints") && ! is_gcode_viewer())
    //     plater_->get_notification_manager()->push_hint_notification(true);

    // The extra CallAfter() is needed because of Mac, where this is the only way
    // to popup a modal dialog on start without screwing combo boxes.
    // Neither wxShowEvent nor wxWindowCreateEvent work reliably.
    // Commenting out preset updater sync to prevent vendor profile download errors
    // This removes attempts to download vendor .ini files from upstream repositories
    /*
    if (this->get_preset_updater_wrapper()) { // G-Code Viewer does not initialize preset_updater.
        CallAfter([this] {
            // preset_updater->sync downloads profile updates and than via event checks updates and incompatible presets. We need to run it on startup.
            // start before cw so it is canceled by cw if needed?
            this->get_preset_updater_wrapper()->sync_preset_updater(this, preset_bundle);
    */
    if (false)
    { // Disabled for preFlight
        CallAfter(
            [this]
            {
                bool cw_showed = this->config_wizard_startup();
                if (!cw_showed)
                {
                    // The CallAfter is needed as well, without it, GL extensions did not show.
                    // Also, we only want to show this when the wizard does not, so the new user
                    // sees something else than "we want something" on the first start.
                    // show_send_system_info_dialog_if_needed();
                }
            });
    }

    CallAfter(
        [this]
        {
            this->app_version_check(false);

            // Start the periodic version check (every 4 hours) if the user allows notifications
            std::string notify_opt = app_config->get("notify_release");
            if (notify_opt == "all" || notify_opt == "release")
            {
                m_version_check_timer = new wxTimer();
                m_version_check_timer->Bind(wxEVT_TIMER,
                                            [this](wxTimerEvent &)
                                            {
                                                // Re-check setting in case user changed it mid-session
                                                std::string opt = app_config->get("notify_release");
                                                if (opt == "all" || opt == "release")
                                                    this->app_version_check(false);
                                            });
                m_version_check_timer->Start(4 * 3600 * 1000); // 4 hours in milliseconds
            }
        });

    // Start TD1S filament sensor monitor
    if (!is_gcode_viewer())
    {
        m_td1s_sensor.start(
            [this](const TD1SReading &reading)
            {
                auto *dlg = new TD1SDialog(mainframe, reading.color, reading.td, reading.hex_color);
                dlg->ShowModal();
                dlg->Destroy();
            });
    }

    // Set preFlight version and save to preFlight.ini or preFlightGcodeViewer.ini.
    app_config->set("version", PREFLIGHT_VERSION);

#ifdef _WIN32
    // Sets window property to mainframe so other instances can indentify it.
    OtherInstanceMessageHandler::init_windows_properties(mainframe, m_instance_hash_int);
#endif //WIN32
}

IMPLEMENT_APP(GUI_App)

GUI_App::GUI_App(EAppMode mode)
    : wxApp()
    , m_app_mode(mode)
    , m_em_unit(10)
    , m_imgui(new ImGuiWrapper())
    , m_removable_drive_manager(std::make_unique<RemovableDriveManager>())
    , m_other_instance_message_handler(std::make_unique<OtherInstanceMessageHandler>())
    , m_downloader(std::make_unique<Downloader>())
{
    //app config initializes early because it is used in instance checking in preFlight.cpp
    this->init_app_config();
    // init app downloader after path to datadir is set
    m_app_updater = std::make_unique<AppUpdater>();
}

GUI_App::~GUI_App()
{
    // Stop TD1S sensor monitor
    m_td1s_sensor.stop();

    // Stop the periodic version check timer
    if (m_version_check_timer)
    {
        m_version_check_timer->Stop();
        delete m_version_check_timer;
        m_version_check_timer = nullptr;
    }

    // The updater's worker thread reads app_config (the release notes URL) while a version
    // check is in flight; as a member it would be destroyed only after this body, so it is
    // cancelled and joined here, before app_config goes
    m_app_updater.reset();

    delete app_config;
    delete preset_bundle;
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string GUI_App::get_gl_info(bool for_github)
{
    return OpenGLManager::get_gl_info().to_string(for_github);
}

wxGLContext *GUI_App::init_glcontext(wxGLCanvas &canvas)
{
#if PREFLIGHT_OPENGL_ES
    return m_opengl_mgr.init_glcontext(canvas);
#else
    return m_opengl_mgr.init_glcontext(canvas,
                                       init_params != nullptr ? init_params->opengl_version : std::make_pair(0, 0),
                                       init_params != nullptr ? init_params->opengl_compatibility_profile : false,
                                       init_params != nullptr ? init_params->opengl_debug : false);
#endif // PREFLIGHT_OPENGL_ES
}

bool GUI_App::init_opengl()
{
    bool status = m_opengl_mgr.init_gl();
    if (!m_opengl_initialized)
    {
        m_opengl_initialized = true;
        if (mainframe)
            mainframe->update_title();
    }
    return status;
}

static bool s_use_phong_lighting(const AppConfig *config)
{
    if (!config)
        return true;
    return OpenGLManager::get_gl_info().should_use_phong(config->get("canvas_lighting_quality"));
}

GLShaderProgram *GUI_App::get_utility_shader()
{
    if (s_use_phong_lighting(app_config))
    {
        GLShaderProgram *shader = get_shader("phong_light");
        if (shader)
            return shader;
    }
    return get_shader("gouraud_light");
}

GLShaderProgram *GUI_App::get_model_shader()
{
    if (s_use_phong_lighting(app_config))
    {
        GLShaderProgram *shader = get_shader("phong");
        if (shader)
            return shader;
    }
    return get_shader("gouraud");
}

// gets path to preFlight.ini, returns semver from first line comment
static boost::optional<Semver> parse_semver_from_ini(const std::string &path)
{
    boost::nowide::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string body = buffer.str();
    size_t start = body.find("preFlight ");
    if (start == std::string::npos)
        return boost::none;
    body = body.substr(start + 12);
    size_t end = body.find_first_of(" \n");
    if (end < body.size())
        body.resize(end);
    return Semver::parse(body);
}

void GUI_App::init_app_config()
{
    SetAppName(PREFLIGHT_APP_FULL_NAME);

    if (!app_config)
        app_config = new AppConfig(is_editor() ? AppConfig::EAppMode::Editor : AppConfig::EAppMode::GCodeViewer);

    // load settings
    m_app_conf_exists = app_config->exists();
    if (m_app_conf_exists)
    {
        std::string error = app_config->load();
        if (!error.empty())
        {
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            if (is_editor())
            {
                throw Luminary::RuntimeError(format(
                    "Error parsing preFlight config file, it is probably corrupted. "
                    "Try to manually delete the file to recover from the error. Your user profiles will not be affected."
                    "\n\n%1%\n\n%2%",
                    app_config->config_path(), error));
            }
            else
            {
                throw Luminary::RuntimeError(
                    format("Error parsing preFlightGCodeViewer config file, it is probably corrupted. "
                           "Try to manually delete the file to recover from the error."
                           "\n\n%1%\n\n%2%",
                           app_config->config_path(), error));
            }
        }
    }
}

std::array<std::string, 3> get_possible_app_names()
{
    const std::array<std::string, 3> suffixes{"-alpha", "-beta", ""};
    std::array<std::string, 3> result;
    std::transform(suffixes.begin(), suffixes.end(), result.begin(),
                   [](const std::string &suffix) { return PREFLIGHT_APP_KEY + suffix; });
    return result;
}

constexpr bool is_linux =
#if defined(__linux__)
    true
#else
    false
#endif
    ;

namespace fs = boost::filesystem;

std::vector<fs::path> get_app_config_dir_candidates(const std::string &current_app_name)
{
    std::vector<fs::path> candidates;

    // e.g. $HOME/.config
    const fs::path config_dir{fs::path{data_dir()}.parent_path()};
    const std::array<std::string, 3> possible_app_names{get_possible_app_names()};

    for (const std::string &possible_app_name : possible_app_names)
    {
        if (possible_app_name != current_app_name)
        {
            candidates.emplace_back(config_dir / possible_app_name);
        }
    }

    if constexpr (is_linux)
    {
        const std::optional<fs::path> home_config_dir{get_home_config_dir()};
        if (home_config_dir && config_dir != home_config_dir)
        {
            for (const std::string &possible_app_name : possible_app_names)
            {
                candidates.emplace_back(*home_config_dir / possible_app_name);
            }
        }
    }

    return candidates;
}

// returns old config path to copy from if such exists,
// returns an empty string if such config path does not exists or if it cannot be loaded.
std::string GUI_App::check_older_app_config(Semver current_version, bool backup)
{
    std::string older_data_dir_path;

    // If the config folder is redefined - do not check
    if (data_dir() != get_default_datadir())
        return {};

    // find other version app config (alpha / beta / release)
    const fs::path app_config_path{app_config->config_path()};
    const std::string filename{app_config_path.filename().string()};

    const std::string current_app_name{GetAppName().ToStdString()};
    const std::vector<fs::path> app_config_dir_candidates{get_app_config_dir_candidates(current_app_name)};

    Semver last_semver = current_version;
    for (const fs::path &candidate_dir : app_config_dir_candidates)
    {
        const fs::path candidate{candidate_dir / filename};
        if (boost::filesystem::exists(candidate))
        {
            // parse
            const boost::optional<Semver> other_semver = parse_semver_from_ini(candidate.string());
            if (other_semver && *other_semver > last_semver)
            {
                last_semver = *other_semver;
                older_data_dir_path = candidate.parent_path().string();
            }
        }
    }
    if (older_data_dir_path.empty())
        return {};
    BOOST_LOG_TRIVIAL(info) << "last app config file used: " << older_data_dir_path;
    // ask about using older data folder
    InfoDialog msg(
        nullptr, format_wxstr(_L("You are opening %1% version %2%."), PREFLIGHT_APP_NAME, PREFLIGHT_VERSION),
        backup ? format_wxstr(
                     _L("The active configuration was created by <b>%1% %2%</b>,"
                        "\nwhile a newer configuration was found in <b>%3%</b>"
                        "\ncreated by <b>%1% %4%</b>."
                        "\n\nShall the newer configuration be imported?"
                        "\nIf so, your active configuration will be backed up before importing the new configuration."),
                     PREFLIGHT_APP_NAME, current_version.to_string(), older_data_dir_path, last_semver.to_string())
               : format_wxstr(_L("An existing configuration was found in <b>%3%</b>"
                                 "\ncreated by <b>%1% %2%</b>."
                                 "\n\nShall this configuration be imported?"),
                              PREFLIGHT_APP_NAME, last_semver.to_string(), older_data_dir_path),
        true, wxYES_NO);

    if (backup)
    {
        msg.SetButtonLabel(wxID_YES, _L("Import"));
        msg.SetButtonLabel(wxID_NO, _L("Don't import"));
    }

    if (msg.ShowModal() == wxID_YES)
    {
        std::string snapshot_id;
        if (backup)
        {
            const Config::Snapshot *snapshot{nullptr};
            if (!DSKY::Config::take_config_snapshot_cancel_on_error(*app_config, Config::Snapshot::SNAPSHOT_USER, "",
                                                                    _u8L("Continue and import newer configuration?"),
                                                                    &snapshot))
                return {};
            if (snapshot)
            {
                // Save snapshot ID before loading the alternate AppConfig, as loading the alternate AppConfig may fail.
                snapshot_id = snapshot->id;
                assert(!snapshot_id.empty());
                app_config->set("on_snapshot", snapshot_id);
            }
            else
                BOOST_LOG_TRIVIAL(error) << "Failed to take congiguration snapshot";
        }

        // load app config from older file
        std::string error = app_config->load((boost::filesystem::path(older_data_dir_path) / filename).string());
        if (!error.empty())
        {
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            if (is_editor())
            {
                throw Luminary::RuntimeError(format(
                    "Error parsing preFlight config file, it is probably corrupted. "
                    "Try to manually delete the file to recover from the error. Your user profiles will not be affected."
                    "\n\n%1%\n\n%2%",
                    app_config->config_path(), error));
            }
            else
            {
                throw Luminary::RuntimeError(
                    format("Error parsing preFlightGCodeViewer config file, it is probably corrupted. "
                           "Try to manually delete the file to recover from the error."
                           "\n\n%1%\n\n%2%",
                           app_config->config_path(), error));
            }
        }
        if (!snapshot_id.empty())
            app_config->set("on_snapshot", snapshot_id);
        m_app_conf_exists = true;
        return older_data_dir_path;
    }
    return {};
}

void GUI_App::init_single_instance_checker(const std::string &name, const std::string &path)
{
    BOOST_LOG_TRIVIAL(debug) << "init wx instance checker " << name << " " << path;
    m_single_instance_checker = std::make_unique<wxSingleInstanceChecker>(boost::nowide::widen(name),
                                                                          boost::nowide::widen(path));
}

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
// Custom renderer that replaces the default platform-specific selection rect with a themed
// fill. Installed via wxRendererNative::Set() in on_init_inner(). Used by controls that route selection
// through wxRendererNative: the settings page tree (generic wxTreeCtrl on macOS/Linux) and the object
// list (generic wxDataViewCtrl on Windows). Native controls (Win32 TreeView, GTK/macOS DataViewCtrl)
// don't use this and are themed separately.
class preFlightRendererNative : public wxDelegateRendererNative
{
public:
    preFlightRendererNative() : wxDelegateRendererNative(wxRendererNative::GetDefault()) {}

    void DrawItemSelectionRect(wxWindow *win, wxDC &dc, const wxRect &rect, int flags = 0) override
    {
#ifdef __APPLE__
        // macOS: muted accent fill on wxTreeCtrl (the settings page tree). Native controls like the
        // object list (wxDataViewCtrl) don't route through this renderer and are themed separately.
        auto *tree = dynamic_cast<wxTreeCtrl *>(win);
        if (!tree)
        {
            m_rendererNative.DrawItemSelectionRect(win, dc, rect, flags);
            return;
        }

        if (!(flags & wxCONTROL_SELECTED))
            return;

        // GetBackgroundColour is unreliable on the macOS generic tree, so blend over the themed input
        // background. Muted = 1/4 accent + 3/4 base, matching the Linux selection tint.
        wxColour accent = UIColors::AccentPrimary();
        wxColour base = UIColors::InputBackground();
        wxColour bgCol((accent.Red() + base.Red() * 3) / 4, (accent.Green() + base.Green() * 3) / 4,
                       (accent.Blue() + base.Blue() * 3) / 4);

        wxDCPenChanger setPen(dc, wxPen(bgCol, 1));
        wxDCBrushChanger setBrush(dc, wxBrush(bgCol));
        dc.DrawRectangle(rect);

        // wx forces selected-item text to white on macOS; reset to the item's own themed color.
        wxColour txt;
        if (!(tree->GetWindowStyleFlag() & wxTR_MULTIPLE))
        {
            wxTreeItemId sel = tree->GetSelection();
            if (sel.IsOk())
                txt = tree->GetItemTextColour(sel);
        }
        if (!txt.IsOk())
            txt = tree->GetForegroundColour();
        dc.SetTextForeground(txt);
#elif defined(_WIN32)
        // Windows: the object list (generic wxDataViewCtrl) draws its selection through this renderer.
        // Fill with the themed selected-button background to match Linux/macOS instead of the native
        // highlight. The settings tree is a native Win32 TreeView and is themed separately (NppDarkMode).
        if (!(flags & wxCONTROL_SELECTED))
            return;

        wxColour bgCol = UIColors::SelectedBtnBackground();
        wxDCPenChanger setPen(dc, wxPen(bgCol, 1));
        wxDCBrushChanger setBrush(dc, wxBrush(bgCol));
        dc.DrawRectangle(rect);
        if (win)
            dc.SetTextForeground(win->GetForegroundColour());
#else
        // Linux: accent-tinted fill for all controls (wxTreeCtrl, wxDataViewCtrl, etc.)
        if (!(flags & wxCONTROL_SELECTED))
            return;

        wxColour accent = UIColors::AccentPrimary();
        wxColour base = win ? win->GetBackgroundColour() : UIColors::InputBackground();
        if (!base.IsOk())
            base = UIColors::InputBackground();
        wxColour bgCol((accent.Red() + base.Red() * 3) / 4, (accent.Green() + base.Green() * 3) / 4,
                       (accent.Blue() + base.Blue() * 3) / 4);

        wxDCPenChanger setPen(dc, wxPen(bgCol, 1));
        wxDCBrushChanger setBrush(dc, wxBrush(bgCol));
        dc.DrawRectangle(rect);

        if (auto *tree = dynamic_cast<wxTreeCtrl *>(win))
        {
            wxColour txt;
            if (!(tree->GetWindowStyleFlag() & wxTR_MULTIPLE))
            {
                wxTreeItemId sel = tree->GetSelection();
                if (sel.IsOk())
                    txt = tree->GetItemTextColour(sel);
            }
            if (!txt.IsOk())
                txt = tree->GetForegroundColour();
            dc.SetTextForeground(txt);
        }
        else if (win)
        {
            dc.SetTextForeground(win->GetForegroundColour());
        }
#endif
    }
};
#endif // __APPLE__ || __linux__

bool GUI_App::OnInit()
{
    try
    {
        return on_init_inner();
    }
    catch (const std::exception &)
    {
        generic_exception_handle();
        return false;
    }
}

void GUI_App::check_and_update_searcher(ConfigOptionMode mode /*= comExpert*/)
{
    std::vector<Search::InputInfo> search_inputs{};

    auto print_tech = preset_bundle->printers.get_selected_preset().printer_technology();
    for (auto tab : tabs_list)
        if (tab->supports_printer_technology(print_tech))
            search_inputs.emplace_back(Search::InputInfo{tab->get_config(), tab->type()});

    m_searcher->check_and_update(print_tech, mode, search_inputs);
}

void GUI_App::jump_to_option(const std::string &opt_key, Preset::Type type, const std::wstring &category)
{
    get_tab(type)->activate_option(opt_key, category);
}

void GUI_App::jump_to_option(size_t selected)
{
    const Search::Option &opt = m_searcher->get_option(selected);
    if (opt.type == Preset::TYPE_PREFERENCES)
        open_preferences(opt.opt_key(), into_u8(opt.group));
    else
        get_tab(opt.type)->activate_option(opt.opt_key(), into_u8(opt.category));
}

void GUI_App::jump_to_option(const std::string &composite_key)
{
    const auto separator_pos = composite_key.find(";");
    const std::string opt_key = composite_key.substr(0, separator_pos);
    const std::string tab_name = composite_key.substr(separator_pos + 1, composite_key.length());

    for (Tab *tab : tabs_list)
    {
        if (tab->name() == tab_name)
        {
            check_and_update_searcher();

            // Regularly searcher is sorted in respect to the options labels,
            // so resort searcher before get an option
            m_searcher->sort_options_by_key();
            const Search::Option &opt = m_searcher->get_option(opt_key, tab->type());
            tab->activate_option(opt_key, into_u8(opt.category));

            // Revert sort of searcher back
            m_searcher->sort_options_by_label();
            break;
        }
    }
}

void GUI_App::update_search_lines()
{
    mainframe->update_search_lines(m_searcher->search_string());
}

void GUI_App::show_search_dialog()
{
    check_and_update_searcher(get_mode());
    m_searcher->show_dialog();
}

static int get_app_font_pt_size(const AppConfig *app_config)
{
    if (!app_config->has("font_pt_size"))
        return -1;
    const int font_pt_size = atoi(app_config->get("font_pt_size").c_str());
    const int max_font_pt_size = wxGetApp().get_max_font_pt_size();

    return (font_pt_size > max_font_pt_size) ? max_font_pt_size : font_pt_size;
}

#if defined(__linux__) && !defined(PREFLIGHT_DESKTOP_INTEGRATION)
void GUI_App::remove_desktop_files_dialog()
{
    // Find all old existing desktop file
    std::vector<boost::filesystem::path> found_desktop_files;
    DesktopIntegrationDialog::find_all_desktop_files(found_desktop_files);
    if (found_desktop_files.empty())
    {
        return;
    }
    // Delete files.
    std::vector<boost::filesystem::path> fails;
    DesktopIntegrationDialog::remove_desktop_file_list(found_desktop_files, fails);
    if (fails.empty())
    {
        return;
    }
    // Inform about fails.
    std::string text = "Failed to remove desktop files:";
    text += "\n";
    for (const boost::filesystem::path &entry : fails)
    {
        text += DSKY::format("%1%\n", entry.string());
    }
    BOOST_LOG_TRIVIAL(error) << text;
}
#endif //(__linux__) && !defined(PREFLIGHT_DESKTOP_INTEGRATION)

// Surface a deferred "theme could not be loaded" message. The active palette is resolved at
// startup (and on a theme switch) before the GUI is painting, so load_active_theme() stashes the message
// and we drain it here. A modal dialog is used rather than a canvas notification because the failure can
// occur before the notification system is rendering, and the user must not miss that the theme didn't apply.
static void flush_theme_load_error()
{
    const std::string err = take_theme_load_error();
    if (err.empty())
        return;
    MessageDialog(nullptr, from_u8(err), _L("Theme could not be loaded"), wxOK | wxICON_WARNING).ShowModal();
}

bool GUI_App::on_init_inner()
{
    // wx assertions fire on paths the app does not control, so they are turned off.
    wxDisableAsserts();

    // Initialize the image handlers before any UI action
    wxInitAllImageHandlers();

    // Set our own gui log as an active target
    m_log_gui = new LogGui();
    wxLog::SetActiveTarget(m_log_gui);

#if defined(_WIN32) && !defined(_WIN64)
    // Win32 32bit build.
    if (wxPlatformInfo::Get().GetArchName().substr(0, 2) == "64")
    {
        RichMessageDialog dlg(
            nullptr,
            _L("You are running a 32 bit build of preFlight on 64-bit Windows."
               "\n32 bit build of preFlight will likely not be able to utilize all the RAM available in the system."
               "\nPlease download and install a 64 bit build of preFlight."
               "\nDo you wish to continue?"),
            "preFlight", wxICON_QUESTION | wxYES_NO);
        if (dlg.ShowModal() != wxID_YES)
            return false;
    }
#endif // _WIN64

    // Forcing back menu icons under gtk2 and gtk3. Solution is based on:
    // https://docs.gtk.org/gtk3/class.Settings.html
    // see also https://docs.wxwidgets.org/3.0/classwx_menu_item.html#a2b5d6bcb820b992b1e4709facbf6d4fb
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_object_set(gtk_settings_get_default(), "gtk-menu-images", TRUE, NULL);
#endif

    // Verify resources path
    const wxString resources_dir = from_u8(Luminary::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
                wxString::Format("Resources path does not exist or is not a directory: %s", resources_dir));

#ifdef __linux__
    if (!check_old_linux_datadir(GetAppName()))
    {
        std::cerr << "Quitting, user chose to move their data to new location." << std::endl;
        return false;
    }
#endif

    // Enable this to get the default Win32 COMCTRL32 behavior of static boxes.
    //    wxSystemOptions::SetOption("msw.staticbox.optimized-paint", 0);
    // Enable this to disable Windows Vista themes for all wxNotebooks. The themes seem to lead to terrible
    // performance when working on high resolution multi-display setups.
    //    wxSystemOptions::SetOption("msw.notebook.themed-background", 0);

    //     Luminary::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;

    // Initialization of UI settings as a language, application color mode, fonts... have to be done before first UI action.
    // Like here, before the show InfoDialog in check_older_app_config()

    // If load_language() fails, the application closes.
    load_language(wxString(), true);

    // Resolve and load the active theme palette before any color is
    // read or any window is created. dark_mode() reads the active theme's is_dark.
    load_active_theme();

#ifdef _MSW_DARK_MODE
    NppDarkMode::InitDarkMode(active_theme_is_dark(), false);
#endif
#ifdef __APPLE__
    // Pin the macOS app appearance to the active theme so native controls (buttons, static text)
    // render in the matching light/dark mode instead of following the system setting.
    mac_set_appearance(active_theme_is_dark());
#endif

    // Apply CPU stability preferences before any slicing work spins up TBB workers.
    // Thread cap is cross-platform; P-core-only affinity is supported on Windows x86 and Linux x86 only;
    // apply_pcore_only_affinity() is a safe no-op on unsupported platforms (ARM, macOS).
    {
        int max_threads = atoi(app_config->get("cpu_max_slicing_threads").c_str());
        if (max_threads > 0)
        {
            Luminary::thread_count = static_cast<std::size_t>(max_threads);
            Luminary::enforce_thread_count(static_cast<std::size_t>(max_threads));
        }
        if (app_config->get_bool("cpu_pcores_only"))
            Luminary::apply_pcore_only_affinity();
    }
    // Cache layout preference (requires restart to change)
    m_legacy_prepare_layout = app_config->get_bool("legacy_prepare_layout");

    // initialize label colors and fonts
    init_ui_colours();
    init_fonts();

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    // Install a custom renderer for themed selection fills (settings tree on macOS/Linux,
    // object list on Windows). Get() must be called first to trigger wxRendererPtr lazy
    // initialization; otherwise the first Get() after Set() calls DoInit(), which resets the
    // internal pointer to NULL and silently discards the custom renderer.
    (void) wxRendererNative::Get();
    wxRendererNative::Set(new preFlightRendererNative());
#endif

#ifdef __APPLE__
    // Native NSOutlineView controls (wxDataViewCtrl, e.g. the object list) draw their own selection and
    // bypass the renderer above. Match the object-list selection used on Windows/Linux: the themed
    // selected-button background (a subtle neutral highlight), not the accent.
    {
        const wxColour sel = UIColors::SelectedBtnBackground();
        mac_install_themed_table_selection(sel.Red(), sel.Green(), sel.Blue());
    }
#endif

    std::string older_data_dir_path;
    if (m_app_conf_exists)
    {
        if (app_config->orig_version().valid() && app_config->orig_version() < *Semver::parse(PREFLIGHT_VERSION))
        {
            // Only copying configuration if it was saved with a newer slicer than the one currently running.
            older_data_dir_path = check_older_app_config(app_config->orig_version(), true);
            m_last_app_conf_lower_version = true;
        }
    }
    else
    {
        // No AppConfig exists, fresh install. Always try to copy from an alternate location, don't make backup of the current configuration.
        older_data_dir_path = check_older_app_config(Semver(), false);
        if (!older_data_dir_path.empty())
            m_last_app_conf_lower_version = true;
    }

    // app_config may have been updated in check_older_app_config(); re-resolve the
    // active theme and re-apply colors if the resolved appearance changed.
    {
        const bool was_dark = active_theme_is_dark();
        load_active_theme();
        if (active_theme_is_dark() != was_dark)
        {
#ifdef _MSW_DARK_MODE
            NppDarkMode::SetDarkMode(active_theme_is_dark());
#endif
            init_ui_colours();
            update_ui_colours_from_appconfig();
        }
    }

#ifdef __linux__
    // Pin GTK's dark/light preference to the active theme at startup so native controls and
    // window decorations (title bar) match it regardless of the system GTK default. FALSE is
    // set explicitly for light themes, or a dark-default system keeps drawing dark native
    // controls under a light theme. This mirrors the macOS NSAppearance pin and the
    // recreate-GUI path, and runs before any dialog is shown so every window picks up the
    // right variant.
    if (GtkSettings *gtk_settings = gtk_settings_get_default())
        g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", active_theme_is_dark() ? TRUE : FALSE, NULL);

    // Override GTK's default blue selection highlight with the theme's accent color
    // for menus, tree views, and list views.
    // GTK3 CSS does NOT support !important - use PRIORITY_USER to override themes.
    {
        const ThemePalette &p = active_palette();
        wxColour menu_bg = p.menu_background;
        wxColour menu_fg = p.menu_text;
        wxColour accent = p.accent_primary;
        wxColour accent_text = p.accent_text;

        char css[2048];
        snprintf(css, sizeof(css),
                 // Menus
                 "menu { background-color:#%02x%02x%02x; color:#%02x%02x%02x; }"
                 " menuitem { color:#%02x%02x%02x; }"
                 " menuitem:hover { background-color:#%02x%02x%02x; color:#%02x%02x%02x; }"
                 // DataViewCtrl (ObjectList) selection - subtle highlight, keep text readable
                 " .view:selected, .view:selected:focus, .view:selected:backdrop"
                 " { background-color:#%02x%02x%02x; color:#%02x%02x%02x; }"
                 // Remove GTK's blue focus border on text entries (TextInput has its own border)
                 " entry { border:none; outline:none; box-shadow:none; }",
                 // Menu args
                 menu_bg.Red(), menu_bg.Green(), menu_bg.Blue(), menu_fg.Red(), menu_fg.Green(), menu_fg.Blue(),
                 menu_fg.Red(), menu_fg.Green(), menu_fg.Blue(), accent.Red(), accent.Green(), accent.Blue(),
                 accent_text.Red(), accent_text.Green(), accent_text.Blue(),
                 // DVC selection: bg + text color to prevent GTK from forcing white
                 p.selected_btn_background.Red(), p.selected_btn_background.Green(), p.selected_btn_background.Blue(),
                 p.input_foreground.Red(), p.input_foreground.Green(), p.input_foreground.Blue());

        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, css, -1, nullptr);
        GdkScreen *screen = gdk_screen_get_default();
        if (screen)
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                                      GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_unref(provider);
    }
#endif

    if (is_editor())
    {
        std::string msg = Http::tls_global_init();
        std::string ssl_cert_store = app_config->get("tls_accepted_cert_store_location");
        bool ssl_accept = app_config->get("tls_cert_store_accepted") == "yes" &&
                          ssl_cert_store == Http::tls_system_cert_store();

        if (!msg.empty() && !ssl_accept)
        {
            RichMessageDialog dlg(nullptr, wxString::Format(_L("%s\nDo you want to continue?"), msg), "preFlight",
                                  wxICON_QUESTION | wxYES_NO);
            dlg.ShowCheckBox(_L("Remember my choice"));
            if (dlg.ShowModal() != wxID_YES)
                return false;

            app_config->set("tls_cert_store_accepted", dlg.IsCheckBoxChecked() ? "yes" : "no");
            app_config->set("tls_accepted_cert_store_location",
                            dlg.IsCheckBoxChecked() ? Http::tls_system_cert_store() : "");
        }
    }

#if defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    // Desktop integration handling on Linux.
    if (DesktopIntegrationDialog::is_integrated() && DesktopIntegrationDialog::needs_path_update())
    {
        // Integrated but pointing to a different binary, so offer to migrate.
        RichMessageDialog dlg(nullptr,
                              _L("preFlight desktop integration points to a different version.\n"
                                 "Would you like to update it to this version?"),
                              "preFlight", wxICON_QUESTION | wxYES_NO);
        if (dlg.ShowModal() == wxID_YES)
        {
            DesktopIntegrationDialog::perform_desktop_integration();
        }
        // No "remember" checkbox: prompt again each launch until they say Yes.
    }
    else if (!DesktopIntegrationDialog::is_integrated() && app_config->get("desktop_integration_suppressed") != "yes")
    {
        // Not integrated yet, so offer first-time integration.
        RichMessageDialog dlg(nullptr, _L("Would you like to add preFlight to your desktop applications menu?"),
                              "preFlight", wxICON_QUESTION | wxYES_NO);
        dlg.ShowCheckBox(_L("Remember my choice"));
        if (dlg.ShowModal() == wxID_YES)
        {
            DesktopIntegrationDialog::perform_desktop_integration();
        }
        else if (dlg.IsCheckBoxChecked())
        {
            app_config->set("desktop_integration_suppressed", "yes");
        }
    }
#endif

    SplashScreen *scrn = nullptr;
    if (app_config->get_bool("show_splash_screen"))
    {
        wxBitmap bmp;

        // Determine target display based on saved window position
        int target_display_idx = 0;
        if (app_config->has("window_mainframe") && app_config->get_bool("restore_win_position"))
        {
            auto metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            if (metrics != boost::none)
            {
                // Use center of saved rect, not top-left - when maximized on Windows, the extended
                // frame bounds place the top-left slightly outside the monitor area (e.g. -7,-7),
                // causing GetFromPoint to fail and default to Monitor 1
                wxPoint center = metrics->get_rect().GetPosition() +
                                 wxSize(metrics->get_rect().GetWidth() / 2, metrics->get_rect().GetHeight() / 2);
                int display_idx = wxDisplay::GetFromPoint(center);
                if (display_idx != wxNOT_FOUND)
                    target_display_idx = display_idx;
            }
        }

        // Get screen dimensions and calculate splash size
        wxDisplay target_display(target_display_idx);
        wxRect screen_rect = target_display.GetClientArea();
        int screen_height = screen_rect.GetHeight();

        // Target splash height is ~60% of screen height, clamped to reasonable range
        // SVG aspect ratio is 456:636 = 0.717
        constexpr float svg_aspect_ratio = 456.0f / 636.0f;
        constexpr float screen_fraction = 0.60f;
        constexpr unsigned min_height = 400;
        constexpr unsigned max_height = 1200;

        unsigned target_height = static_cast<unsigned>(screen_height * screen_fraction + 0.5f);
        target_height = std::clamp(target_height, min_height, max_height);
        unsigned target_width = static_cast<unsigned>(target_height * svg_aspect_ratio + 0.5f);

        // Supersample factor for anti-aliasing (render at 4x, scale down)
        constexpr float supersample = 4.0f;
        unsigned render_width = static_cast<unsigned>(target_width * supersample);

#ifdef _WIN32
        // Load splash SVG from embedded Windows resource
        HRSRC hResource = FindResource(NULL, L"SPLASH_SVG", RT_RCDATA);
        if (hResource)
        {
            HGLOBAL hMemory = LoadResource(NULL, hResource);
            if (hMemory)
            {
                DWORD size = SizeofResource(NULL, hResource);
                const char *pData = static_cast<const char *>(LockResource(hMemory));
                if (pData && size > 0)
                {
                    // nsvgParse modifies input, so make a mutable copy
                    std::vector<char> svg_data(pData, pData + size);
                    svg_data.push_back('\0'); // Ensure null-terminated

                    NSVGimage *image = nsvgParse(svg_data.data(), "px", 96.0f);
                    if (image != nullptr)
                    {
                        // Calculate scale to fit render width (supersampled)
                        float svg_scale = static_cast<float>(render_width) / image->width;
                        int width = static_cast<int>(svg_scale * image->width + 0.5f);
                        int height = static_cast<int>(svg_scale * image->height + 0.5f);

                        if (width > 0 && height > 0)
                        {
                            NSVGrasterizer *rast = nsvgCreateRasterizer();
                            if (rast != nullptr)
                            {
                                std::vector<unsigned char> rgba_data(width * height * 4, 0);
                                nsvgRasterize(rast, image, 0, 0, svg_scale, rgba_data.data(), width, height, width * 4);
                                nsvgDeleteRasterizer(rast);

                                // Convert RGBA to wxImage
                                wxImage wximg(width, height);
                                wximg.InitAlpha();
                                unsigned char *rgb = wximg.GetData();
                                unsigned char *alpha = wximg.GetAlpha();
                                const unsigned char *src = rgba_data.data();
                                for (int i = 0; i < width * height; ++i)
                                {
                                    *rgb++ = *src++;
                                    *rgb++ = *src++;
                                    *rgb++ = *src++;
                                    *alpha++ = *src++;
                                }

                                int final_width = static_cast<int>(target_width);
                                int final_height = static_cast<int>(height / supersample + 0.5f);
                                wximg.Rescale(final_width, final_height, wxIMAGE_QUALITY_HIGH);

                                bmp = SplashScreen::MakeBitmap(wxBitmap(wximg));
                            }
                        }
                        nsvgDelete(image);
                    }
                }
            }
        }
        // Fallback to disk if embedded resource failed
        if (!bmp.IsOk())
#endif
        {
            // Load the splash SVG from disk and render it with NanoSVG (same quality as the Windows path)
            NSVGimage *image = nsvgParseFromFile(Luminary::var("preFlight-splash.svg").c_str(), "px", 96.0f);
            if (image != nullptr)
            {
                float svg_scale = static_cast<float>(render_width) / image->width;
                int width = static_cast<int>(svg_scale * image->width + 0.5f);
                int height = static_cast<int>(svg_scale * image->height + 0.5f);

                if (width > 0 && height > 0)
                {
                    NSVGrasterizer *rast = nsvgCreateRasterizer();
                    if (rast != nullptr)
                    {
                        std::vector<unsigned char> rgba_data(width * height * 4, 0);
                        nsvgRasterize(rast, image, 0, 0, svg_scale, rgba_data.data(), width, height, width * 4);
                        nsvgDeleteRasterizer(rast);

                        wxImage wximg(width, height);
                        wximg.InitAlpha();
                        unsigned char *rgb = wximg.GetData();
                        unsigned char *alpha = wximg.GetAlpha();
                        const unsigned char *src = rgba_data.data();
                        for (int i = 0; i < width * height; ++i)
                        {
                            *rgb++ = *src++;
                            *rgb++ = *src++;
                            *rgb++ = *src++;
                            *alpha++ = *src++;
                        }

                        int final_width = static_cast<int>(target_width);
                        int final_height = static_cast<int>(height / supersample + 0.5f);
                        wximg.Rescale(final_width, final_height, wxIMAGE_QUALITY_HIGH);

                        bmp = SplashScreen::MakeBitmap(wxBitmap(wximg));
                    }
                }
                nsvgDelete(image);
            }
        }

        // Detect position (display) to show the splash screen
        // Use center of saved rect as hint position - when maximized, the top-left of the extended
        // frame bounds can fall outside the monitor area, causing wrong-monitor detection
        wxPoint splashscreen_pos = wxDefaultPosition;
        bool default_splashscreen_pos = true;
        if (app_config->has("window_mainframe") && app_config->get_bool("restore_win_position"))
        {
            auto metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            default_splashscreen_pos = metrics == boost::none;
            if (!default_splashscreen_pos)
                splashscreen_pos = metrics->get_rect().GetPosition() +
                                   wxSize(metrics->get_rect().GetWidth() / 2, metrics->get_rect().GetHeight() / 2);
        }

        if (!default_splashscreen_pos)
        {
            // workaround for crash related to the positioning of the window on secondary monitor
            get_app_config()->set("restore_win_position", "crashed_at_splashscreen_pos");
            get_app_config()->save();
        }

        // create splash screen with updated bmp
        // wxSPLASH_CENTRE_ON_SCREEN would override our multi-monitor positioning
        // Pass 0 for timeout to disable the internal auto-close timer.
        // The external timer (below, after mainframe->Show) manages the splash lifetime
        // via wxWeakRef. Having two independent timers caused a use-after-free race.
        scrn = new SplashScreen(bmp.IsOk() ? bmp : get_bmp_bundle("preFlight", 400)->GetBitmap(wxSize(400, 400)),
                                wxSPLASH_TIMEOUT, 0, splashscreen_pos);

        if (!default_splashscreen_pos)
            // revert "restore_win_position" value if application wasn't crashed
            get_app_config()->set("restore_win_position", "1");
        // Removed wxYield() that was allowing events to process before initialization complete
        // #ifndef __linux__
        //     wxYield();
        // #endif
        scrn->SetText(_L("Loading configuration") + dots);
    }

    preset_bundle = new PresetBundle();

    // just checking for existence of Luminary::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    preset_bundle->setup_directories();

    if (!older_data_dir_path.empty())
    {
        preset_bundle->import_newer_configs(older_data_dir_path);
    }

    if (is_editor())
    {
#ifdef __WXMSW__
        if (app_config->get_bool("associate_3mf"))
            associate_3mf_files();
        if (app_config->get_bool("associate_stl"))
            associate_stl_files();
#endif // __WXMSW__

        m_preset_updater_wrapper = std::make_unique<PresetUpdaterWrapper>();
        Bind(EVT_PREFLIGHT_VERSION_ONLINE, &GUI_App::on_version_read, this);
        Bind(EVT_PREFLIGHT_EXPERIMENTAL_VERSION_ONLINE,
             [this](const wxCommandEvent &evt)
             {
                 if (this->plater_ != nullptr &&
                     (m_app_updater->get_triggered_by_user() || app_config->get("notify_release") == "all"))
                 {
                     std::string evt_string = into_u8(evt.GetString());
                     if (*Semver::parse(PREFLIGHT_VERSION) < *Semver::parse(evt_string))
                     {
                         auto notif_type = (evt_string.find("beta") != std::string::npos
                                                ? NotificationType::NewBetaAvailable
                                                : NotificationType::NewAlphaAvailable);
                         this->plater_->get_notification_manager()->push_version_notification(
                             notif_type, NotificationManager::NotificationLevel::ImportantNotificationLevel,
                             Luminary::format(_u8L("New prerelease version %1% is available."), evt_string),
                             _u8L("See Releases page."),
                             [](wxEvtHandler *evnthndlr)
                             {
                                 wxGetApp().open_browser_with_warning_dialog(
                                     "https://github.com/oozebot/preFlight/releases");
                                 return true;
                             });
                     }
                 }
             });
        Bind(EVT_PREFLIGHT_APP_DOWNLOAD_PROGRESS,
             [this](const wxCommandEvent &evt)
             {
                 //lm:This does not force a render. The progress bar only updateswhen the mouse is moved.
                 if (this->plater_ != nullptr)
                     this->plater_->get_notification_manager()->set_download_progress_percentage(
                         (float) std::stoi(into_u8(evt.GetString())) / 100.f);
             });

        Bind(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED,
             [this](const wxCommandEvent &evt)
             {
                 if (this->plater_ != nullptr)
                     this->plater_->get_notification_manager()->close_notification_of_type(
                         NotificationType::AppDownload);
                 if (!evt.GetString().IsEmpty())
                     show_error(nullptr, evt.GetString());
             });

        Bind(EVT_PREFLIGHT_APP_OPEN_FAILED, [](const wxCommandEvent &evt) { show_error(nullptr, evt.GetString()); });

        Bind(EVT_CONFIG_UPDATER_SYNC_DONE, [this](const wxCommandEvent &evt) { this->check_updates(false); });

        Bind(
            EVT_CONFIG_UPDATER_FAILED_ARCHIVE,
            [this](const wxCommandEvent &evt)
            {
                assert(!evt.GetString().empty());
                // TRN Notification text, %1% is list of vendors.
                std::string notification_text = format(
                    _u8L(
                        "Update check failed for the following vendors:\n\n%1%\nThis may be due to an account logout or a lost connection. Please verify your account status and internet connection. Then select \"Check for Configuration Updates\" to repeat."),
                    evt.GetString());
                notification_manager()->push_notification(
                    NotificationType::FailedSecretVendorUpdateSync,
                    NotificationManager::NotificationLevel::WarningNotificationLevel, notification_text);
            });
    }
    else
    {
#ifdef __WXMSW__
        if (app_config->get_bool("associate_gcode"))
            associate_gcode_files();
        if (app_config->get_bool("associate_bgcode"))
            associate_bgcode_files();
#endif // __WXMSW__
    }

    std::string delayed_error_load_presets;
    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(app_config->get_bool("no_defaults"));
    try
    {
        // Enable all substitutions (in both user and system profiles), but log the substitutions in user profiles only.
        // If there are substitutions in system profiles, then a "reconfigure" event shall be triggered, which will force
        // installation of a compatible system preset, thus nullifying the system preset substitutions.
        init_params->preset_substitutions =
            preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    }
    catch (const std::exception &ex)
    {
        delayed_error_load_presets = ex.what();
    }

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3, 1, 3)
    register_win32_dpi_event();
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN
    register_win32_device_notification_event();
#endif // WIN32

    // Let the engine know the callback, which will translate messages on demand.
    Luminary::I18N::set_translate_callback(engine_translate_callback);

    // application frame
    if (scrn && is_editor())
        scrn->SetText(_L("Preparing settings tabs") + dots);

    if (!delayed_error_load_presets.empty())
        show_error(nullptr, delayed_error_load_presets);

    mainframe = new MainFrame(get_app_font_pt_size(app_config));
    // hide settings tabs after first Layout
    if (is_editor())
        mainframe->select_tab(size_t(0));

#ifdef __linux__
    // Set the _GTK_THEME_VARIANT X11 property on the window to request
    // dark title bar decorations. This works on distros where
    // gtk-application-prefer-dark-theme alone isn't enough.
    if (dark_mode())
    {
        GtkWidget *gtk_win = static_cast<GtkWidget *>(mainframe->GetHandle());
        if (gtk_win)
        {
            gtk_widget_realize(gtk_win);
            GdkWindow *gdk_win = gtk_widget_get_window(gtk_win);
#ifdef GDK_WINDOWING_X11
            if (gdk_win && GDK_IS_X11_WINDOW(gdk_win))
            {
                Display *xdisplay = GDK_WINDOW_XDISPLAY(gdk_win);
                Window xwindow = GDK_WINDOW_XID(gdk_win);
                Atom variant_atom = XInternAtom(xdisplay, "_GTK_THEME_VARIANT", False);
                Atom utf8_atom = XInternAtom(xdisplay, "UTF8_STRING", False);
                const char *dark = "dark";
                XChangeProperty(xdisplay, xwindow, variant_atom, utf8_atom, 8, PropModeReplace,
                                (const unsigned char *) dark, strlen(dark));
            }
#endif
        }
    }
#endif

#if defined(__linux__) && !defined(PREFLIGHT_DESKTOP_INTEGRATION)
    remove_desktop_files_dialog();
#endif //(__linux__) && !defined(PREFLIGHT_DESKTOP_INTEGRATION)

    sidebar().obj_list()->init_objects(); // propagate model objects to object list
    update_mode(); // mode sizer doesn't exist anymore, so we came update mode here, before load_current_presets
    SetTopWindow(mainframe);

    plater_->init_notification_manager();

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    if (is_gcode_viewer())
    {
        mainframe->update_layout();
        if (plater_ != nullptr)
            // ensure the selected technology is ptFFF
            plater_->set_printer_technology(ptFFF);
    }
    else
    {
        // Suppress config validation dialogs during initial load.
        // Modal dialogs deadlock behind the wxSTAY_ON_TOP splash screen.
        ConfigManipulation::set_suppress_startup_dialogs(true);
        load_current_presets();
        ConfigManipulation::set_suppress_startup_dialogs(false);
    }

    // Save the active profiles as a "saved into project".
    update_saved_preset_from_current_preset();

    if (plater_ != nullptr)
    {
        // Save the names of active presets and project specific config into ProjectDirtyStateManager.
        plater_->reset_project_dirty_initial_presets();
        // Update Project dirty state, update application title bar.
        plater_->update_project_dirty_from_presets();
    }

#ifdef _WIN32
    // Keep the mainframe invisible during the splash to prevent a white flash.
    // The mainframe is larger than the splash bitmap, so its white edges are visible.
    // Hide it with WS_EX_LAYERED, let it render off-screen, reveal when splash closes.
    HWND mf_hwnd = mainframe->GetHWND();
    if (scrn && mf_hwnd)
    {
        SetWindowLong(mf_hwnd, GWL_EXSTYLE, GetWindowLong(mf_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(mf_hwnd, 0, 0, LWA_ALPHA);
    }
#endif

    mainframe->Show(true);

    // The settings-spec assertion at start-up: the console's --check-settings-spec, plus the
    // agreement of overridable_at with the per-object taxonomy the object list uses today.
    // Mismatches are logged as errors and trip the assert in a debug build.
    {
        std::ostringstream report;
        int failures = check_settings_spec(report);
        for (const bool is_part : {false, true})
        {
            const std::vector<std::string> options = SettingsFactory::get_options(is_part);
            const std::set<std::string> taxonomy(options.begin(), options.end());
            for (const std::string &key : FullPrintConfig().keys())
                if (overridable_at(is_part ? OverrideScope::Part : OverrideScope::Object, key) !=
                    (taxonomy.count(key) != 0))
                {
                    report << "settings spec: overridable_at(" << (is_part ? "part" : "object") << ", " << key
                           << ") disagrees with SettingsFactory::get_options\n";
                    ++failures;
                }
        }
        if (failures > 0)
            BOOST_LOG_TRIVIAL(error) << "settings spec: " << failures << " failure(s) at start-up\n" << report.str();
        assert(failures == 0);
    }

    // PREFLIGHT_DUMP_SIDEBAR=<dir>: write the settings registry of every surface (the three
    // sidebar panels in both layouts and both visibility modes, the three Settings pages) into
    // dir, then exit. The files let two builds of the settings layout be compared.
    if (const char *dump_dir = std::getenv("PREFLIGHT_DUMP_SIDEBAR"); dump_dir != nullptr && *dump_dir != '\0')
    {
        const std::string dir(dump_dir);
        CallAfter(
            [this, dir]()
            {
                boost::filesystem::create_directories(dir);
                if (plater_ != nullptr && std::getenv("PREFLIGHT_DUMP_SIDEBAR_SKIP_SIDEBAR") == nullptr)
                    plater_->sidebar().dump_settings_registry(dir);
                if (std::getenv("PREFLIGHT_DUMP_SIDEBAR_SKIP_TABS") == nullptr)
                    for (Tab *tab : tabs_list)
                    {
                        const char *name = tab->type() == Preset::TYPE_PRINT      ? "print"
                                           : tab->type() == Preset::TYPE_FILAMENT ? "filament"
                                           : tab->type() == Preset::TYPE_PRINTER  ? "printer"
                                                                                  : nullptr;
                        if (name != nullptr)
                            tab->dump_registry(dir + "/tab_" + name + ".txt");
                    }
                // PREFLIGHT_DUMP_SIDEBAR_KEEP leaves the window open after the dump. Otherwise
                // the frame closes from a one-shot timer, after the start-up's own deferred work
                // (the splash timer among it) has run.
                if (std::getenv("PREFLIGHT_DUMP_SIDEBAR_KEEP") == nullptr)
                {
                    auto *close_timer = new wxTimer();
                    close_timer->Bind(wxEVT_TIMER,
                                      [this, close_timer](wxTimerEvent &)
                                      {
                                          close_timer->Stop();
                                          mainframe->Close(true);
                                      });
                    close_timer->StartOnce(2000);
                }
            });
    }

    // The override panel builds its rows once, hidden, shortly after the window is up, so the
    // first click on an Overrides cell shows them instead of building them
    if (plater_ != nullptr)
    {
        auto *prebuild_timer = new wxTimer();
        prebuild_timer->Bind(wxEVT_TIMER,
                             [this, prebuild_timer](wxTimerEvent &)
                             {
                                 prebuild_timer->Stop();
                                 if (plater_ != nullptr)
                                     obj_settings()->prebuild();
                             });
        prebuild_timer->StartOnce(1500);
    }

    // PREFLIGHT_OPEN_OVERRIDES=<open ms>[,<close ms>]: after the first delay, select the first
    // object of the loaded project and open its override panel, so a screenshot of the panel
    // can be taken without a click (the theme and layout checks); after the second delay, when
    // given, the frame closes.
    if (const char *spec = std::getenv("PREFLIGHT_OPEN_OVERRIDES"); spec != nullptr && *spec != '\0')
    {
        int open_ms = 0, close_ms = 0;
        sscanf(spec, "%d,%d", &open_ms, &close_ms);
        auto *open_timer = new wxTimer();
        open_timer->Bind(wxEVT_TIMER,
                         [this, open_timer](wxTimerEvent &)
                         {
                             open_timer->Stop();
                             ObjectList *list = plater_ != nullptr ? obj_list() : nullptr;
                             if (list != nullptr && !list->GetModel()->IsEmpty())
                                 list->open_overrides(list->GetModel()->GetItemById(0));
                         });
        open_timer->StartOnce(std::max(100, open_ms));
        if (close_ms > 0)
        {
            auto *close_timer = new wxTimer();
            close_timer->Bind(wxEVT_TIMER,
                              [this, close_timer](wxTimerEvent &)
                              {
                                  close_timer->Stop();
                                  mainframe->Close(true);
                              });
            close_timer->StartOnce(close_ms);
        }
    }

    if (scrn)
    {
        // Use a one-shot timer to close splash after delay without blocking UI
        // Use wxWeakRef to safely track if splash is still alive - it may be destroyed
        // by user click or wxWidgets timeout before our timer fires
        wxWeakRef<SplashScreen> splash_weak(scrn);
        auto *timer = new wxTimer();
        timer->Bind(wxEVT_TIMER,
                    [splash_weak, timer
#ifdef _WIN32
                     ,
                     mf_hwnd
#endif
        ](wxTimerEvent &)
                    {
                        // Check both wxWeakRef validity AND that window isn't being deleted
                        // to avoid race condition with splash screen's internal timeout
                        if (splash_weak && !splash_weak->IsBeingDeleted())
                            splash_weak->Close();
#ifdef _WIN32
                        // Reveal mainframe now that it's fully rendered with dark theme
                        if (mf_hwnd && IsWindow(mf_hwnd))
                        {
                            SetLayeredWindowAttributes(mf_hwnd, 0, 255, LWA_ALPHA);
                            SetWindowLong(mf_hwnd, GWL_EXSTYLE, GetWindowLong(mf_hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
                        }
#endif
                        timer->Stop();
                        delete timer;
                    });
        timer->StartOnce(1000); // 1 second after main window shows
    }

    obj_list()->set_min_height();

    if (is_editor())
        update_mode(); // update view mode after fix of the object_list size

    show_printer_webview_tab();

#ifdef _WIN32
    mainframe->update_title(); // To ensure taskbar icons is updated.
#endif

#ifdef __APPLE__
    other_instance_message_handler()->bring_instance_forward();
#endif //__APPLE__

    Bind(wxEVT_IDLE,
         [this](wxIdleEvent &event)
         {
             if (!plater_)
                 return;

             this->obj_manipul()->update_if_dirty();

        // On Linux and macOS, GUI_App::init_opengl (normally reached from wxEVT_PAINT and
        // wxEVT_SET_FOCUS) can run after GUI_App::post_init, leaving OpenGL uninitialized, so
        // post_init waits for m_opengl_initialized there.

#if defined(__linux__) || defined(__APPLE__)
             if (!m_post_initialized && m_opengl_initialized)
             {
#else
        if (!m_post_initialized) {
#endif
                 m_post_initialized = true;

#ifdef WIN32
                 this->mainframe->register_win32_callbacks();
#endif
                 this->post_init();
             }

             if (m_post_initialized && app_config->dirty())
                 app_config->save();
         });

    m_initialized = true;

    // If the selected theme failed to load at startup, the palette fell back to Auto; show the
    // warning dialog once the UI is up (deferred so there is a running event loop to host the modal).
    CallAfter([]() { flush_theme_load_error(); });

    if (const std::string &crash_reason = app_config->get("restore_win_position");
        boost::starts_with(crash_reason, "crashed"))
    {
        wxString preferences_item = _L("Restore window position on start");
        InfoDialog dialog(
            nullptr, _L("preFlight started after a crash"),
            format_wxstr(
                _L("preFlight crashed last time when attempting to set window position.\n"
                   "We are sorry for the inconvenience, it unfortunately happens with certain multiple-monitor setups.\n"
                   "More precise reason for the crash: \"%1%\".\n\n"
                   "To avoid this problem, consider disabling \"%2%\" in \"Preferences\". "
                   "Otherwise, the application will most likely crash again next time."),
                "<b>" + from_u8(crash_reason) + "</b>", "<b>" + preferences_item + "</b>"),
            true, wxYES_NO);

        dialog.SetButtonLabel(wxID_YES, format_wxstr(_L("Disable \"%1%\""), preferences_item));
        dialog.SetButtonLabel(wxID_NO, format_wxstr(_L("Leave \"%1%\" enabled"), preferences_item));

        auto answer = dialog.ShowModal();
        if (answer == wxID_YES)
            app_config->set("restore_win_position", "0");
        else if (answer == wxID_NO)
            app_config->set("restore_win_position", "1");
    }

    return true;
}

unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(r * r * .241 + g * g * .691 + b * b * .068));
}

bool GUI_App::dark_mode()
{
    // The active theme's is_dark is the single master for dark/light on
    // all platforms (theme selection overrides OS-follow). Defaults to dark until
    // the theme is loaded at startup.
    return DSKY::active_theme_is_dark();
}

// Global helper for UIColors - allows UIColors.hpp to check theme without including GUI_App.hpp
bool IsDarkMode()
{
    return wxGetApp().dark_mode();
}

const wxColour GUI_App::get_label_default_clr_system()
{
    // preFlight brand orange
    return UIColors::AccentPrimary();
}

const wxColour GUI_App::get_label_default_clr_modified()
{
    // preFlight brand orange
    return UIColors::AccentPrimary();
}

const std::vector<std::string> GUI_App::get_mode_default_palette()
{
    // Simple/Advanced markers follow the theme accent + secondary (orange/blue by default,
    // baked into the Default palettes); Expert stays red.
    const ThemePalette &p = active_palette();
    return {into_u8(p.accent_primary.GetAsString(wxC2S_HTML_SYNTAX)),
            into_u8(p.accent_secondary.GetAsString(wxC2S_HTML_SYNTAX)), "#E74C3C"};
}

void GUI_App::init_ui_colours()
{
    m_color_label_modified = get_label_default_clr_modified();
    m_color_label_sys = get_label_default_clr_system();
    m_mode_palette = get_mode_default_palette();

    // All label/button colors come from the active theme palette via the unified
    // accessors (no OS-system-color fallback, which would break light themes under a dark OS).
    m_color_label_default = UIColors::LabelDefault();
    m_color_highlight_label_default = UIColors::HighlightLabel();
    m_color_highlight_default = UIColors::HighlightBackground();
    m_color_hovered_btn_label = UIColors::HoveredBtnLabel();
    m_color_default_btn_label = UIColors::DefaultBtnLabel();
    m_color_selected_btn_bg = UIColors::SelectedBtnBackground();
    m_color_window_default = UIColors::InputBackground();
}

void GUI_App::update_ui_colours_from_appconfig()
{
    // Label colors are hardcoded - not read from config
    // See get_label_default_clr_modified() and get_label_default_clr_system()

    // load mode markers colors
    if (app_config->has("mode_palette"))
    {
        const auto colors = app_config->get("mode_palette");
        if (!colors.empty())
        {
            m_mode_palette.clear();
            if (!unescape_strings_cstyle(colors, m_mode_palette))
                m_mode_palette = get_mode_default_palette();
        }
    }
}

void GUI_App::update_label_colours()
{
    for (Tab *tab : tabs_list)
        tab->update_label_colours();
}

[[maybe_unused]] static bool is_default(wxWindow *win)
{
    wxTopLevelWindow *tlw = find_toplevel_parent(win);
    if (!tlw)
        return false;

    return win == tlw->GetDefaultItem();
}

void GUI_App::UpdateDarkUI(wxWindow *window, bool highlited /* = false*/, bool just_font /* = false*/)
{
#ifdef _WIN32
    // Hyperlinks: keep the accent colour and skip the generic foreground reset below, which would
    // otherwise repaint the link in the regular label colour when a dialog themes its children.
    if (auto *link = dynamic_cast<wxHyperlinkCtrlBase *>(window))
    {
        tint_hyperlink(link);
        return;
    }
    bool is_focused_button = false;
    bool is_default_button = false;
    if (wxButton *btn = dynamic_cast<wxButton *>(window))
    {
        // Skip background handling for transparent buttons (e.g., mirror buttons)
        bool is_transparent = (btn->GetWindowStyle() & wxTRANSPARENT_WINDOW) != 0;

        if (!(btn->GetWindowStyle() & wxNO_BORDER))
        {
            btn->SetWindowStyle(btn->GetWindowStyle() | wxNO_BORDER);
            highlited = true;
        }
        // button marking - skip for transparent buttons
        if (!dynamic_cast<TopBarItemsCtrl *>(window->GetParent()) && !is_transparent)
        {
            auto mark_button = [this, btn, highlited](const bool mark)
            {
                if (btn->GetLabel().IsEmpty())
                    btn->SetBackgroundColour(mark        ? m_color_selected_btn_bg
                                             : highlited ? m_color_highlight_default
                                                         : m_color_window_default);
                else
                    btn->SetForegroundColour(
                        mark ? m_color_hovered_btn_label
                             : (is_default(btn) ? m_color_default_btn_label : m_color_label_default));
                btn->Refresh();
                btn->Update();
            };

            // hovering
            btn->Bind(wxEVT_ENTER_WINDOW,
                      [mark_button](wxMouseEvent &event)
                      {
                          mark_button(true);
                          event.Skip();
                      });
            btn->Bind(wxEVT_LEAVE_WINDOW,
                      [mark_button, btn](wxMouseEvent &event)
                      {
                          mark_button(btn->HasFocus());
                          event.Skip();
                      });
            // focusing
            btn->Bind(wxEVT_SET_FOCUS,
                      [mark_button](wxFocusEvent &event)
                      {
                          mark_button(true);
                          event.Skip();
                      });
            btn->Bind(wxEVT_KILL_FOCUS,
                      [mark_button](wxFocusEvent &event)
                      {
                          mark_button(false);
                          event.Skip();
                      });

            is_focused_button = btn->HasFocus();
            is_default_button = is_default(btn);
            if (is_focused_button || is_default_button)
                mark_button(is_focused_button);
        }
    }
    else if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(window))
    {
        if (text->GetBorder() != wxBORDER_SIMPLE)
            text->SetWindowStyle(text->GetWindowStyle() | wxBORDER_SIMPLE);
    }
    else if (wxCheckListBox *list = dynamic_cast<wxCheckListBox *>(window))
    {
        list->SetWindowStyle(list->GetWindowStyle() | wxBORDER_SIMPLE);
        list->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
        for (size_t i = 0; i < list->GetCount(); i++)
            if (wxOwnerDrawn *item = list->GetItem(i))
            {
                item->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
                item->SetTextColour(m_color_label_default);
            }
        // Fall through to apply SetDarkExplorerTheme for consistent dark scrollbars
    }
    else if (dynamic_cast<wxListBox *>(window))
        window->SetWindowStyle(window->GetWindowStyle() | wxBORDER_SIMPLE);

    // Skip background color for:
    // - wxStaticBox: FlatStaticBox handles its own theming by reading parent's background
    // - Transparent windows: They should inherit parent's background
    bool is_transparent_window = (window->GetWindowStyle() & wxTRANSPARENT_WINDOW) != 0;
    if (!just_font && !dynamic_cast<wxStaticBox *>(window) && !is_transparent_window)
        window->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
    if (!is_focused_button && !is_default_button)
        window->SetForegroundColour(m_color_label_default);

    // Apply dark theme to scrollable controls (tree, list, text controls, scrolled windows)
    // Guard: window may not be realized yet when called during construction
    HWND hwnd = window->GetHWND();
    if (hwnd &&
        (dynamic_cast<wxScrollHelper *>(window) || dynamic_cast<wxTreeCtrl *>(window) ||
         dynamic_cast<wxListCtrl *>(window) || dynamic_cast<wxTextCtrl *>(window) || dynamic_cast<wxListBox *>(window)))
    {
        NppDarkMode::SetDarkExplorerTheme(hwnd);
    }
#elif defined(__linux__)
    // Linux/GTK: set background and foreground on widgets so they follow the active theme palette.
    // GTK3 buttons need per-widget CSS since SetBackgroundColour is ignored by the CSS engine.
    if (auto *link = dynamic_cast<wxHyperlinkCtrlBase *>(window))
    {
        tint_hyperlink(link);
        return;
    }

    if (wxButton *btn = dynamic_cast<wxButton *>(window))
    {
        bool is_borderless = (btn->GetWindowStyle() & (wxNO_BORDER | wxBU_EXACTFIT)) != 0;
        GtkWidget *gtk_btn = static_cast<GtkWidget *>(btn->GetHandle());
        if (gtk_btn)
        {
            wxColour bg = highlited ? m_color_highlight_default : m_color_window_default;
            wxColour fg = m_color_label_default;
            wxColour hover_bg = m_color_highlight_default;
            wxColour border = UIColors::StaticBoxBorderDark();

            char css[512];
            if (is_borderless)
            {
                // ScalableButton and other borderless buttons: colors only, no padding/border changes
                snprintf(css, sizeof(css),
                         "button { background-image:none; background-color:#%02x%02x%02x;"
                         " color:#%02x%02x%02x; border:none; }"
                         "button:hover { background-color:#%02x%02x%02x; }",
                         bg.Red(), bg.Green(), bg.Blue(), fg.Red(), fg.Green(), fg.Blue(), hover_bg.Red(),
                         hover_bg.Green(), hover_bg.Blue());
            }
            else
            {
                // Standard dialog/settings buttons: full theming
                snprintf(css, sizeof(css),
                         "button { background-image:none; background-color:#%02x%02x%02x;"
                         " color:#%02x%02x%02x; border:1px solid #%02x%02x%02x; border-radius:4px; padding:4px 12px; }"
                         "button:hover { background-color:#%02x%02x%02x; }",
                         bg.Red(), bg.Green(), bg.Blue(), fg.Red(), fg.Green(), fg.Blue(), border.Red(), border.Green(),
                         border.Blue(), hover_bg.Red(), hover_bg.Green(), hover_bg.Blue());
            }

            GtkCssProvider *provider = gtk_css_provider_new();
            gtk_css_provider_load_from_data(provider, css, -1, nullptr);
            gtk_style_context_add_provider(gtk_widget_get_style_context(gtk_btn), GTK_STYLE_PROVIDER(provider),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(provider);
        }
    }

    if (wxCheckBox *cb = dynamic_cast<wxCheckBox *>(window))
    {
        GtkWidget *gtk_cb = static_cast<GtkWidget *>(cb->GetHandle());
        if (gtk_cb)
        {
            wxColour fg = m_color_label_default;
            wxColour border = UIColors::StaticBoxBorderDark();
            char css[512];
            snprintf(css, sizeof(css),
                     "checkbutton, checkbutton label { color:#%02x%02x%02x; }"
                     "checkbutton check { border-color:#%02x%02x%02x; }",
                     fg.Red(), fg.Green(), fg.Blue(), border.Red(), border.Green(), border.Blue());
            GtkCssProvider *provider = gtk_css_provider_new();
            gtk_css_provider_load_from_data(provider, css, -1, nullptr);
            gtk_style_context_add_provider(gtk_widget_get_style_context(gtk_cb), GTK_STYLE_PROVIDER(provider),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(provider);
        }
    }

    bool is_transparent = (window->GetWindowStyle() & wxTRANSPARENT_WINDOW) != 0;
    if (!just_font && !dynamic_cast<wxStaticBox *>(window) && !dynamic_cast<wxButton *>(window) && !is_transparent)
        window->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
    window->SetForegroundColour(m_color_label_default);
#endif
}

void GUI_App::tint_hyperlink(wxHyperlinkCtrlBase *link)
{
    if (!link)
        return;
    // Hyperlinks follow the primary accent instead of the default web-blue.
    link->SetNormalColour(UIColors::AccentPrimary());
    link->SetHoverColour(UIColors::AccentHover());
    link->SetVisitedColour(UIColors::AccentPrimary());
}

// recursive function for scaling fonts for all controls in Window
#if defined(_WIN32) || defined(__linux__)
static void update_dark_children_ui(wxWindow *window, bool just_buttons_update = false)
{
    bool is_btn = dynamic_cast<wxButton *>(window) != nullptr;
    if (!(just_buttons_update && !is_btn))
        wxGetApp().UpdateDarkUI(window, is_btn);

    auto children = window->GetChildren();
    for (auto child : children)
    {
        update_dark_children_ui(child);
    }
}
#endif

// Note: Don't use this function for Dialog contains ScalableButtons
// The size grip (bottom-right corner resize handle) doesn't theme in dark mode.
// Windows can still resize via edge dragging with wxRESIZE_BORDER style.
#ifdef _WIN32
struct SizeGripSearchParams
{
    HWND parentHwnd;
    int parentWidth;
    int parentHeight;
};

static BOOL CALLBACK RemoveSizeGripProc(HWND hwnd, LPARAM lParam)
{
    if (!hwnd || !IsWindow(hwnd))
        return TRUE;

    wchar_t className[64] = {0};
    GetClassNameW(hwnd, className, 64);

    bool shouldRemove = false;

    // Check for ScrollBar with size grip style
    if (wcscmp(className, L"ScrollBar") == 0)
    {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        if (style & SBS_SIZEGRIP)
        {
            shouldRemove = true;
        }
    }

    // Check for msctls_sizebox (another possible size grip class)
    if (wcscmp(className, L"msctls_sizebox") == 0)
    {
        shouldRemove = true;
    }

    // Also check for any small control in bottom-right corner that might be a grip
    if (!shouldRemove && lParam != 0)
    {
        SizeGripSearchParams *params = reinterpret_cast<SizeGripSearchParams *>(lParam);
        RECT rect;
        if (GetWindowRect(hwnd, &rect))
        {
            POINT pt = {rect.left, rect.top};
            ScreenToClient(params->parentHwnd, &pt);
            int ctrlWidth = rect.right - rect.left;
            int ctrlHeight = rect.bottom - rect.top;

            // Size grips are typically small (around 16x16 or 20x20) and in the bottom-right corner
            bool isSmall = (ctrlWidth <= 25 && ctrlHeight <= 25 && ctrlWidth > 5 && ctrlHeight > 5);
            bool isBottomRight = (pt.x > params->parentWidth - 30) && (pt.y > params->parentHeight - 30);

            // Additional check: size grips typically have specific styles
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            bool looksLikeGrip = (style & (SBS_SIZEGRIP | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN)) != 0;

            if (isSmall && isBottomRight && looksLikeGrip)
            {
                shouldRemove = true;
            }
        }
    }

    if (shouldRemove)
    {
        // Try multiple methods to remove the grip
        // Method 1: Hide it
        ShowWindow(hwnd, SW_HIDE);

        // Method 2: Move it off-screen
        SetWindowPos(hwnd, NULL, -100, -100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        // Method 3: Try to destroy it (may fail for system-owned windows)
        DestroyWindow(hwnd);
    }

    return TRUE;
}

// Helper to remove size grips - can be called multiple times safely
static void RemoveSizeGrips(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return;

    // Get parent dimensions for position-based detection
    RECT clientRect;
    if (GetClientRect(hwnd, &clientRect))
    {
        SizeGripSearchParams params;
        params.parentHwnd = hwnd;
        params.parentWidth = clientRect.right;
        params.parentHeight = clientRect.bottom;
        EnumChildWindows(hwnd, RemoveSizeGripProc, reinterpret_cast<LPARAM>(&params));
    }
    else
    {
        // Fallback: search without position info
        EnumChildWindows(hwnd, RemoveSizeGripProc, 0);
    }
}
#endif

void GUI_App::UpdateDlgDarkUI(wxDialog *dlg, bool just_buttons_update /* = false*/)
{
#ifdef _WIN32
    update_dark_children_ui(dlg, just_buttons_update);
    NppDarkMode::AllowDarkModeForWindow(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());

    // Remove size grip controls that don't theme properly in dark mode.
    // The dialog is still resizable via edge dragging (WS_THICKFRAME from wxRESIZE_BORDER).
    RemoveSizeGrips(dlg->GetHWND());

    // Bind to show event to catch any grips created after initial setup.
    // Multiple bindings are safe since RemoveSizeGrips is idempotent.
    dlg->Bind(wxEVT_SHOW,
              [](wxShowEvent &event)
              {
                  event.Skip();
                  if (event.IsShown())
                  {
                      if (wxDialog *d = dynamic_cast<wxDialog *>(event.GetEventObject()))
                      {
                          RemoveSizeGrips(d->GetHWND());
                      }
                  }
              });
#elif defined(__linux__)
    update_dark_children_ui(dlg, just_buttons_update);
#endif
}
void GUI_App::UpdateDVCDarkUI(wxDataViewCtrl *dvc, bool highlited /* = false*/)
{
#ifdef _WIN32
    UpdateDarkUI(dvc, highlited);
    // The upstream wx fork had a custom RefreshHeaderDarkMode() method on wxDataViewCtrl
    // that doesn't exist in upstream wx 3.2.9. We use our own implementation that
    // applies dark theme to the header control and child windows.
    NppDarkMode::SetDarkThemeForDataViewCtrl(dvc->GetHWND());
    if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);
    if (dvc->GetBorder() != wxBORDER_SIMPLE)
        dvc->SetWindowStyle(dvc->GetWindowStyle() | wxBORDER_SIMPLE);
#else
    // On Linux/GTK, wxDataViewCtrl inherits GTK theme colors which don't match
    // preFlight's UIColors palette. Set background and text colors explicitly.
    dvc->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
    dvc->SetForegroundColour(m_color_label_default);
    if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);

    // Style the column header to match preFlight's dark theme via GTK CSS.
    // GTK3 header buttons are children of the GtkTreeView and require screen-level
    // CSS to override their theme styling. Applied once via static guard.
#ifdef __linux__
    {
        static bool s_dvc_css_applied = false;
        if (!s_dvc_css_applied)
        {
            s_dvc_css_applied = true;
            wxColour bg = m_color_window_default;
            wxColour fg = m_color_label_default;
            char css_buf[512];
            snprintf(css_buf, sizeof(css_buf),
                     ".view header button {"
                     "  background-image: none;"
                     "  background-color: #%02x%02x%02x;"
                     "  color: #%02x%02x%02x;"
                     "  border: none;"
                     "  border-bottom: 1px solid #%02x%02x%02x;"
                     "  font-weight: normal;"
                     "}"
                     ".view header button label {"
                     "  color: #%02x%02x%02x;"
                     "  font-weight: normal;"
                     "}",
                     bg.Red(), bg.Green(), bg.Blue(), fg.Red(), fg.Green(), fg.Blue(), bg.Red(), bg.Green(), bg.Blue(),
                     fg.Red(), fg.Green(), fg.Blue());

            GtkCssProvider *provider = gtk_css_provider_new();
            gtk_css_provider_load_from_data(provider, css_buf, -1, nullptr);
            GdkScreen *screen = gdk_screen_get_default();
            if (screen)
                gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(provider);
        }
    }
#endif // __linux__
#endif
}

void GUI_App::UpdateAllStaticTextDarkUI(wxWindow *parent)
{
#if defined(_WIN32) || defined(__linux__)
    if (!parent)
        return;

    // Update parent background
    wxGetApp().UpdateDarkUI(parent);

    // Recursively update all wxStaticText children
    std::function<void(wxWindow *)> update_recursive;
    update_recursive = [this, &update_recursive](wxWindow *window)
    {
        if (!window)
            return;
        if (dynamic_cast<wxStaticText *>(window))
            window->SetForegroundColour(m_color_label_default);
        for (wxWindow *child : window->GetChildren())
            update_recursive(child);
    };
    update_recursive(parent);
#endif
}

void GUI_App::SetWindowVariantForButton(wxButton *btn)
{
#ifdef __APPLE__
    // This is a limit imposed by OSX. The way the native button widget is drawn only allows it to be stretched horizontally,
    // and the vertical size is fixed. (see https://stackoverflow.com/questions/29083891/wxpython-button-size-being-ignored-on-osx)
    // But standard height is possible to change using SetWindowVariant method (see https://docs.wxwidgets.org/3.0/window_8h.html#a879bccd2c987fedf06030a8abcbba8ac)
    if (m_normal_font.GetPointSize() > 15)
    {
        btn->SetWindowVariant(wxWINDOW_VARIANT_LARGE);
        btn->SetFont(m_normal_font);
    }

    // Theme the native button so dialog/standard buttons follow the active theme instead of the system
    // default blue (affirmative) and gray (others). Deferred via CallAfter because the dialog calls
    // SetDefault() AFTER this hook, so is_default() is only reliable once the current call stack unwinds.
    // CallAfter is dropped automatically if the button is destroyed first, so the captured pointer is safe.
    btn->CallAfter(
        [btn]()
        {
            const bool dflt = is_default(btn);
            const wxColour bezel = dflt ? UIColors::AccentPrimary() : UIColors::SelectedBtnBackground();
            const wxColour title = dflt ? UIColors::AccentText() : UIColors::LabelDefault();
            mac_set_button_bezel_color(btn->GetHandle(), bezel.Red(), bezel.Green(), bezel.Blue());
            mac_set_button_title_color(btn->GetHandle(), title.Red(), title.Green(), title.Blue());
        });
#endif
}

int GUI_App::get_max_font_pt_size()
{
    const unsigned disp_count = wxDisplay::GetCount();
    for (unsigned i = 0; i < disp_count; i++)
    {
        const wxRect display_rect = wxDisplay(i).GetGeometry();
        if (display_rect.width >= 2560 && display_rect.height >= 1440)
            return 20;
    }
    return 15;
}

void GUI_App::init_fonts()
{
    m_small_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    m_bold_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
    m_normal_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/

    // wxSYS_OEM_FIXED_FONT and wxSYS_ANSI_FIXED_FONT use the same as
    // DEFAULT in wxGtk. Use the TELETYPE family as a work-around
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
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
    m_normal_font = main_frame->normal_font();
    m_small_font = m_normal_font;
    m_bold_font = main_frame->normal_font().Bold();
    m_link_font = m_bold_font.Underlined();
    m_em_unit = main_frame->em_unit();
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
}

void GUI_App::set_label_clr_modified(const wxColour &clr)
{
    if (m_color_label_modified == clr)
        return;
    m_color_label_modified = clr;
    const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
    app_config->set("label_clr_modified", str);
}

void GUI_App::set_label_clr_sys(const wxColour &clr)
{
    if (m_color_label_sys == clr)
        return;
    m_color_label_sys = clr;
    const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
    app_config->set("label_clr_sys", str);
}

const std::string GUI_App::get_html_bg_color(wxWindow *html_parent)
{
    wxColour bgr_clr = html_parent->GetBackgroundColour();
#ifdef __APPLE__
    // On macOS 10.13 and older the background color returned by wxWidgets
    // is wrong, which causes incorrect background colors. wxSYS_COLOUR_WINDOW
    // may not match the window background exactly, but it seems to never end up
    // as black on black.

    if (wxPlatformInfo::Get().GetOSMajorVersion() == 10 && wxPlatformInfo::Get().GetOSMinorVersion() < 14)
        bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
#endif

    return encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));
}

const std::string &GUI_App::get_mode_btn_color(int mode_id)
{
    assert(0 <= mode_id && size_t(mode_id) < m_mode_palette.size());
    return m_mode_palette[mode_id];
}

std::vector<wxColour> GUI_App::get_mode_palette()
{
    return {wxColor(m_mode_palette[0]), wxColor(m_mode_palette[1]), wxColor(m_mode_palette[2])};
}

void GUI_App::set_mode_palette(const std::vector<wxColour> &palette)
{
    bool save = false;

    for (size_t mode = 0; mode < palette.size(); ++mode)
    {
        const wxColour &clr = palette[mode];
        std::string color_str = clr == wxTransparentColour ? std::string("")
                                                           : encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
        if (m_mode_palette[mode] != color_str)
        {
            m_mode_palette[mode] = color_str;
            save = true;
        }
    }

    if (save)
    {
        mainframe->update_mode_markers();
        app_config->set("mode_palette", escape_strings_cstyle(m_mode_palette));
    }
}

bool GUI_App::suppress_round_corners() const
{
    return true; // app_config->get("suppress_round_corners") == "1";
}

wxSize GUI_App::get_min_size(wxWindow *display_win) const
{
    wxSize min_size(76 * m_em_unit, 49 * m_em_unit);

    const wxDisplay display = wxDisplay(display_win);
    wxRect display_rect = display.GetGeometry();
    display_rect.width *= 0.75;
    display_rect.height *= 0.75;

    if (min_size.x > display_rect.GetWidth())
        min_size.x = display_rect.GetWidth();
    if (min_size.y > display_rect.GetHeight())
        min_size.y = display_rect.GetHeight();

    return min_size;
}

float GUI_App::toolbar_icon_scale(bool &is_custom) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    const std::string &use_val = app_config->get("use_custom_toolbar_size");
    const std::string &val = app_config->get("custom_toolbar_size");
    const std::string &auto_val = app_config->get("auto_toolbar_size");

    if (val.empty() || auto_val.empty() || use_val.empty())
        return icon_sc;

    is_custom = app_config->get_bool("use_custom_toolbar_size");

    int int_val = use_val == "0" ? 100 : atoi(val.c_str());
    // correct value in respect to auto_toolbar_size
    int_val = std::min(atoi(auto_val.c_str()), int_val);

    return 0.01f * int_val;
}

void GUI_App::set_auto_toolbar_icon_scale(float scale) const
{
    std::string val = std::to_string(int(std::lround(scale * 100)));
    app_config->set("auto_toolbar_size", val);
}

// check user printer_presets for the containing information about "Print Host upload"
void GUI_App::check_printer_presets()
{
    std::vector<std::string> preset_names = PhysicalPrinter::presets_with_print_host_information(
        preset_bundle->printers);
    if (preset_names.empty())
        return;

    wxString msg_text = _L("You have the following presets with saved options for \"Print Host upload\"") + ":";
    for (const std::string &preset_name : preset_names)
        msg_text += "\n    \"" + from_u8(preset_name) + "\",";
    msg_text.RemoveLast();
    msg_text += "\n\n" +
                _L("But since this version of preFlight we don't show this information in Printer Settings anymore.\n"
                   "Settings will be available in physical printers settings.") +
                "\n\n" +
                _L("By default new Printer devices will be named as \"Printer N\" during its creation.\n"
                   "Note: This name can be changed later from the physical printers settings");

    //wxMessageDialog(nullptr, msg_text, _L("Information"), wxOK | wxICON_INFORMATION).ShowModal();
    MessageDialog(nullptr, msg_text, _L("Information"), wxOK | wxICON_INFORMATION).ShowModal();

    preset_bundle->physical_printers.load_printers_from_presets(preset_bundle->printers);
}

void GUI_App::recreate_GUI(const wxString &msg_name)
{
#if defined(__APPLE__) || defined(__linux__)
    // In-process GUI recreation leaves stale toolkit state (Cocoa NSView teardown
    // crashes on macOS; GTK CSS theme caching keeps old colors on Linux). Restart
    // the entire process for a guaranteed clean slate.
    app_config->save();
    auto exe_path = boost::dll::program_location();
    BOOST_LOG_TRIVIAL(info) << "Restarting application: " << exe_path;
    const std::string exe = exe_path.string();
    const char *argv[] = {exe.c_str(), nullptr};
    execv(argv[0], const_cast<char *const *>(argv));
    // execv only returns on failure
    BOOST_LOG_TRIVIAL(error) << "execv failed, falling through to in-process recreation";
#endif

    m_is_recreating_gui = true;
    m_legacy_prepare_layout = app_config->get_bool("legacy_prepare_layout");

    // Re-resolve the active theme so a theme change applies on GUI recreation
    // (on_init_inner is not re-run in the in-process recreate path used on Windows/Linux).
    load_active_theme();
    init_ui_colours();
    // Refresh ImGui style + icon atlas so the 3D-canvas overlays (gcode legend, toolbars) pick up the
    // new theme; the in-process recreate path does not re-run ImGui init.
    if (m_imgui)
    {
        m_imgui->refresh_style();
        m_imgui->invalidate_font();
    }
#ifdef _MSW_DARK_MODE
    NppDarkMode::SetDarkMode(active_theme_is_dark());
#endif
#ifdef __linux__
    if (GtkSettings *gtk_settings = gtk_settings_get_default())
        g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", active_theme_is_dark() ? TRUE : FALSE, NULL);
#endif

    mainframe->shutdown();

    wxProgressDialog dlg(msg_name, msg_name, 100, nullptr, wxPD_AUTO_HIDE);
    dlg.Pulse();
    dlg.Update(10, _L("Recreating") + dots);

    MainFrame *old_main_frame = mainframe;
    mainframe = new MainFrame(get_app_font_pt_size(app_config));
    if (is_editor())
        // hide settings tabs after first Layout
        mainframe->select_tab(size_t(0));
    // Propagate model objects to object list.
    sidebar().obj_list()->init_objects();
    SetTopWindow(mainframe);

    dlg.Update(30, _L("Recreating") + dots);
    old_main_frame->Destroy();

    dlg.Update(80, _L("Loading of current presets") + dots);
    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));
    // Suppress config validation dialogs during GUI recreation (same as initial load)
    ConfigManipulation::set_suppress_startup_dialogs(true);
    load_current_presets();
    ConfigManipulation::set_suppress_startup_dialogs(false);
    mainframe->Show(true);

    dlg.Update(90, _L("Loading of a mode view") + dots);

    obj_list()->set_min_height();
    update_mode();

    m_is_recreating_gui = false;
}

void GUI_App::system_info()
{
    SysInfoDialog dlg;
    dlg.ShowModal();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
}

// static method accepting a wxWindow object as first parameter
bool GUI_App::catch_error(std::function<void()> cb,
                          //                       wxMessageDialog* message_dialog,
                          const std::string &err /*= ""*/)
{
    if (!err.empty())
    {
        if (cb)
            cb();
        //         if (message_dialog)
        //             message_dialog->(err, "Error", wxOK | wxICON_ERROR);
        show_error(/*this*/ nullptr, err);
        return true;
    }
    return false;
}

// static method accepting a wxWindow object as first parameter
void fatal_error(wxWindow *parent)
{
    show_error(parent, "");
}

#ifdef _WIN32

#ifdef _MSW_DARK_MODE
static void update_scrolls(wxWindow *window)
{
    wxWindowList::compatibility_iterator node = window->GetChildren().GetFirst();
    while (node)
    {
        wxWindow *win = node->GetData();
        if (dynamic_cast<wxScrollHelper *>(win) || dynamic_cast<wxTreeCtrl *>(win) || dynamic_cast<wxTextCtrl *>(win))
            NppDarkMode::SetDarkExplorerTheme(win->GetHWND());

        update_scrolls(win);
        node = node->GetNext();
    }
}
#endif //_MSW_DARK_MODE

void GUI_App::force_colors_update()
{
#ifdef _MSW_DARK_MODE
    NppDarkMode::SetDarkMode(active_theme_is_dark());
    // The upstream fork made wxToolTip::GetToolTipCtrl() public, but it's private in stock wx.
    // Tooltip theming is a minor issue - skip it.
    NppDarkMode::SetDarkTitleBar(mainframe->GetHWND());
    NppDarkMode::SetDarkTitleBar(mainframe->m_settings_dialog.GetHWND());
#endif //_MSW_DARK_MODE
    m_force_colors_update = true;
}
#endif //_WIN32

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings()
{
    update_label_colours();
#ifdef _WIN32
    // Upadte UI colors before Update UI from settings
    if (m_force_colors_update)
    {
        m_force_colors_update = false;
        // wxWidgets logs "Couldn't add an image to the image list" during menu bitmap setup
        // when menu items don't have icons. This is cosmetic only and clutters the log.
        wxLogNull logSuppressor;
        mainframe->force_color_changed();
        mainframe->diff_dialog.force_color_changed();
        if (mainframe->preferences_dialog)
            mainframe->preferences_dialog->force_color_changed();
        mainframe->printhost_queue_dlg()->force_color_changed();
        // Rebuild ImGui font atlas with theme-appropriate icon colors
        imgui()->invalidate_font();
#ifdef _MSW_DARK_MODE
        update_scrolls(mainframe);
        if (mainframe->is_dlg_layout())
        {
            // update for tabs bar
            UpdateDarkUI(&mainframe->m_settings_dialog);
            mainframe->m_settings_dialog.Fit();
            mainframe->m_settings_dialog.Refresh();
            // update scrollbars
            update_scrolls(&mainframe->m_settings_dialog);
        }
#endif //_MSW_DARK_MODE
    }
#endif
    mainframe->update_ui_from_settings();

    // Apply tabbed sidebar preference change immediately
    if (plater())
        plater()->sidebar().SetTabbedMode(app_config->get_bool("use_tabbed_sidebar"));

    // If the just-applied theme failed to load, surface the warning dialog (deferred to the loop).
    CallAfter([]() { flush_theme_load_error(); });
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW,
                 [=](wxCloseEvent &event)
                 {
                     window_pos_save(window, name);
                     event.Skip();
                 });

    window_pos_restore(window, name, default_maximized);

    on_window_geometry(window, [=]() { window_pos_sanitize(window); });
}

void GUI_App::load_project(wxWindow *parent, wxString &input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(), _L("Choose one file (3MF/AMF):"), app_config->get_last_dir(),
                        "", file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (ShowFileDialogModal(dialog) == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString &input_files) const
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(), _L("Choose one or more files (STL/3MF/STEP/OBJ/AMF/SVG):"),
                        from_u8(app_config->get_last_dir()), "", file_wildcards(FT_MODEL),
                        wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (ShowFileDialogModal(dialog) == wxID_OK)
        dialog.GetPaths(input_files);
}

void GUI_App::import_zip(wxWindow *parent, wxString &input_file) const
{
    wxFileDialog dialog(parent ? parent : GetTopWindow(), _L("Choose ZIP file") + ":",
                        from_u8(app_config->get_last_dir()), "", file_wildcards(FT_ZIP),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (ShowFileDialogModal(dialog) == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::load_gcode(wxWindow *parent, wxString &input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(), _L("Choose one file (GCODE/GCO/G/BGCODE/BGC/NGC):"),
                        app_config->get_last_dir(), "", file_wildcards(FT_GCODE), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (ShowFileDialogModal(dialog) == wxID_OK)
        input_file = dialog.GetPath();
}

bool GUI_App::switch_language()
{
    if (select_language())
    {
        recreate_GUI(_L("Changing of an application language") + dots);
        return true;
    }
    else
    {
        return false;
    }
}

#ifdef __linux__
static const wxLanguageInfo *linux_get_existing_locale_language(const wxLanguageInfo *language,
                                                                const wxLanguageInfo *system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = into_u8(language->CanonicalName.BeforeFirst('_'));

    // Call locale -a so we can parse the output to get the list of available locales
    // We expect lines such as "en_US.utf8". Pick ones starting with the language code
    // we are switching to. Lines with different formatting will be removed later.
    FILE *fp = popen("locale -a", "r");
    if (fp != NULL)
    {
        while (fgets(path, max_len, fp) != NULL)
        {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    // locales now contain all candidates for this language.
    // Sort them so ones containing anything about UTF-8 are at the end.
    std::sort(locales.begin(), locales.end(),
              [](const std::string &a, const std::string &b)
              {
                  auto has_utf8 = [](const std::string &s)
                  {
                      auto S = boost::to_upper_copy(s);
                      return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
                  };
                  return !has_utf8(a) && has_utf8(b);
              });

    // Remove the suffix behind a dot, if there is one.
    for (std::string &s : locales)
        s = s.substr(0, s.find("."));

    // We just hope that dear Linux "locale -a" returns country codes
    // in ISO 3166-1 alpha-2 code (two letter) format.
    // https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
    // To be sure, remove anything not looking as expected
    // (any number of lowercase letters, underscore, two uppercase letters).
    locales.erase(std::remove_if(locales.begin(), locales.end(), [](const std::string &s)
                                 { return !std::regex_match(s, std::regex("^[a-z]+_[A-Z]{2}$")); }),
                  locales.end());

    if (system_language)
    {
        // Is there a candidate matching a country code of a system language? Move it to the end,
        // while maintaining the order of matches, so that the best match ends up at the very end.
        std::string system_country = "_" + into_u8(system_language->CanonicalName.AfterFirst('_')).substr(0, 2);
        int cnt = locales.size();
        for (int i = 0; i < cnt; ++i)
            if (locales[i].find(system_country) != std::string::npos)
            {
                locales.emplace_back(std::move(locales[i]));
                locales[i].clear();
            }
    }

    // Now try them one by one.
    for (auto it = locales.rbegin(); it != locales.rend(); ++it)
        if (!it->empty())
        {
            const std::string &locale = *it;
            const wxLanguageInfo *lang = wxLocale::FindLanguageInfo(from_u8(locale));
            if (wxLocale::IsAvailable(lang->Language))
                return lang;
        }
    return language;
}
#endif

int GUI_App::GetSingleChoiceIndex(const wxString &message, const wxString &caption, const wxArrayString &choices,
                                  int initialSelection)
{
#ifdef _WIN32
    wxSingleChoiceDialog dialog(nullptr, message, caption, choices);
    wxGetApp().UpdateDlgDarkUI(&dialog);
    auto children = dialog.GetChildren();
    for (auto child : children)
        child->SetFont(normal_font());

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
#else
    return wxGetSingleChoiceIndex(message, caption, choices, initialSelection);
#endif
}

// select language from the list of installed languages
bool GUI_App::select_language()
{
    wxArrayString translations = wxTranslations::Get()->GetAvailableTranslations(PREFLIGHT_APP_KEY);
    std::vector<const wxLanguageInfo *> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++i)
    {
        const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);
        if (langinfo != nullptr)
            language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
    std::sort(language_infos.begin(), language_infos.end(),
              [](const wxLanguageInfo *l, const wxLanguageInfo *r) { return l->Description < r->Description; });

    wxArrayString names;
    names.Alloc(language_infos.size());

    // Some valid language should be selected since the application start up.
    const wxLanguage current_language = wxLanguage(m_wxLocale->GetLanguage());
    int init_selection = -1;
    int init_selection_alt = -1;
    int init_selection_default = -1;
    for (size_t i = 0; i < language_infos.size(); ++i)
    {
        if (wxLanguage(language_infos[i]->Language) == current_language)
            // The dictionary matches the active language and country.
            init_selection = i;
        else if ((language_infos[i]->CanonicalName.BeforeFirst('_') ==
                  m_wxLocale->GetCanonicalName().BeforeFirst('_')) ||
                 // if the active language is Slovak, mark the Czech language as active.
                 (language_infos[i]->CanonicalName.BeforeFirst('_') == "cs" &&
                  m_wxLocale->GetCanonicalName().BeforeFirst('_') == "sk"))
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
    if (index != -1 && (init_selection == -1 || init_selection != index))
    {
        const wxLanguageInfo *new_language_info = language_infos[index];
        if (this->load_language(new_language_info->CanonicalName, false))
        {
            // Save language at application config.
            // Which language to save as the selected dictionary language?
            // 1) Hopefully the language set to wxTranslations by this->load_language(), but that API is weird and we don't want to rely on its
            //    stability in the future:
            //    wxTranslations::Get()->GetBestTranslation(PREFLIGHT_APP_KEY, wxLANGUAGE_ENGLISH);
            // 2) Current locale language may not match the dictionary name:
            //    m_wxLocale->GetCanonicalName()
            // 3) new_language_info->CanonicalName is a safe bet. It points to a valid dictionary name.
            app_config->set("translation_language", new_language_info->CanonicalName.ToUTF8().data());
            return true;
        }
    }

    return false;
}

// Load gettext translation files and activate them at the start of the application,
// based on the "translation_language" key stored in the application config.
bool GUI_App::load_language(wxString language, bool initial)
{
    if (initial)
    {
        // There is a static list of lookup path prefixes in wxWidgets. Add ours.
        wxFileTranslationsLoader::AddCatalogLookupPathPrefix(from_u8(localization_dir()));
        // Get the active language from preFlight.ini, or empty string if the key does not exist.
        language = app_config->get("translation_language");
        if (!language.empty())
            BOOST_LOG_TRIVIAL(trace) << boost::format("translation_language provided by preFlight.ini: %1%") % language;

        // Get the system language.
        {
            const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
            if (lang_system != wxLANGUAGE_UNKNOWN)
            {
                m_language_info_system = wxLocale::GetLanguageInfo(lang_system);
                BOOST_LOG_TRIVIAL(trace) << boost::format("System language detected (user locales and such): %1%") %
                                                m_language_info_system->CanonicalName.ToUTF8().data();
            }
        }
        {
            // Allocating a temporary locale will switch the default wxTranslations to its internal wxTranslations instance.
            wxLocale temp_locale;
#ifdef __WXOSX__
            // Start from English: on macOS wxLANGUAGE_DEFAULT can resolve to a locale that does not exist
            // (for example "en_CZ"), which raises a warning.
            temp_locale.Init(wxLANGUAGE_ENGLISH);
#else
            temp_locale.Init();
#endif // __WXOSX__
            // Set the current translation's language to default, otherwise GetBestTranslation() may not work (see the wxWidgets source code).
            wxTranslations::Get()->SetLanguage(wxLANGUAGE_DEFAULT);
            // Let the wxFileTranslationsLoader enumerate all translation dictionaries for preFlight
            // and try to match them with the system specific "preferred languages".
            // There seems to be a support for that on Windows and OSX, while on Linuxes the code just returns wxLocale::GetSystemLanguage().
            // The last parameter gets added to the list of detected dictionaries. This is a workaround
            // for not having the English dictionary. Let's hope wxWidgets of various versions process this call the same way.
            wxString best_language = wxTranslations::Get()->GetBestTranslation(PREFLIGHT_APP_KEY, wxLANGUAGE_ENGLISH);
            if (!best_language.IsEmpty())
            {
                m_language_info_best = wxLocale::FindLanguageInfo(best_language);
                BOOST_LOG_TRIVIAL(trace)
                    << boost::format("Best translation language detected (may be different from user locales): %1%") %
                           m_language_info_best->CanonicalName.ToUTF8().data();
            }
#ifdef __linux__
            wxString lc_all;
            if (wxGetEnv("LC_ALL", &lc_all) && !lc_all.IsEmpty())
            {
                // Best language returned by wxWidgets on Linux apparently does not respect LC_ALL.
                // Disregard the "best" suggestion in case LC_ALL is provided.
                m_language_info_best = nullptr;
            }
#endif
        }
    }

    const wxLanguageInfo *language_info = language.empty() ? nullptr : wxLocale::FindLanguageInfo(language);
    if (!language.empty() && (language_info == nullptr || language_info->CanonicalName.empty()))
    {
        // Fix for wxWidgets issue, where the FindLanguageInfo() returns locales with undefined ANSII code (wxLANGUAGE_KONKANI or wxLANGUAGE_MANIPURI).
        language_info = nullptr;
        BOOST_LOG_TRIVIAL(error) << boost::format("Language code \"%1%\" is not supported") % language.ToUTF8().data();
    }

    if (language_info != nullptr && language_info->LayoutDirection == wxLayout_RightToLeft)
    {
        BOOST_LOG_TRIVIAL(trace)
            << boost::format(
                   "The following language code requires right to left layout, which is not supported by preFlight: %1%") %
                   language_info->CanonicalName.ToUTF8().data();
        language_info = nullptr;
    }

    if (language_info == nullptr)
    {
        // preFlight does not support the Right to Left languages yet.
        if (m_language_info_system != nullptr && m_language_info_system->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_system;
        if (m_language_info_best != nullptr && m_language_info_best->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_best;
        if (language_info == nullptr)
            language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US);
    }

    BOOST_LOG_TRIVIAL(trace) << boost::format("Switching wxLocales to %1%") %
                                    language_info->CanonicalName.ToUTF8().data();

    // Alternate language code.
    wxLanguage language_dict = wxLanguage(language_info->Language);
    if (language_info->CanonicalName.BeforeFirst('_') == "sk")
    {
        // Slovaks understand Czech well. Give them the Czech translation.
        language_dict = wxLANGUAGE_CZECH;
        BOOST_LOG_TRIVIAL(trace) << "Using Czech dictionaries for Slovak language";
    }

    // Select language for locales. This language may be different from the language of the dictionary.
    if (language_info == m_language_info_best || language_info == m_language_info_system)
    {
        // The current language matches user's default profile exactly. That's great.
    }
    else if (m_language_info_best != nullptr &&
             language_info->CanonicalName.BeforeFirst('_') == m_language_info_best->CanonicalName.BeforeFirst('_'))
    {
        // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
        // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
        language_info = m_language_info_best;
    }
    else if (m_language_info_system != nullptr &&
             language_info->CanonicalName.BeforeFirst('_') == m_language_info_system->CanonicalName.BeforeFirst('_'))
        language_info = m_language_info_system;

#ifdef __linux__
    // If we can't find this locale , try to use different one for the language
    // instead of just reporting that it is impossible to switch.
    if (!wxLocale::IsAvailable(language_info->Language))
    {
        std::string original_lang = into_u8(language_info->CanonicalName);
        language_info = linux_get_existing_locale_language(language_info, m_language_info_system);
        BOOST_LOG_TRIVIAL(trace) << boost::format(
                                        "Can't switch language to %1% (missing locales). Using %2% instead.") %
                                        original_lang % language_info->CanonicalName.ToUTF8().data();
    }
#endif

    if (!wxLocale::IsAvailable(language_info->Language))
    {
        // Loading the language dictionary failed.
        wxString message = "Switching preFlight to language " + language_info->CanonicalName + " failed.";
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message +=
            "\nYou may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n";
#endif
        if (initial)
            message + "\n\nApplication will close.";
        wxMessageBox(message, "preFlight - Switching language failed", wxOK | wxICON_ERROR);
        if (initial)
            std::exit(EXIT_FAILURE);
        else
            return false;
    }

    // Release the old locales, create new locales.
    // The old locale is released, not deleted: wxWidgets misbehaves if the active locale is destroyed.
    m_wxLocale.release();
    m_wxLocale = Luminary::make_unique<wxLocale>();
    m_wxLocale->Init(language_info->Language);
    // Override language at the active wxTranslations class (which is stored in the active m_wxLocale)
    // to load possibly different dictionary, for example, load Czech dictionary for Slovak language.
    wxTranslations::Get()->SetLanguage(language_dict);
    m_wxLocale->AddCatalog(PREFLIGHT_APP_KEY);
    m_imgui->set_language(into_u8(language_info->CanonicalName));
    Preset::update_suffix_modified(format(" (%1%)", _L("modified")));
    return true;
}

Tab *GUI_App::get_tab(Preset::Type type)
{
    for (Tab *tab : tabs_list)
        if (tab->type() == type)
            return tab->completed() ? tab : nullptr; // To avoid actions with no-completed Tab
    return nullptr;
}

ConfigOptionMode GUI_App::get_mode()
{
    return comExpert;
    // Original code:
    // if (!app_config->has("view_mode"))
    //     return comSimple;
    //
    // const auto mode = app_config->get("view_mode");
    // return mode == "expert" ? comExpert :
    //        mode == "simple" ? comSimple : comAdvanced;
}

bool GUI_App::save_mode(const /*ConfigOptionMode*/ int mode)
{
    const std::string mode_str = mode == comExpert ? "expert" : mode == comSimple ? "simple" : "advanced";

    auto can_switch_to_simple = [](Model &model)
    {
        for (const ModelObject *model_object : model.objects)
            if (model_object->volumes.size() > 1)
            {
                for (size_t i = 1; i < model_object->volumes.size(); ++i)
                    if (!model_object->volumes[i]->is_support_modifier())
                        return false;
            }
        return true;
    };

    if (mode == comSimple && !can_switch_to_simple(model()))
    {
        show_info(nullptr,
                  _L("Simple mode supports manipulation with single-part object(s)\n"
                     "or object(s) with support modifiers only.") +
                      "\n\n" + _L("Please check your object list before mode changing."),
                  _L("Change application mode"));
        return false;
    }
    app_config->set("view_mode", mode_str);
    update_mode();
    return true;
}

// Update view mode according to selected menu
void GUI_App::update_mode()
{
    if (is_gcode_viewer())
        return;

    sidebar().update_mode();

    mainframe->m_tmp_top_bar->UpdateMode();
    mainframe->m_tabpanel->UpdateMode();

    for (auto tab : tabs_list)
        tab->update_mode();

    plater()->update_menus();
    plater()->canvas3D()->update_gizmos_on_off_state();
}

wxMenu *GUI_App::get_config_menu(MainFrame *main_frame)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt));

    const wxString config_wizard_name = _(ConfigWizard::name(true));
    const wxString config_wizard_tooltip = from_u8((boost::format(_u8L("Run %s")) % config_wizard_name).str());
    // Cmd+, is standard on OS X - what about other operating systems?
    if (is_editor())
    {
        local_menu->Append(config_id_base + ConfigMenuWizard, config_wizard_name + dots, config_wizard_tooltip);
        local_menu->Append(config_id_base + ConfigMenuSnapshots, _L("&Configuration Snapshots") + dots,
                           _L("Inspect / activate configuration snapshots"));
        local_menu->Append(config_id_base + ConfigMenuTakeSnapshot, _L("Take Configuration &Snapshot"),
                           _L("Capture a configuration snapshot"));
        // local_menu->Append(config_id_base + ConfigMenuUpdateConf, _L("Check for Configuration Updates"), _L("Check for configuration updates"));
        local_menu->Append(config_id_base + ConfigMenuUpdateApp, _L("Check for Application Updates"),
                           _L("Check for new version of application"));
#if defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
        //if (DesktopIntegrationDialog::integration_possible())
        local_menu->Append(config_id_base + ConfigMenuDesktopIntegration, _L("Desktop Integration"),
                           _L("Desktop Integration"));
#endif //(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
        local_menu->AppendSeparator();
    }
#ifdef __APPLE__
    local_menu->Append(config_id_base + ConfigMenuPreferences, _L("&Preferences") + dots + "\tCtrl+,",
                       _L("Application preferences"));
#else
    append_menu_item(
        local_menu, config_id_base + ConfigMenuPreferences, _L("&Preferences") + "\tCtrl+P",
        _L("Application preferences"), [](wxCommandEvent &) { wxGetApp().open_preferences(); }, "", nullptr,
        []() { return true; }, main_frame);
#endif

    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuLanguage, _L("&Language"));
    if (is_editor())
    {
        local_menu->AppendSeparator();
    }
    // Wi-Fi Configuration File menu item removed

    local_menu->Bind(
        wxEVT_MENU,
        [this, config_id_base](wxEvent &event)
        {
            switch (event.GetId() - config_id_base)
            {
            case ConfigMenuWizard:
                run_wizard(ConfigWizard::RR_USER);
                break;
            // case ConfigMenuUpdateConf:
            //     check_updates(true);
            //     break;
            case ConfigMenuUpdateApp:
                app_version_check(true);
                break;
#ifdef __linux__
            case ConfigMenuDesktopIntegration:
                show_desktop_integration_dialog();
                break;
#endif
            case ConfigMenuTakeSnapshot:
                // Take a configuration snapshot.
                if (wxString action_name = _L("Taking a configuration snapshot"); check_and_save_current_preset_changes(
                        action_name,
                        _L("Some presets are modified and the unsaved changes will not be captured by the configuration snapshot."),
                        false, true))
                {
                    wxTextEntryDialog dlg(nullptr, action_name, _L("Snapshot name"));
                    UpdateDlgDarkUI(&dlg);

                    // set current normal font for dialog children,
                    // because of just dlg.SetFont(normal_font()) has no result;
                    for (auto child : dlg.GetChildren())
                        child->SetFont(normal_font());

                    if (dlg.ShowModal() == wxID_OK)
                        if (const Config::Snapshot *snapshot = Config::take_config_snapshot_report_error(
                                *app_config, Config::Snapshot::SNAPSHOT_USER, dlg.GetValue().ToUTF8().data());
                            snapshot != nullptr)
                            app_config->set("on_snapshot", snapshot->id);
                }
                break;
            case ConfigMenuSnapshots:
                if (check_and_save_current_preset_changes(_L("Loading a configuration snapshot"), "", false))
                {
                    std::string on_snapshot;
                    if (Config::SnapshotDB::singleton().is_on_snapshot(*app_config))
                        on_snapshot = app_config->get("on_snapshot");
                    ConfigSnapshotDialog dlg(DSKY::Config::SnapshotDB::singleton(), on_snapshot);
                    dlg.ShowModal();
                    if (!dlg.snapshot_to_activate().empty())
                    {
                        if (!Config::SnapshotDB::singleton().is_on_snapshot(*app_config) &&
                            !Config::take_config_snapshot_cancel_on_error(
                                *app_config, Config::Snapshot::SNAPSHOT_BEFORE_ROLLBACK, "",
                                DSKY::format(_L("Continue to activate a configuration snapshot %1%?"),
                                             dlg.snapshot_to_activate())))
                            break;
                        try
                        {
                            app_config->set("on_snapshot",
                                            Config::SnapshotDB::singleton()
                                                .restore_snapshot(dlg.snapshot_to_activate(), *app_config)
                                                .id);
                            // Enable substitutions, log both user and system substitutions. There should not be any substitutions performed when loading system
                            // presets because compatibility of profiles shall be verified using the min_preflight_version keys in config index, but users
                            // are known to be creative and mess with the config files in various ways.
                            if (PresetsConfigSubstitutions all_substitutions = preset_bundle->load_presets(
                                    *app_config, ForwardCompatibilitySubstitutionRule::Enable);
                                !all_substitutions.empty())
                                show_substitutions_info(all_substitutions);

                            // Load the currently selected preset into the GUI, update the preset selection box.
                            load_current_presets();
                        }
                        catch (std::exception &ex)
                        {
                            DSKY::show_error(nullptr, _L("Failed to activate configuration snapshot.") + "\n" +
                                                          into_u8(ex.what()));
                        }
                    }
                }
                break;
            case ConfigMenuPreferences:
            {
                open_preferences();
                break;
            }
            case ConfigMenuLanguage:
            {
                /* Before change application language, let's check unsaved changes on 3D-Scene
             * and draw user's attention to the application restarting after a language change
             */
                {
                    // the dialog needs to be destroyed before the call to switch_language()
                    // or sometimes the application crashes into wxDialogBase() destructor
                    // so we put it into an inner scope
                    wxString title = is_editor() ? wxString(PREFLIGHT_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
                    title += " - " + _L("Language selection");
                    //wxMessageDialog dialog(nullptr,
                    MessageDialog dialog(nullptr,
                                         _L("Switching the language will trigger application restart.\n"
                                            "You will lose content of the plater.") +
                                             "\n\n" + _L("Do you want to proceed?"),
                                         title, wxICON_QUESTION | wxOK | wxCANCEL);
                    if (dialog.ShowModal() == wxID_CANCEL)
                        return;
                }

                switch_language();
                break;
            }
            default:
                break;
            }
        });

    return local_menu;
}

void GUI_App::open_preferences(const std::string &highlight_option /*= std::string()*/,
                               const std::string &tab_name /*= std::string()*/)
{
    // Creating during MainFrame construction causes GCodeViewer hang because
    // window handles may not be fully valid yet
    if (!mainframe->preferences_dialog)
        mainframe->preferences_dialog = new PreferencesDialog(mainframe);

    mainframe->preferences_dialog->show(highlight_option, tab_name);

    // Capture dialog state before recreate_GUI() potentially destroys it
    const bool should_recreate = mainframe->preferences_dialog->recreate_GUI();
    const bool layout_changed = mainframe->preferences_dialog->settings_layout_changed();

    if (should_recreate)
        recreate_GUI(_L("Restart application") + dots);
    else if (is_editor())
        // The browser preference changes the printer tab's mode; the tab is rebuilt only when the
        // mode actually changed
        show_printer_webview_tab();

#ifdef _WIN32
    if (is_editor())
    {
        if (app_config->get_bool("associate_3mf"))
            associate_3mf_files();
        if (app_config->get_bool("associate_stl"))
            associate_stl_files();
    }
    else
    {
        if (app_config->get_bool("associate_gcode"))
            associate_gcode_files();
        if (app_config->get_bool("associate_bgcode"))
            associate_bgcode_files();
    }
#endif // _WIN32

    if (layout_changed)
    {
        // hide full main_sizer for mainFrame
        mainframe->GetSizer()->Show(false);
        mainframe->update_layout();
        mainframe->select_tab(size_t(0));
    }
}

bool GUI_App::has_unsaved_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab *const tab : tabs_list)
    {
        if (tab->supports_printer_technology(printer_technology) && tab->saved_preset_is_dirty())
            return true;
    }
    return false;
}

bool GUI_App::has_current_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab *const tab : tabs_list)
    {
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            return true;
    }
    return false;
}

void GUI_App::update_saved_preset_from_current_preset()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab *tab : tabs_list)
    {
        if (tab->supports_printer_technology(printer_technology))
            tab->update_saved_preset_from_current_preset();
    }
}

std::vector<const PresetCollection *> GUI_App::get_active_preset_collections() const
{
    std::vector<const PresetCollection *> ret;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab *tab : tabs_list)
        if (tab->supports_printer_technology(printer_technology))
            ret.push_back(tab->get_presets());
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
bool GUI_App::check_and_save_current_preset_changes(const wxString &caption, const wxString &header,
                                                    bool remember_choice /* = true*/,
                                                    bool dont_save_insted_of_discard /* = false*/)
{
    if (has_current_preset_changes())
    {
        const std::string app_config_key = remember_choice ? "default_action_on_close_application" : "";
        int act_buttons = ActionButtons::SAVE;
        if (dont_save_insted_of_discard)
            act_buttons |= ActionButtons::DONT_SAVE;
        UnsavedChangesDialog dlg(caption, header, app_config_key, act_buttons);
        std::string act = app_config_key.empty() ? "none" : wxGetApp().app_config->get(app_config_key);
        if (act == "none" && dlg.ShowModal() == wxID_CANCEL)
            return false;

        if (dlg.save_preset()) // save selected changes
        {
            for (const std::pair<std::string, Preset::Type> &nt : dlg.get_names_and_types())
                preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

            load_current_presets(false);

            // if we saved changes to the new presets, we should to
            // synchronize config.ini with the current selections.
            preset_bundle->export_selections(*app_config);

            MessageDialog(nullptr, dlg.msg_success_saved_modifications(dlg.get_names_and_types().size())).ShowModal();
        }
    }

    return true;
}

void GUI_App::apply_keeped_preset_modifications()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab *tab : tabs_list)
    {
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
bool GUI_App::check_and_keep_current_preset_changes(const wxString &caption, const wxString &header, int action_buttons,
                                                    bool *postponed_apply_of_keeped_changes /* = nullptr*/)
{
    if (has_current_preset_changes())
    {
        bool is_called_from_configwizard = postponed_apply_of_keeped_changes != nullptr;

        const std::string app_config_key = is_called_from_configwizard ? "" : "default_action_on_new_project";
        UnsavedChangesDialog dlg(caption, header, app_config_key, action_buttons);
        std::string act = app_config_key.empty() ? "none" : wxGetApp().app_config->get(app_config_key);
        if (act == "none" && dlg.ShowModal() == wxID_CANCEL)
            return false;

        auto reset_modifications = [this, is_called_from_configwizard]()
        {
            if (is_called_from_configwizard)
                return; // no need to discared changes. It will be done fromConfigWizard closing

            PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
            for (const Tab *const tab : tabs_list)
            {
                if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        };

        if (dlg.discard())
            reset_modifications();
        else // save selected changes
        {
            const auto &preset_names_and_types = dlg.get_names_and_types();
            if (dlg.save_preset())
            {
                for (const std::pair<std::string, Preset::Type> &nt : preset_names_and_types)
                    preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

                // if we saved changes to the new presets, we should to
                // synchronize config.ini with the current selections.
                preset_bundle->export_selections(*app_config);

                wxString text = dlg.msg_success_saved_modifications(preset_names_and_types.size());
                if (!is_called_from_configwizard)
                    text += "\n\n" + _L("For new project all modifications will be reseted");

                MessageDialog(nullptr, text).ShowModal();
                reset_modifications();
            }
            else if (dlg.transfer_changes() && (dlg.has_unselected_options() || is_called_from_configwizard))
            {
                // execute this part of code only if not all modifications are keeping to the new project
                // OR this function is called when ConfigWizard is closed and "Keep modifications" is selected
                for (const std::pair<std::string, Preset::Type> &nt : preset_names_and_types)
                {
                    Preset::Type type = nt.second;
                    Tab *tab = get_tab(type);
                    std::vector<std::string> selected_options = dlg.get_selected_options(type);
                    if (type == Preset::TYPE_PRINTER)
                    {
                        auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                        if (it != selected_options.end())
                        {
                            // erase "extruders_count" option from the list
                            selected_options.erase(it);
                            // cache the extruders count
                            static_cast<TabPrinter *>(tab)->cache_extruder_cnt();
                        }
                    }
                    tab->cache_config_diff(selected_options);
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
    int saved_project = plater()->save_project_if_dirty(
        _L("Loading a new project while the current project is modified."));
    if (saved_project == wxID_CANCEL ||
        (plater()->is_project_dirty() && saved_project == wxID_NO &&
         !check_and_save_current_preset_changes(_L("Project is loading"),
                                                _L("Opening new project while some presets are unsaved."))))
        return false;
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
    for (const auto &job : jobs)
    {
        job_string += format_wxstr("   %1% : %2% \n", job.first, job.second);
    }
    wxString message;
    message += _(L("The uploads are still ongoing")) + ":\n\n" + job_string + "\n" +
               _(L("Stop them and continue anyway?"));
    //wxMessageDialog dialog(mainframe,
    MessageDialog dialog(mainframe, message, wxString(PREFLIGHT_APP_NAME) + " - " + _(L("Ongoing uploads")),
                         wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    if (dialog.ShowModal() == wxID_YES)
        return true;

    // Show() does not raise a window that is already open.
    mainframe->m_printhost_queue_dlg->Show();
    return false;
}

bool GUI_App::checked_tab(Tab *tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end())
        ret = false;
    return ret;
}

// Update UI / Tabs to reflect changes in the currently loaded presets
void GUI_App::load_current_presets(bool check_printer_presets_ /*= true*/)
{
    // check printer_presets for the containing information about "Print Host upload"
    // and create physical printer from it, if any exists
    if (check_printer_presets_)
        check_printer_presets();

    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
        if (tab->supports_printer_technology(printer_technology))
        {
            if (tab->type() == Preset::TYPE_PRINTER)
                // Mark the plater to update print bed by tab->load_current_preset() from Plater::on_config_change().
                this->plater()->force_print_bed_update();
            else if (tab->type() == Preset::TYPE_FILAMENT)
                // active extruder can be changed in a respect to the new loaded configurations, if some filament preset will be modified
                static_cast<TabFilament *>(tab)->invalidate_active_extruder();
            tab->load_current_preset();
        }

    // After all tabs are loaded, apply the print preset's nozzle diameters so they take
    // precedence over the printer preset's at startup.
    if (printer_technology == ptFFF)
    {
        Tab *print_tab = get_tab(Preset::TYPE_PRINT);
        if (print_tab)
        {
            // Cast to TabPrint and call on_preset_loaded to apply nozzle overrides
            static_cast<TabPrint *>(print_tab)->on_preset_loaded();
        }
        else
        {
        }
    }
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
    size_t num_gcodes = 0;
    for (const wxString &filename : fileNames)
        if (is_gcode_file(into_u8(filename)))
            ++num_gcodes;
    if (fileNames.size() == num_gcodes)
    {
        // Opening preFlight by drag & dropping a G-Code onto preFlight icon in Finder,
        // just G-codes were passed. Switch to G-code viewer mode.
        m_app_mode = EAppMode::GCodeViewer;
        unlock_lockfile(get_instance_hash_string() + ".lock", data_dir() + "/cache/");
        if (app_config != nullptr)
            delete app_config;
        app_config = nullptr;
        init_app_config();
    }
    wxApp::OSXStoreOpenFiles(fileNames);
}
// wxWidgets override to get an event on open files.
void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    std::vector<std::string> files;
    std::vector<wxString> gcode_files;
    std::vector<wxString> non_gcode_files;
    for (const auto &filename : fileNames)
    {
        if (is_gcode_file(into_u8(filename)))
            gcode_files.emplace_back(filename);
        else
        {
            files.emplace_back(into_u8(filename));
            non_gcode_files.emplace_back(filename);
        }
    }
    if (m_app_mode == EAppMode::GCodeViewer)
    {
        // Running in G-code viewer.
        // Load the first G-code into the G-code viewer.
        // Or if no G-codes, send other files to slicer.
        if (!gcode_files.empty())
        {
            if (m_post_initialized)
                this->plater()->load_gcode(gcode_files.front());
            else
                this->init_params->input_files = {into_u8(gcode_files.front())};
        }
        if (!non_gcode_files.empty())
            start_new_slicer(non_gcode_files, true);
    }
    else
    {
        if (!files.empty())
        {
            if (m_post_initialized)
            {
                wxArrayString input_files;
                for (size_t i = 0; i < non_gcode_files.size(); ++i)
                    input_files.push_back(non_gcode_files[i]);
                this->plater()->load_files(input_files);
            }
            else
            {
                for (const auto &f : non_gcode_files)
                    this->init_params->input_files.emplace_back(into_u8(f));
            }
        }
        for (const wxString &filename : gcode_files)
            start_new_gcodeviewer(&filename);
    }
}

void GUI_App::MacOpenURL(const wxString &url)
{
    std::string narrow_url = into_u8(url);
    if (boost::starts_with(narrow_url, "preflight://open?file="))
    {
        // This app config field applies only to downloading file
        // (we need to handle login URL even if this flag is set off)
        if (app_config && !app_config->get_bool("downloader_url_registered"))
        {
            notification_manager()->push_notification(NotificationType::URLNotRegistered);
            BOOST_LOG_TRIVIAL(error) << "Recieved command to open URL, but it is not allowed in app configuration. URL: "
                                     << url;
            return;
        }

        start_download(std::move(narrow_url));
    }
    else if (boost::starts_with(narrow_url, "preflight://login"))
    {
        // No UserAccount system, so login URLs are not supported
        BOOST_LOG_TRIVIAL(warning) << "MacOpenURL: login URL received but UserAccount is not available: " << url;
    }
    else
    {
        BOOST_LOG_TRIVIAL(error) << "MacOpenURL recieved improper URL: " << url;
    }
}

#endif /* __APPLE */

Sidebar &GUI_App::sidebar()
{
    return plater_->sidebar();
}

ObjectManipulation *GUI_App::obj_manipul()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return (plater_ != nullptr) ? sidebar().obj_manipul() : nullptr;
}

ObjectSettings *GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList *GUI_App::obj_list()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return plater_ ? sidebar().obj_list() : nullptr;
}

ObjectLayers *GUI_App::obj_layers()
{
    return sidebar().obj_layers();
}

Plater *GUI_App::plater()
{
    return plater_;
}

const Plater *GUI_App::plater() const
{
    return plater_;
}

Model &GUI_App::model()
{
    return plater_->model();
}
wxBookCtrlBase *GUI_App::tab_panel() const
{
    return mainframe->m_tabpanel;
}

NotificationManager *GUI_App::notification_manager()
{
    return plater_->get_notification_manager();
}

GalleryDialog *GUI_App::gallery_dialog()
{
    return mainframe->gallery_dialog();
}

Downloader *GUI_App::downloader()
{
    return m_downloader.get();
}

// extruders count from selected printer preset
int GUI_App::extruders_cnt() const
{
    const Preset &preset = preset_bundle->printers.get_selected_preset();
    return preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// extruders count from edited printer preset
int GUI_App::extruders_edited_cnt() const
{
    const Preset &preset = preset_bundle->printers.get_edited_preset();
    return preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

wxString GUI_App::current_language_code_safe() const
{
    // Translate the language code to a code for which translations are maintained.
    const std::map<wxString, wxString> mapping{
        {
            "cs",
            "cs_CZ",
        },
        {
            "sk",
            "cs_CZ",
        },
        {
            "de",
            "de_DE",
        },
        {
            "es",
            "es_ES",
        },
        {
            "fr",
            "fr_FR",
        },
        {
            "it",
            "it_IT",
        },
        {
            "ja",
            "ja_JP",
        },
        {
            "ko",
            "ko_KR",
        },
        {
            "pl",
            "pl_PL",
        },
        //{ "uk", 	"uk_UA", },
        //{ "zh", 	"zh_CN", },
        //{ "ru", 	"ru_RU", },
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
    open_browser_with_warning_dialog(from_u8(http_address + "&lng=") + this->current_language_code_safe(), nullptr,
                                     false);
}

bool GUI_App::run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page)
{
    wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");

    // Loading of Config Wizard takes some time.
    // First part is to download neccessary data.
    // That is done on worker thread while nice modal progress is shown.
    // TRN: Progress dialog title
    // get_preset_updater_wrapper()->wizard_sync(preset_bundle, app_config->orig_version(), mainframe, reason == ConfigWizard::RunReason::RR_USER, _L("Opening Configuration Wizard"));
    // Then the wizard itself will start and that also takes time.
    // But for now no ui is shown until then. (Showing modal progress dialog while showing another would be a headacke)
    m_config_wizard = new ConfigWizard(mainframe);
    const bool res = m_config_wizard->run(reason, start_page);

    // Deallocate memory after close ConfigWizard.
    // Note, that mainframe is a parent of ConfigWizard.
    // So, wizard will be destroyed only during destroying of mainframe
    // To avoid this state the wizard have to be disconnected from mainframe and Destroyed explicitly
    mainframe->RemoveChild(m_config_wizard);
    m_config_wizard->Destroy();
    m_config_wizard = nullptr;

    if (res)
    {
        load_current_presets();
    }
    return res;
}

void GUI_App::update_wizard_login_page()
{
    if (!m_config_wizard)
    {
        return;
    }
    m_config_wizard->update_login();
}

void GUI_App::show_desktop_integration_dialog()
{
#ifdef __linux__
    //wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");
    DesktopIntegrationDialog dialog(mainframe);
    dialog.ShowModal();
#endif //__linux__
}

void GUI_App::show_downloader_registration_dialog()
{
    InfoDialog msg(nullptr, format_wxstr(_L("Welcome to %1% version %2%."), PREFLIGHT_APP_NAME, PREFLIGHT_VERSION),
                   format_wxstr(_L("Do you wish to register downloads from supported websites"
                                   "\nfor this <b>%1% %2%</b> executable?"
                                   "\n\nDownloads can be registered for only 1 executable at a time."),
                                PREFLIGHT_APP_NAME, PREFLIGHT_VERSION),
                   true, wxYES_NO);
    if (msg.ShowModal() == wxID_YES)
    {
        auto downloader_worker = new DownloaderUtils::Worker(nullptr);
        downloader_worker->perform_download_register(app_config->get("url_downloader_dest"));
#if defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
        if (DownloaderUtils::Worker::perform_registration_linux)
            DesktopIntegrationDialog::perform_downloader_desktop_integration();
#endif //(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    }
    else
    {
        app_config->set("downloader_url_registered", "0");
    }
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

    wxFileDialog dialog(GetTopWindow(), _L("Select a gcode file:"), "", "", "G-code files (*.gcode)|*.gcode;*.GCODE;",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
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
                    width = (unsigned int) ::atoi(width_str.c_str());
                    std::string height_str = gcode_line.substr(x_pos + 1);
                    height = (unsigned int) ::atoi(height_str.c_str());
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
                        decoded.resize(
                            boost::beast::detail::base64::decode((void *) &decoded[0], row.data(), row.size()).first);

                        out_file.write(decoded.c_str(), decoded.size());
                        out_file.close();
                    }

                    reading_image = false;
                    width = 0;
                    height = 0;
                    rows.clear();
                }
                else if (reading_image)
                    row += gcode_line.substr(2);
            }
        }

        in_file.close();
    }
}
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

void GUI_App::window_pos_save(wxTopLevelWindow *window, const std::string &name)
{
    if (name.empty())
    {
        return;
    }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    // save changed app_config here, before all action related to a close of application is processed
    if (app_config->dirty())
        app_config->save();
}

void GUI_App::window_pos_restore(wxTopLevelWindow *window, const std::string &name, bool default_maximized)
{
    if (name.empty())
    {
        return;
    }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (!app_config->has(config_key))
    {
        window->Maximize(default_maximized);
        return;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (!metrics)
    {
        window->Maximize(default_maximized);
        return;
    }

    const wxRect &rect = metrics->get_rect();

    if (app_config->get_bool("restore_win_position"))
    {
        // workaround for crash related to the positioning of the window on secondary monitor
        app_config->set("restore_win_position", (boost::format("crashed_at_%1%_pos") % name).str());
        app_config->save();
        window->SetPosition(rect.GetPosition());

        // workaround for crash related to the positioning of the window on secondary monitor
        app_config->set("restore_win_position", (boost::format("crashed_at_%1%_size") % name).str());
        app_config->save();
        window->SetSize(rect.GetSize());

        // revert "restore_win_position" value if application wasn't crashed
        app_config->set("restore_win_position", "1");
        app_config->save();
    }
    else
        window->CenterOnScreen();

    window->Maximize(metrics->get_maximized());
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow *window)
{
    /*unsigned*/ int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND)
    {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    }
    else
    {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect())
    {
        window->SetSize(metrics.get_rect());
    }
}

bool GUI_App::config_wizard_startup()
{
    if (!m_app_conf_exists || preset_bundle->printers.only_default_printers())
    {
        run_wizard(ConfigWizard::RR_DATA_EMPTY);
        return true;
    }
    else if (get_app_config()->legacy_datadir())
    {
        // Legacy pre-vendorbundle data directory - just run the wizard
        run_wizard(ConfigWizard::RR_DATA_LEGACY);
        return true;
    }
#ifndef __APPLE__
    else if (is_editor() && m_last_app_conf_lower_version && app_config->get_bool("downloader_url_registered"))
    {
        show_downloader_registration_dialog();
        return true;
    }
#endif
    return false;
}

bool GUI_App::check_updates(const bool invoked_by_user)
{
    // Return early to prevent any vendor profile update checks
    return true;
    /*
    PresetUpdater::UpdateResult updater_result;
    if (invoked_by_user)
    {
         updater_result = get_preset_updater_wrapper()->check_updates_on_user_request(preset_bundle, app_config->orig_version(), mainframe);
    } else {
        updater_result = get_preset_updater_wrapper()->check_updates_on_startup( app_config->orig_version());
    }
	if (updater_result == PresetUpdater::R_INCOMPAT_EXIT) {
		mainframe->Close();
        // Applicaiton is closing.
        return false;
	}
	else if (updater_result == PresetUpdater::R_INCOMPAT_CONFIGURED) {
        m_app_conf_exists = true;
	}
	else if (invoked_by_user && updater_result == PresetUpdater::R_NOOP) {
		MsgNoUpdates dlg;
		dlg.ShowModal();
	}
    // Applicaiton will continue.
    return true;
    */
}
namespace
{
bool open_dialog_hyperlink_checkbox(wxWindow *parent, AppConfig *app_config)
{
    RichMessageDialog dialog(parent, _L("Open hyperlink in default browser?"), _L("preFlight: Open hyperlink"),
                             wxICON_QUESTION | wxYES_NO);
    dialog.ShowCheckBox(_L("Remember my choice"));
    auto answer = dialog.ShowModal();
    bool launch = answer == wxID_YES;
    if (dialog.IsCheckBoxChecked())
    {
        wxString preferences_item = _L("Suppress to open hyperlink in browser");
        wxString msg = _L("preFlight will remember your choice.") + "\n\n" +
                       _L("You will not be asked about it again on hyperlinks hovering.") + "\n\n" +
                       format_wxstr(_L("Visit \"Preferences\" and check \"%1%\"\nto changes your choice."),
                                    preferences_item);

        MessageDialog msg_dlg(parent, msg, _L("preFlight: Don't ask me again"), wxOK | wxCANCEL | wxICON_INFORMATION);
        if (msg_dlg.ShowModal() == wxID_CANCEL)
            return false;
        app_config->set("suppress_hyperlinks", answer == wxID_NO ? "1" : "0");
    }
    return launch;
}
bool open_dialog_hyperlink(wxWindow *parent)
{
    MessageDialog dialog(parent, _L("Open hyperlink in default browser?"), _L("preFlight: Open hyperlink"),
                         wxICON_QUESTION | wxYES_NO);
    return dialog.ShowModal() == wxID_YES;
}
} // namespace
bool GUI_App::open_browser_with_warning_dialog(const wxString &url, wxWindow *parent /* = nullptr*/,
                                               bool force_remember_choice /*= true*/, int flags /* = 0*/)
{
    enum class SupressHyperLinksOption
    {
        SHLO_UNCHECKED,
        SHLO_ALWAYS_SUPRESS,
        SHLO_ALWAYS_ALLOW
    };
    bool empty = app_config->get("suppress_hyperlinks").empty();
    bool checked = app_config->get_bool("suppress_hyperlinks");
    SupressHyperLinksOption opt_val = (empty ? SupressHyperLinksOption::SHLO_UNCHECKED
                                             : (checked ? SupressHyperLinksOption::SHLO_ALWAYS_SUPRESS
                                                        : SupressHyperLinksOption::SHLO_ALWAYS_ALLOW));
    bool launch = true;
    if (opt_val == SupressHyperLinksOption::SHLO_UNCHECKED)
    {
        // no previous action from user
        // open dialog with remember checkbox
        launch = open_dialog_hyperlink_checkbox(parent, app_config);
    }
    else if (opt_val == SupressHyperLinksOption::SHLO_ALWAYS_ALLOW)
    {
        // user already set checkbox to always open
        launch = true;
    }
    else if (opt_val == SupressHyperLinksOption::SHLO_ALWAYS_SUPRESS && force_remember_choice)
    {
        // user already set checkbox or preferences to always supress
        launch = false;
    }
    else if (opt_val == SupressHyperLinksOption::SHLO_ALWAYS_SUPRESS && !force_remember_choice)
    {
        // user already set checkbox or preferences to always supress but it is overriden
        // no checkbox in dialog
        launch = open_dialog_hyperlink(parent);
    }
    return launch && wxLaunchDefaultBrowser(url, flags);
}

bool GUI_App::open_login_browser_with_dialog(const wxString &url, wxWindow *parent /* = nullptr*/, int flags /* = 0*/)
{
    bool auth_login_dialog_confirmed = app_config->get_bool("auth_login_dialog_confirmed");
    if (!auth_login_dialog_confirmed)
    {
        RichMessageDialog dialog(
            parent, _L("Open default browser with Log in page?\n(If you select 'Yes', you will not be asked again.)"),
            _L("preFlight: Open Log in page"), wxICON_QUESTION | wxYES_NO);
        if (dialog.ShowModal() != wxID_YES)
            return false;
        app_config->set("auth_login_dialog_confirmed", "1");
    }
    return wxLaunchDefaultBrowser(url, flags);
}

#ifdef __WXMSW__
void GUI_App::associate_3mf_files()
{
    associate_file_type(L".3mf", L"preFlight.Slicer.1", L"preFlight", true);
}

void GUI_App::associate_stl_files()
{
    associate_file_type(L".stl", L"preFlight.Slicer.1", L"preFlight", true);
}

void GUI_App::associate_gcode_files()
{
    associate_file_type(L".gcode", L"preFlight.GCodeViewer.1", L"preFlightGCodeViewer", true);
}

void GUI_App::associate_bgcode_files()
{
    associate_file_type(L".bgcode", L"preFlight.GCodeViewer.1", L"preFlightGCodeViewer", true);
}
#endif // __WXMSW__

void GUI_App::on_version_read(wxCommandEvent &evt)
{
    app_config->set("version_online", into_u8(evt.GetString()));
    std::string opt = app_config->get("notify_release");
    if (this->plater_ == nullptr || (!m_app_updater->get_triggered_by_user() && opt != "all" && opt != "release"))
    {
        BOOST_LOG_TRIVIAL(info) << "Version online: " << evt.GetString() << ". User does not wish to be notified.";
        return;
    }
    if (*Semver::parse(PREFLIGHT_VERSION) >= *Semver::parse(into_u8(evt.GetString())))
    {
        if (m_app_updater->get_triggered_by_user())
        {
            std::string text = (*Semver::parse(into_u8(evt.GetString())) == Semver())
                                   ? _u8L("Check for application update has failed.")
                                   : Luminary::format(_u8L(
                                                          "You are currently running the latest released version %1%."),
                                                      evt.GetString());

            if (*Semver::parse(PREFLIGHT_VERSION) > *Semver::parse(into_u8(evt.GetString())))
                text = Luminary::format(
                    _u8L("There are no new released versions online. The latest release version is %1%."),
                    evt.GetString());

            this->plater_->get_notification_manager()->push_version_notification(
                NotificationType::NoNewReleaseAvailable,
                NotificationManager::NotificationLevel::RegularNotificationLevel, text, std::string(),
                std::function<bool(wxEvtHandler *)>());
        }
        return;
    }
    // User-triggered checks and the first automatic check (startup)
    // get the full modal dialog.  Subsequent periodic checks get a toast notification.
    if (m_app_updater->get_triggered_by_user() || !m_startup_update_shown)
    {
        m_startup_update_shown = true;
        app_updater(m_app_updater->get_triggered_by_user());
    }
    else
    {
        std::string online_ver = into_u8(evt.GetString());
        if (online_ver != m_version_last_notified)
        {
            m_version_last_notified = online_ver;
            this->plater_->get_notification_manager()->push_notification(
                NotificationType::NewAppAvailable, NotificationManager::NotificationLevel::ImportantNotificationLevel,
                Luminary::format(_u8L("New version %1% is available."), online_ver), _u8L("Download"),
                [](wxEvtHandler *)
                {
                    wxGetApp().app_updater(false);
                    return true;
                });
        }
    }
}

void GUI_App::app_updater(bool from_user)
{
    DownloadAppData app_data = m_app_updater->get_app_data();

    if (from_user && (!app_data.version || *app_data.version <= *Semver::parse(PREFLIGHT_VERSION)))
    {
        BOOST_LOG_TRIVIAL(info) << "There is no newer version online.";
        MsgNoAppUpdates no_update_dialog;
        no_update_dialog.ShowModal();
        return;
    }

    assert(!app_data.url.empty());
    assert(!app_data.target_path.empty());

    // dialog with new version info
    AppUpdateAvailableDialog dialog(*Semver::parse(PREFLIGHT_VERSION), *app_data.version, from_user,
                                    app_data.action == AppUpdaterURLAction::AUUA_OPEN_IN_BROWSER,
                                    app_data.release_notes, app_data.release_page);
    auto dialog_result = dialog.ShowModal();
    // checkbox "do not show again"
    if (dialog.disable_version_check())
    {
        app_config->set("notify_release", "none");
        // Stop periodic version checks since the user opted out
        if (m_version_check_timer)
        {
            m_version_check_timer->Stop();
            delete m_version_check_timer;
            m_version_check_timer = nullptr;
        }
    }
    // Doesn't wish to update
    if (dialog_result != wxID_OK)
    {
        return;
    }
    if (app_data.action == AppUpdaterURLAction::AUUA_OPEN_IN_BROWSER)
    {
        open_browser_with_warning_dialog(from_u8(app_data.url), nullptr, false);
        return;
    }
    // dialog with new version download (installer or app dependent on system) including path selection
    AppUpdateDownloadDialog dwnld_dlg(*app_data.version, app_data.target_path);
    dialog_result = dwnld_dlg.ShowModal();
    //  Doesn't wish to download
    if (dialog_result != wxID_OK)
    {
        return;
    }
    app_data.target_path = dwnld_dlg.get_download_path();
    // start download
    this->plater_->get_notification_manager()->push_download_progress_notification(
        DSKY::format(_L("Downloading %1%"), app_data.target_path.filename().string()),
        std::bind(&AppUpdater::cancel_callback, this->m_app_updater.get()));
    app_data.start_after = dwnld_dlg.run_after_download();
    m_app_updater->set_app_data(std::move(app_data));
    m_app_updater->sync_download();
}

void GUI_App::app_version_check(bool from_user)
{
    if (from_user)
    {
        if (m_app_updater->get_download_ongoing())
        {
            MessageDialog msgdlg(nullptr, _L("Downloading of the new version is in progress. Do you want to continue?"),
                                 _L("Notice"), wxYES_NO);
            if (msgdlg.ShowModal() != wxID_YES)
                return;
        }
    }
    std::string version_check_url = app_config->version_check_url();
    m_app_updater->sync_version(version_check_url, from_user);
}

void GUI_App::start_download(std::string url)
{
    if (!plater_)
    {
        BOOST_LOG_TRIVIAL(error) << "Could not start URL download: plater is nullptr.";
        return;
    }

#if defined(__APPLE__) || (defined(__linux__) && !defined(PREFLIGHT_DESKTOP_INTEGRATION))
    if (app_config && !app_config->get_bool("downloader_url_registered"))
    {
        notification_manager()->push_notification(NotificationType::URLNotRegistered);
        BOOST_LOG_TRIVIAL(error) << "Received command to open URL, but it is not allowed in app configuration. URL: "
                                 << url;
        return;
    }
#endif //defined(__APPLE__) || (defined(__linux__) && !defined(PREFLIGHT_DESKTOP_INTEGRATION))

    //lets always init so if the download dest folder was changed, new dest is used
    boost::filesystem::path dest_folder(app_config->get("url_downloader_dest"));
    if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder))
    {
        std::string msg = _u8L(
            "Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return;
    }
    m_downloader->init(dest_folder);
    m_downloader->start_download(url);
}

// Returns true if preset had to be installed.
bool GUI_App::select_printer_preset(const Preset *preset)
{
    assert(preset);

    bool is_installed{false};

    // When physical printer is selected, it somehow remains selected in printer tab
    // TabPresetComboBox::update() looks at physical_printers and if some has selected = true, it overrides the selection.
    // This might be, because OnSelect event callback is not triggered
    if (preset_bundle->physical_printers.get_selected_printer_config())
    {
        preset_bundle->physical_printers.unselect_printer();
    }

    if (!preset->is_visible)
    {
        size_t preset_id = preset_bundle->printers.get_preset_idx_by_name(preset->name);
        assert(preset_id != size_t(-1));
        preset_bundle->printers.select_preset(preset_id);
        is_installed = true;
    }

    get_tab(Preset::Type::TYPE_PRINTER)->select_preset(preset->name);
    return is_installed;
}

namespace
{
const Preset *find_preset_by_nozzle_and_options(const PrinterPresetCollection &collection, const std::string &model_id,
                                                std::map<std::string, std::vector<std::string>> &options)
{
    // find all matching presets when repo prefix is ommited
    std::vector<const Preset *> results;
    for (const Preset &preset : collection)
    {
        // trim repo prefix
        std::string printer_model = preset.config.opt_string("printer_model");
        const PresetWithVendorProfile &printer_with_vendor = collection.get_preset_with_vendor_profile(preset);
        printer_model = preset.trim_vendor_repo_prefix(printer_model, printer_with_vendor.vendor);

        if (!preset.is_system || printer_model != model_id)
            continue;
        // options (including nozzle_diameter)
        bool failed = false;
        for (const auto &opt : options)
        {
            assert(preset.config.has(opt.first));
            // We compare only first value now, but options contains data for all (some might be empty tho)
            std::string opt_val;
            if (preset.config.option(opt.first)->is_scalar())
            {
                opt_val = preset.config.option(opt.first)->serialize();
            }
            else
            {
                switch (preset.config.option(opt.first)->type())
                {
                case coInts:
                    opt_val = std::to_string(
                        static_cast<const ConfigOptionInts *>(preset.config.option(opt.first))->values[0]);
                    break;
                case coFloats:
                    opt_val = into_u8(double_to_string(
                        static_cast<const ConfigOptionFloats *>(preset.config.option(opt.first))->values[0]));
                    if (size_t pos = opt_val.find(",") != std::string::npos)
                        opt_val.replace(pos, 1, 1, '.');
                    break;
                case coStrings:
                    opt_val = static_cast<const ConfigOptionStrings *>(preset.config.option(opt.first))->values[0];
                    break;
                case coBools:
                    opt_val = static_cast<const ConfigOptionBools *>(preset.config.option(opt.first))->values[0] ? "1"
                                                                                                                 : "0";
                    break;
                default:
                    assert(false);
                    continue;
                }
            }

            if (opt_val != opt.second[0])
            {
                failed = true;
                break;
            }
        }
        if (!failed)
        {
            results.push_back(&preset);
        }
    }
    // find visible without prefix
    for (const Preset *preset : results)
    {
        if (preset->is_visible && preset->config.opt_string("printer_model") == model_id)
        {
            return preset;
        }
    }
    // find one visible
    for (const Preset *preset : results)
    {
        if (preset->is_visible)
        {
            return preset;
        }
    }
    // find one without prefix
    for (const Preset *preset : results)
    {
        if (preset->config.opt_string("printer_model") == model_id)
        {
            return preset;
        }
    }
    if (results.size() != 0)
    {
        return results.front();
    }
    return nullptr;
}
} // namespace

bool GUI_App::select_printer_from_connect(const std::string & /*msg*/)
{
    return false;
}

bool GUI_App::select_filament_preset(const Preset *preset, size_t extruder_index)
{
    assert(preset && preset->is_compatible);

    if (!preset->is_visible)
    {
        // To correct update of presets visibility call select_preset for preset_bundle->filaments()
        size_t preset_id = preset_bundle->filaments.get_preset_idx_by_name(preset->name);
        assert(preset_id != size_t(-1));
        preset_bundle->filaments.select_preset(preset_id);
    }
    assert(preset->is_visible);
    return preset_bundle->extruders_filaments[extruder_index].select_filament(preset->name);
}
void GUI_App::search_and_select_filaments(const std::string &material, bool avoid_abrasive, size_t extruder_index,
                                          std::string &out_message)
{
    const Preset *preset = preset_bundle->extruders_filaments[extruder_index].get_selected_preset();
    // selected is ok
    if (!preset->is_default && preset->config.has("filament_type") &&
        (!avoid_abrasive || preset->config.option<ConfigOptionBools>("filament_abrasive")->values[0] == false) &&
        preset->config.option("filament_type")->serialize() == material)
    {
        return;
    }
    // find installed compatible filament with suitable type and select it
    for (const auto &filament : preset_bundle->extruders_filaments[extruder_index])
    {
        if (filament.is_compatible && !filament.preset->is_default && filament.preset->is_visible &&
            filament.preset->config.has("filament_type") &&
            (!avoid_abrasive ||
             filament.preset->config.option<ConfigOptionBools>("filament_abrasive")->values[0] == false) &&
            filament.preset->config.option("filament_type")->serialize() == material &&

            select_filament_preset(filament.preset, extruder_index))
        {
            out_message += /*(extruder_count == 1)
                ? DSKY::format(_L("Selected Filament:\n%1%"), filament_preset.preset->name)
                : */
                DSKY::format(_L("Extruder %1%: Selected filament %2%"), extruder_index + 1, filament.preset->name) +
                "\n";
            return;
        }
    }
    // find first installed compatible filament with suitable type and select it
    for (const auto &filament : preset_bundle->extruders_filaments[extruder_index])
    {
        if (filament.is_compatible && !filament.preset->is_default && filament.preset->is_visible &&
            filament.preset->config.has("filament_type") &&
            (!avoid_abrasive ||
             filament.preset->config.option<ConfigOptionBools>("filament_abrasive")->values[0] == false) &&
            filament.preset->config.option("filament_type")->serialize() == material &&
            select_filament_preset(filament.preset, extruder_index))
        {
            out_message += /*(extruder_count == 1)
                ? DSKY::format(_L("Selected Filament:\n%1%"), filament_preset.preset->name)
                : */
                DSKY::format(_L("Extruder %1%: Selected filament %2%"), extruder_index + 1, filament.preset->name) +
                "\n";
            return;
        }
    }
    // find profile to install
    // try finding any compatible filament to install
    for (const auto &filament : preset_bundle->extruders_filaments[extruder_index])
    {
        if (filament.is_compatible && !filament.preset->is_default && filament.preset->config.has("filament_type") &&
            (!avoid_abrasive ||
             filament.preset->config.option<ConfigOptionBools>("filament_abrasive")->values[0] == false) &&
            filament.preset->config.option("filament_type")->serialize() == material &&

            select_filament_preset(filament.preset, extruder_index))
        {
            out_message += DSKY::format(_L("Extruder %1%: Installed and selected filament %2%"), extruder_index + 1,
                                        filament.preset->name) +
                           "\n";
            return;
        }
    }
    out_message += DSKY::format(_L("Extruder %2%: Failed to find and select filament type: %1%"), material,
                                extruder_index + 1) +
                   "\n";
}

void GUI_App::select_filament_from_connect(const std::string & /*msg*/) {}

void GUI_App::handle_connect_request_printer_select(const std::string & /*msg*/) {}
void GUI_App::handle_connect_request_printer_select_inner(const std::string & /*msg*/) {}

void GUI_App::show_printer_webview_tab()
{
    mainframe->show_printer_webview_tab(preset_bundle->physical_printers.get_selected_printer_config());
}

void GUI_App::printables_download_request(const std::string &download_url, const std::string &model_url)
{
    //this->mainframe->select_tab(size_t(0));

    //lets always init so if the download dest folder was changed, new dest is used
    boost::filesystem::path dest_folder(app_config->get("url_downloader_dest"));
    if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder))
    {
        std::string msg = _u8L(
            "Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return;
    }
    m_downloader->init(dest_folder);
    m_downloader->start_download_printables(download_url, false, model_url, this);
}
void GUI_App::printables_slice_request(const std::string &download_url, const std::string &model_url)
{
    this->mainframe->select_tab(size_t(0));

    //lets always init so if the download dest folder was changed, new dest is used
    boost::filesystem::path dest_folder(app_config->get("url_downloader_dest"));
    if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder))
    {
        std::string msg = _u8L(
            "Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return;
    }
    m_downloader->init(dest_folder);
    m_downloader->start_download_printables(download_url, true, model_url, this);
}

void GUI_App::printables_login_request() {}

bool GUI_App::is_account_logged_in() const
{
    return false;
}

bool LogGui::ignorred_message(const wxString &msg)
{
    for (const wxString &err :
         std::initializer_list<wxString>{wxString("cHRM chunk does not match sRGB"),
                                         wxString("known incorrect sRGB profile"), wxString("Error running JavaScript"),
                                         wxString("lost focus even though it didn't have it")})
    {
        if (msg.Contains(err))
            return true;
    }
    return false;
}

void LogGui::DoLogText(const wxString &msg)
{
    if (ignorred_message(msg))
        return;
    wxLogGui::DoLogText(msg);
}

void LogGui::DoLogRecord(wxLogLevel level, const wxString &msg, const wxLogRecordInfo &info)
{
    if (ignorred_message(msg))
        return;
    wxLogGui::DoLogRecord(level, msg, info);
}

} // namespace DSKY
