#include "SystemFrame.h"

#include "AppConfig.h"
#include "AppIcon.h"
#include "DeepSeekBrowserFrame.h"
#include "DeepSeekWebViewSetup.h"
#include "HistoryFrame.h"
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

class SystemTrayIcon : public wxTaskBarIcon {
public:
    explicit SystemTrayIcon(SystemFrame* frame)
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
                m_frame->showHistoryFrame();
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
        menu->Append(ID_TRAY_WAKE, wxString::FromUTF8(_("Wake")));
        menu->Append(ID_TRAY_STATS, wxString::FromUTF8(_("Statistics / History")));
        menu->AppendSeparator();
        menu->Append(ID_TRAY_QUIT, wxString::FromUTF8(_("Quit")));
        return menu;
    }

private:
    SystemFrame* m_frame;
};

}

SystemFrame::SystemFrame()
    : wxFrame(nullptr, wxID_ANY, "Observer"),
      m_timer(this, PromptTimerId),
      m_ipcTimer(this, IpcTimerId)
{
    m_intervalSeconds = appConfig().intervalSeconds;
    m_store = createObservationStore();
    m_renderDriver = std::make_unique<WxDialogDriver>(this);
    Bind(wxEVT_TIMER, &SystemFrame::onTimer, this, PromptTimerId);
    Bind(wxEVT_TIMER, &SystemFrame::onIpcPoll, this, IpcTimerId);
    setupIpcServer();
    setupTrayIcon();
    const wxIcon appIcon = observerAppIcon(32);
    if (appIcon.IsOk()) {
        SetIcon(appIcon);
    }
    Hide();
    CallAfter(&SystemFrame::showPrompt);
}

SystemFrame::~SystemFrame()
{
    cleanupTrayIcon();
    cleanupIpcServer();
}

bool SystemFrame::notifyExistingInstance()
{
    const std::string path = ipcSocketPath();
    sockaddr_un addr{};
    if (!fillSocketAddress(addr, path)) {
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
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

bool SystemFrame::hasOpenBrowserWindow() const
{
    return m_browserFrame != nullptr;
}

void SystemFrame::onTimer(wxTimerEvent& event)
{
    (void)event;
    if (m_promptOpen || hasOpenBrowserWindow()) {
        return;
    }
    showPrompt();
}

void SystemFrame::setupTrayIcon()
{
    m_trayIcon = std::make_unique<SystemTrayIcon>(this);
    const wxIcon icon = observerAppIcon(32);
    if (icon.IsOk()) {
        m_trayIcon->SetIcon(icon, wxString::FromUTF8("Observer"));
    }
}

void SystemFrame::cleanupTrayIcon()
{
    if (m_trayIcon != nullptr) {
        m_trayIcon->RemoveIcon();
        m_trayIcon.reset();
    }
}

void SystemFrame::setupIpcServer()
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

void SystemFrame::cleanupIpcServer()
{
    m_ipcTimer.Stop();
    if (m_ipcServerFd >= 0) {
        ::close(m_ipcServerFd);
        m_ipcServerFd = -1;
        ::unlink(ipcSocketPath().c_str());
    }
}

void SystemFrame::onIpcPoll(wxTimerEvent& event)
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
            handleExternalWake();
        }
    }
}

void SystemFrame::handleExternalWake()
{
    CallAfter([this]() {
        if (m_promptOpen) {
            auto* driver = dynamic_cast<WxDialogDriver*>(m_renderDriver.get());
            if (driver != nullptr) {
                driver->requestHistoryIfActive();
            }
            return;
        }

        if (m_browserFrame != nullptr) {
            m_browserFrame->activateWindow();
            return;
        }

        wakePrompt();
    });
}

void SystemFrame::wakePrompt()
{
    if (m_promptOpen) {
        return;
    }
    if (m_browserFrame != nullptr) {
        m_browserFrame->activateWindow();
        return;
    }

    m_timer.Stop();
    showPrompt();
}

void SystemFrame::showBrowserWindow(const wxString &prompt, const wxString &searchQuote)
{
    if (m_browserFrame != nullptr) {
        m_browserFrame->setPrompt(prompt, searchQuote);
        m_browserFrame->activateWindow();
        return;
    }

    auto *frame = new DeepSeekBrowserFrame(nullptr, prompt, searchQuote);
    if (!frame->isReady()) {
        delete frame;
        wxMessageBox("WebView is unavailable on this system.", "DeepSeek", wxOK | wxICON_ERROR, this);
        return;
    }
    m_browserFrame = frame;
    frame->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent &) {
        m_browserFrame = nullptr;
        onBrowserWindowClosed();
    });
    frame->activateWindow();
}

void SystemFrame::onBrowserWindowClosed()
{
    CallAfter([this]() {
        if (m_promptOpen || hasOpenBrowserWindow()) {
            return;
        }
        wakePrompt();
    });
}

void SystemFrame::showHistoryFrame()
{
    if (m_promptOpen) {
        auto* driver = dynamic_cast<WxDialogDriver*>(m_renderDriver.get());
        if (driver != nullptr) {
            driver->requestHistoryIfActive();
        }
        return;
    }

    try {
        HistoryFrame dialog(this, m_store.get(), appConfig().theme, appConfig().weekStartsMonday,
                            m_quoteProvider.quotes());
        dialog.ShowModal();
    } catch (const std::exception &ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error", wxOK | wxICON_ERROR, this);
    }
}

void SystemFrame::scheduleNextPrompt(int delayMs)
{
    if (m_promptOpen || hasOpenBrowserWindow()) {
        return;
    }
    m_timer.Stop();
    if (delayMs <= 0) {
        return;
    }
    m_timer.StartOnce(delayMs);
}

void SystemFrame::exitApp()
{
    Close(true);
    if (wxTheApp != nullptr) {
        wxTheApp->ExitMainLoop();
    }
}

void SystemFrame::handlePromptClosed(const ObserveResult& result)
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

void SystemFrame::showPrompt()
{
    if (m_promptOpen || hasOpenBrowserWindow()) {
        return;
    }

    m_promptOpen = true;
    m_timer.Stop();

    RemindPromptDefaults defaults;
    if (m_savedPromptDefaults.has_value()) {
        defaults = *m_savedPromptDefaults;
        m_savedPromptDefaults.reset();
    } else {
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
        defaults.store = m_store.get();
    }

    for (;;) {
        try {
            m_store->load({});
        } catch (const std::exception& ex) {
            wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error", wxOK | wxICON_ERROR,
                         this);
        }

        ObserveResult result = m_renderDriver->prompt(defaults);
        m_intervalSeconds = result.intervalSeconds;
        if (result.resume.has_value()) {
            defaults = *result.resume;
        }

        if (result.kind == ObserveResultKind::Browser) {
            if (result.resume.has_value()) {
                m_savedPromptDefaults = *result.resume;
            }
            if (result.externalBrowserUrl.has_value()) {
                wxLaunchDefaultBrowser(wxString::FromUTF8(result.externalBrowserUrl->c_str()));
                m_promptOpen = false;
                wakePrompt();
            } else if (result.browserPrompt.has_value()) {
                const wxString searchQuote =
                    result.browserSearchQuote.has_value()
                        ? wxString::FromUTF8(result.browserSearchQuote->c_str())
                        : wxString();
                m_promptOpen = false;
                showBrowserWindow(wxString::FromUTF8(result.browserPrompt->c_str()), searchQuote);
            } else {
                m_promptOpen = false;
            }
            return;
        }

        if (result.kind == ObserveResultKind::Submitted && result.observation.has_value()) {
            try {
                m_store->save(*result.observation);
            } catch (const std::exception& ex) {
                wxMessageBox(wxString::FromUTF8(ex.what()), "Observer SQLite Error", wxOK | wxICON_ERROR,
                             this);
            }
        }

        if (result.kind == ObserveResultKind::Skipped) {
            ++m_consecutiveSkips;
        } else {
            m_consecutiveSkips = 0;
        }

        handlePromptClosed(result);
        break;
    }
}
