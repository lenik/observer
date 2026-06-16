#include "ObserverFrame.h"

#include "AppConfig.h"
#include "AppIcon.h"
#include "StatisticsDialog.h"
#include "WxDialogDriver.h"

#include <bas/locale/i18n.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wx/taskbar.h>

#if defined(__WXGTK__)
#include <X11/keysym.h>
#endif

namespace {

enum {
    ID_TRAY_WAKE = wxID_HIGHEST + 300,
    ID_TRAY_STATS,
    ID_TRAY_QUIT,
};

std::string ipcSocketPath()
{
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    std::string dir = runtimeDir != nullptr && *runtimeDir != '\0' ? runtimeDir : "/tmp";
    return dir + "/oremind-" + std::to_string(static_cast<long long>(getuid())) + ".sock";
}

bool fillSocketAddress(sockaddr_un& addr, const std::string& path)
{
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return true;
}

class ObserverTrayIcon : public wxTaskBarIcon {
public:
    explicit ObserverTrayIcon(ObserverFrame* frame)
        : m_frame(frame)
    {
        Bind(wxEVT_TASKBAR_LEFT_UP, [this](wxTaskBarIconEvent&) {
            if (m_frame != nullptr) {
                m_frame->wakePrompt();
            }
        });
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            if (m_frame != nullptr) {
                m_frame->wakePrompt();
            }
        }, ID_TRAY_WAKE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            if (m_frame != nullptr) {
                m_frame->showStatisticsDialog();
            }
        }, ID_TRAY_STATS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            if (m_frame != nullptr) {
                m_frame->exitApp();
            }
        }, ID_TRAY_QUIT);
    }

    wxMenu* CreatePopupMenu() override
    {
        auto* menu = new wxMenu();
        menu->Append(ID_TRAY_WAKE, wxString::FromUTF8(_("Wake")) + "\tWin+Alt+G");
        menu->Append(ID_TRAY_STATS, wxString::FromUTF8(_("Statistics / History")) + "\tWin+Alt+H");
        menu->AppendSeparator();
        menu->Append(ID_TRAY_QUIT, wxString::FromUTF8(_("Quit")));
        return menu;
    }

private:
    ObserverFrame* m_frame;
};

#if defined(__WXGTK__)
int ignoreXError(Display*, XErrorEvent*)
{
    return 0;
}
#endif

}

ObserverFrame::ObserverFrame()
    : wxFrame(nullptr, wxID_ANY, "Observer"),
      m_timer(this, PromptTimerId),
      m_hotKeyTimer(this, HotKeyTimerId),
      m_ipcTimer(this, IpcTimerId)
{
    m_intervalSeconds = appConfig().intervalSeconds;
    m_store = createObservationStore();
    m_renderDriver = std::make_unique<WxDialogDriver>(this);
    Bind(wxEVT_TIMER, &ObserverFrame::onTimer, this, PromptTimerId);
    Bind(wxEVT_TIMER, &ObserverFrame::onHotKeyPoll, this, HotKeyTimerId);
    Bind(wxEVT_TIMER, &ObserverFrame::onIpcPoll, this, IpcTimerId);
    setupIpcServer();
    setupTrayIcon();
    setupGlobalHotKey();
    const wxIcon appIcon = observerAppIcon(32);
    if (appIcon.IsOk()) {
        SetIcon(appIcon);
    }
    Hide();
    CallAfter(&ObserverFrame::showPrompt);
}

ObserverFrame::~ObserverFrame()
{
    cleanupGlobalHotKey();
    cleanupTrayIcon();
    cleanupIpcServer();
}

bool ObserverFrame::notifyExistingInstance()
{
    const std::string path = ipcSocketPath();
    sockaddr_un addr{};
    if (!fillSocketAddress(addr, path)) {
        return false;
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    const bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    if (connected) {
        const char command[] = "WAKE\n";
        (void)::write(fd, command, sizeof(command) - 1);
    }
    ::close(fd);
    return connected;
}

void ObserverFrame::onTimer(wxTimerEvent& event)
{
    (void)event;
    if (m_promptOpen) {
        return;
    }
    showPrompt();
}

void ObserverFrame::setupTrayIcon()
{
    m_trayIcon = std::make_unique<ObserverTrayIcon>(this);
    const wxIcon icon = observerAppIcon(32);
    if (icon.IsOk()) {
        m_trayIcon->SetIcon(icon, wxString::FromUTF8("Observer"));
    }
}

void ObserverFrame::cleanupTrayIcon()
{
    if (m_trayIcon != nullptr) {
        m_trayIcon->RemoveIcon();
        m_trayIcon.reset();
    }
}

void ObserverFrame::setupIpcServer()
{
    const std::string path = ipcSocketPath();
    sockaddr_un addr{};
    if (!fillSocketAddress(addr, path)) {
        return;
    }

    m_ipcServerFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_ipcServerFd < 0) {
        return;
    }

    ::unlink(path.c_str());
    if (::bind(m_ipcServerFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0
        || ::listen(m_ipcServerFd, 4) != 0) {
        cleanupIpcServer();
        return;
    }

    int flags = ::fcntl(m_ipcServerFd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(m_ipcServerFd, F_SETFL, flags | O_NONBLOCK);
    }
    m_ipcTimer.Start(100);
}

void ObserverFrame::cleanupIpcServer()
{
    m_ipcTimer.Stop();
    if (m_ipcServerFd >= 0) {
        ::close(m_ipcServerFd);
        m_ipcServerFd = -1;
        ::unlink(ipcSocketPath().c_str());
    }
}

void ObserverFrame::onIpcPoll(wxTimerEvent& event)
{
    (void)event;
    if (m_ipcServerFd < 0) {
        return;
    }

    for (;;) {
        int client = ::accept(m_ipcServerFd, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            return;
        }

        char buffer[64] = {};
        const ssize_t n = ::read(client, buffer, sizeof(buffer) - 1);
        ::close(client);
        if (n > 0 && std::string(buffer, static_cast<std::size_t>(n)).find("WAKE") != std::string::npos) {
            if (m_promptOpen) {
                auto* driver = dynamic_cast<WxDialogDriver*>(m_renderDriver.get());
                if (driver != nullptr) {
                    driver->showStatisticsIfActive();
                }
            } else {
                wakePrompt();
            }
        }
    }
}

void ObserverFrame::setupGlobalHotKey()
{
#if defined(__WXGTK__)
    m_hotKeyDisplay = XOpenDisplay(nullptr);
    if (m_hotKeyDisplay == nullptr) {
        return;
    }

    m_hotKeyRoot = DefaultRootWindow(m_hotKeyDisplay);
    m_hotKeyModifiers = Mod4Mask | Mod1Mask;
    m_promptHotKeyCode = XKeysymToKeycode(m_hotKeyDisplay, XK_G);
    m_statsHotKeyCode = XKeysymToKeycode(m_hotKeyDisplay, XK_H);
    if (m_promptHotKeyCode == 0 || m_statsHotKeyCode == 0) {
        cleanupGlobalHotKey();
        return;
    }

    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    XErrorHandler previousHandler = XSetErrorHandler(ignoreXError);
    for (unsigned int mask : masks) {
        XGrabKey(m_hotKeyDisplay, m_promptHotKeyCode, m_hotKeyModifiers | mask, m_hotKeyRoot,
            True, GrabModeAsync, GrabModeAsync);
        XGrabKey(m_hotKeyDisplay, m_statsHotKeyCode, m_hotKeyModifiers | mask, m_hotKeyRoot,
            True, GrabModeAsync, GrabModeAsync);
    }
    XSync(m_hotKeyDisplay, False);
    XSetErrorHandler(previousHandler);
    m_hotKeyRegistered = true;
    m_hotKeyTimer.Start(100);
#endif
}

void ObserverFrame::cleanupGlobalHotKey()
{
#if defined(__WXGTK__)
    if (m_hotKeyDisplay == nullptr) {
        return;
    }

    if (m_hotKeyRegistered) {
        const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
        for (unsigned int mask : masks) {
            XUngrabKey(m_hotKeyDisplay, m_promptHotKeyCode, m_hotKeyModifiers | mask, m_hotKeyRoot);
            XUngrabKey(m_hotKeyDisplay, m_statsHotKeyCode, m_hotKeyModifiers | mask, m_hotKeyRoot);
        }
        m_hotKeyTimer.Stop();
        m_hotKeyRegistered = false;
    }
    XCloseDisplay(m_hotKeyDisplay);
    m_hotKeyDisplay = nullptr;
    m_hotKeyRoot = 0;
    m_promptHotKeyCode = 0;
    m_statsHotKeyCode = 0;
    m_hotKeyModifiers = 0;
#endif
}

void ObserverFrame::onHotKeyPoll(wxTimerEvent& event)
{
    (void)event;
#if defined(__WXGTK__)
    if (m_hotKeyDisplay == nullptr) {
        return;
    }

    while (XPending(m_hotKeyDisplay) > 0) {
        XEvent xEvent;
        XNextEvent(m_hotKeyDisplay, &xEvent);
        if (xEvent.type == KeyPress
            && (xEvent.xkey.state & m_hotKeyModifiers) == m_hotKeyModifiers) {
            if (xEvent.xkey.keycode == m_promptHotKeyCode) {
                triggerPromptFromHotKey();
            } else if (xEvent.xkey.keycode == m_statsHotKeyCode) {
                showStatisticsDialog();
            }
        }
    }
#endif
}

void ObserverFrame::triggerPromptFromHotKey()
{
    wakePrompt();
}

void ObserverFrame::wakePrompt()
{
    if (m_promptOpen) {
        return;
    }

    m_timer.Stop();
    showPrompt();
}

void ObserverFrame::showStatisticsDialog()
{
    if (m_promptOpen) {
        return;
    }
    try {
        StatisticsDialog dialog(this, m_store->loadAll(), appConfig().theme, appConfig().weekStartsMonday);
        dialog.ShowModal();
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error", wxOK | wxICON_ERROR, this);
    }
}

void ObserverFrame::scheduleNextPrompt(int delayMs)
{
    if (m_promptOpen) {
        return;
    }
    m_timer.Stop();
    if (delayMs <= 0) {
        return;
    }
    m_timer.StartOnce(delayMs);
}

void ObserverFrame::exitApp()
{
    Close(true);
    if (wxTheApp != nullptr) {
        wxTheApp->ExitMainLoop();
    }
}

void ObserverFrame::handlePromptClosed(const ObserveResult& result)
{
    m_promptOpen = false;

    if (result.kind == ObserveResultKind::Quit) {
        exitApp();
        return;
    }
    if (m_consecutiveSkips >= appConfig().cancelExitCount) {
        exitApp();
        return;
    }
    if (result.kind == ObserveResultKind::Snoozed) {
        scheduleNextPrompt(SnoozeIntervalMs);
        return;
    }
    if (m_intervalSeconds <= 0.0) {
        exitApp();
        return;
    }
    scheduleNextPrompt(std::max(1, static_cast<int>(m_intervalSeconds * 1000.0)));
}

void ObserverFrame::showPrompt()
{
    if (m_promptOpen) {
        return;
    }

    m_promptOpen = true;
    m_timer.Stop();

    ObservePromptDefaults defaults;
    defaults.energy = DefaultObservationScore;
    defaults.mood = DefaultObservationScore;
    defaults.grounding = DefaultObservationScore;
    defaults.intervalSeconds = m_intervalSeconds;
    defaults.opacityPercent = appConfig().opacityPercent;
    defaults.weekStartsMonday = appConfig().weekStartsMonday;
    defaults.theme = appConfig().theme;
    defaults.quotes = m_quoteProvider.quotes();
    defaults.quoteIndex = m_quoteProvider.randomIndex();
    defaults.quote = defaults.quotes[defaults.quoteIndex];
    try {
        defaults.history = m_store->loadAll();
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error", wxOK | wxICON_ERROR, this);
    }

    ObserveResult result = m_renderDriver->prompt(defaults);
    m_intervalSeconds = result.intervalSeconds;

    if (result.kind == ObserveResultKind::Submitted && result.observation.has_value()) {
        try {
            m_store->save(*result.observation);
        } catch (const std::exception& ex) {
            wxMessageBox(wxString::FromUTF8(ex.what()), "Observer SQLite Error", wxOK | wxICON_ERROR, this);
        }
    }

    if (result.kind == ObserveResultKind::Skipped) {
        ++m_consecutiveSkips;
    } else {
        m_consecutiveSkips = 0;
    }

    handlePromptClosed(result);
}
