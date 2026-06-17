#include "AuxGuiProcess.h"

#include "AppConfig.h"
#include "DeepSeekBrowserFrame.h"
#include "HistoryFrame.h"
#include "ObservationStore.h"
#include "QuoteProvider.h"
#include "DeepSeekWebViewSetup.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__unix__)
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

AuxGuiLaunchRequest g_auxGuiLaunchRequest;

namespace {

constexpr int AuxGuiIpcTimerId = wxID_HIGHEST + 400;

std::string auxGuiSocketPath() {
    const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    const std::string dir =
        runtimeDir != nullptr && *runtimeDir != '\0' ? runtimeDir : "/tmp";
    return dir + "/oremind-aux-" + std::to_string(static_cast<long long>(getuid())) + ".sock";
}

bool fillSocketAddress(sockaddr_un &addr, const std::string &path) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return true;
}

wxString writePromptTempFile(const wxString &prompt) {
    wxString path = wxFileName::CreateTempFileName("oremind-aux-deepseek-");
    if (path.empty()) {
        return wxString();
    }

    wxFile file(path, wxFile::write);
    if (!file.IsOpened()) {
        wxRemoveFile(path);
        return wxString();
    }

    const wxCharBuffer bytes = prompt.utf8_str();
    if (bytes.data() == nullptr || file.Write(bytes.data(), bytes.length()) != bytes.length()) {
        file.Close();
        wxRemoveFile(path);
        return wxString();
    }

    return path;
}

bool readPromptArg(const char *arg, wxString &prompt) {
    if (arg == nullptr || *arg == '\0') {
        return false;
    }
    if (arg[0] == '@') {
        wxFile file(wxString::FromUTF8(arg + 1));
        if (!file.IsOpened()) {
            return false;
        }
        if (!file.ReadAll(&prompt)) {
            return false;
        }
        wxRemoveFile(wxString::FromUTF8(arg + 1));
    } else {
        prompt = wxString::FromUTF8(arg);
    }
    return !prompt.empty();
}

#if defined(__unix__)
pid_t g_auxGuiChild = 0;

void reapAuxGuiChildIfExited() {
    if (g_auxGuiChild <= 0) {
        return;
    }
    int status = 0;
    const pid_t result = waitpid(g_auxGuiChild, &status, WNOHANG);
    if (result == g_auxGuiChild) {
        g_auxGuiChild = 0;
    }
}
#endif

bool auxGuiSocketAlive() {
#if defined(__unix__)
    reapAuxGuiChildIfExited();
#endif

    const std::string path = auxGuiSocketPath();
    sockaddr_un addr{};
    if (!fillSocketAddress(addr, path)) {
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    const bool connected =
        ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return connected;
}

bool sendAuxGuiCommand(const std::string &command) {
    const std::string path = auxGuiSocketPath();
    sockaddr_un addr{};
    if (!fillSocketAddress(addr, path)) {
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }

    const ssize_t written = ::write(fd, command.data(), command.size());
    ::close(fd);
    return written == static_cast<ssize_t>(command.size());
}

bool waitForAuxGuiSocket(int attempts = 200) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (auxGuiSocketAlive()) {
            return true;
        }
#if defined(__unix__)
        usleep(10000);
#else
        wxMilliSleep(10);
#endif
    }
    return false;
}

#if defined(__unix__)
bool runAuxGuiChildMain(const char *argv0) {
    g_auxGuiLaunchRequest.active = true;
    g_auxGuiLaunchRequest.initial = AuxGuiLaunchRequest::InitialWindow::None;
    prepareDeepSeekWebViewEnvironment();
    const char *program = (argv0 != nullptr && *argv0 != '\0') ? argv0 : "oremind";
    int wxArgc = 1;
    char *wxArgv[] = {const_cast<char *>(program), nullptr};
    return wxEntry(wxArgc, wxArgv) != 0;
}
#endif

class AuxGuiSession {
  public:
    bool start(const AuxGuiLaunchRequest &request) {
        m_store = createObservationStore();
        if (m_store == nullptr) {
            return false;
        }

        m_holder = new wxFrame(nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(1, 1),
                               wxFRAME_NO_TASKBAR | wxBORDER_NONE);
        m_holder->Hide();
        m_ipcTimer.SetOwner(m_holder, AuxGuiIpcTimerId);
        m_holder->Bind(wxEVT_TIMER, &AuxGuiSession::onIpcPoll, this, AuxGuiIpcTimerId);

        if (!setupIpcServer()) {
            return false;
        }

        switch (request.initial) {
        case AuxGuiLaunchRequest::InitialWindow::History:
            openHistory();
            break;
        case AuxGuiLaunchRequest::InitialWindow::DeepSeek:
            openDeepSeek(request.deepSeekPrompt);
            break;
        case AuxGuiLaunchRequest::InitialWindow::None:
            break;
        }

        if (wxTheApp != nullptr) {
            wxTheApp->SetExitOnFrameDelete(false);
        }
        return true;
    }

  private:
    bool setupIpcServer() {
        const std::string path = auxGuiSocketPath();
        sockaddr_un addr{};
        if (!fillSocketAddress(addr, path)) {
            return false;
        }

        m_ipcServerFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_ipcServerFd < 0) {
            return false;
        }

        ::unlink(path.c_str());
        if (::bind(m_ipcServerFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            ::listen(m_ipcServerFd, 4) != 0) {
            cleanupIpcServer();
            return false;
        }

        const int flags = ::fcntl(m_ipcServerFd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(m_ipcServerFd, F_SETFL, flags | O_NONBLOCK);
        }
        m_ipcTimer.Start(100);
        return true;
    }

    void cleanupIpcServer() {
        m_ipcTimer.Stop();
        if (m_ipcServerFd >= 0) {
            ::close(m_ipcServerFd);
            m_ipcServerFd = -1;
            ::unlink(auxGuiSocketPath().c_str());
        }
    }

    void onIpcPoll(wxTimerEvent &) {
        if (m_ipcServerFd < 0) {
            return;
        }

        for (;;) {
            const int client = ::accept(m_ipcServerFd, nullptr, nullptr);
            if (client < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }
                return;
            }

            char buffer[512] = {};
            const ssize_t bytesRead = ::read(client, buffer, sizeof(buffer) - 1);
            ::close(client);
            if (bytesRead <= 0) {
                continue;
            }
            handleCommand(std::string(buffer, static_cast<std::size_t>(bytesRead)));
        }
    }

    void handleCommand(const std::string &rawCommand) {
        std::string command = rawCommand;
        while (!command.empty() && (command.back() == '\n' || command.back() == '\r')) {
            command.pop_back();
        }
        if (command == "HISTORY") {
            openHistory();
            return;
        }
        if (command.rfind("DEEPSEEK ", 0) == 0) {
            wxString prompt;
            if (readPromptArg(command.c_str() + 9, prompt)) {
                openDeepSeek(prompt);
            }
        }
    }

    void openHistory() {
        if (m_historyFrame != nullptr) {
            m_historyFrame->activateWindow();
            return;
        }

        m_historyFrame = new HistoryFrame(nullptr, m_store.get(), appConfig().theme,
                                          appConfig().weekStartsMonday, m_quotes.quotes());
        m_historyFrame->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent &) {
            m_historyFrame = nullptr;
            maybeExit();
        });
        m_historyFrame->Show(true);
        m_historyFrame->activateWindow();
    }

    void openDeepSeek(const wxString &prompt) {
        if (prompt.empty()) {
            return;
        }

        if (m_deepSeekFrame != nullptr) {
            m_deepSeekFrame->setPrompt(prompt);
            m_deepSeekFrame->activateWindow();
            return;
        }

        m_deepSeekFrame = new DeepSeekBrowserFrame(prompt);
        if (!m_deepSeekFrame->isReady()) {
            wxMessageBox("WebView is unavailable on this system.", "DeepSeek", wxOK | wxICON_ERROR);
            delete m_deepSeekFrame;
            m_deepSeekFrame = nullptr;
            maybeExit();
            return;
        }

        m_deepSeekFrame->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent &) {
            m_deepSeekFrame = nullptr;
            maybeExit();
        });
        m_deepSeekFrame->Show(true);
        m_deepSeekFrame->activateWindow();
    }

    void maybeExit() {
        if (m_historyFrame != nullptr || m_deepSeekFrame != nullptr) {
            return;
        }
        cleanupIpcServer();
        if (wxTheApp != nullptr) {
            wxTheApp->ExitMainLoop();
        }
    }

    wxFrame *m_holder = nullptr;
    wxTimer m_ipcTimer;
    int m_ipcServerFd = -1;
    std::unique_ptr<ObservationStore> m_store;
    QuoteProvider m_quotes;
    HistoryFrame *m_historyFrame = nullptr;
    DeepSeekBrowserFrame *m_deepSeekFrame = nullptr;
};

std::unique_ptr<AuxGuiSession> g_auxGuiSession;

} // namespace

bool parseAuxGuiLaunchRequest(int argc, char **argv, AuxGuiLaunchRequest &request) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--deepseek-browser") == 0) {
            request.active = true;
            request.initial = AuxGuiLaunchRequest::InitialWindow::DeepSeek;
            if (i + 1 >= argc) {
                return false;
            }
            return readPromptArg(argv[i + 1], request.deepSeekPrompt);
        }
        if (std::strcmp(argv[i], "--aux-gui") != 0) {
            continue;
        }

        request.active = true;
        if (i + 1 >= argc) {
            return true;
        }
        if (std::strcmp(argv[i + 1], "history") == 0) {
            request.initial = AuxGuiLaunchRequest::InitialWindow::History;
            return true;
        }
        if (std::strcmp(argv[i + 1], "deepseek") == 0) {
            request.initial = AuxGuiLaunchRequest::InitialWindow::DeepSeek;
            if (i + 2 >= argc) {
                return false;
            }
            return readPromptArg(argv[i + 2], request.deepSeekPrompt);
        }
        return true;
    }
    return true;
}

bool forkAuxGuiDaemon(const char *argv0) {
#if defined(__unix__)
    if (auxGuiSocketAlive()) {
        return true;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        const bool failed = runAuxGuiChildMain(argv0);
        _exit(failed ? 1 : 0);
    }

    g_auxGuiChild = pid;
    return waitForAuxGuiSocket();
#else
    (void)argv0;
    return false;
#endif
}

bool launchAuxGuiHistory() {
    return sendAuxGuiCommand("HISTORY\n");
}

bool launchAuxGuiDeepSeek(const wxString &prompt) {
    if (prompt.empty() || !auxGuiSocketAlive()) {
        return false;
    }

    const wxString promptPath = writePromptTempFile(prompt);
    if (promptPath.empty()) {
        return false;
    }
    const std::string command = "DEEPSEEK @" + promptPath.ToStdString() + "\n";
    return sendAuxGuiCommand(command);
}

bool runAuxGuiSession() {
    g_auxGuiSession = std::make_unique<AuxGuiSession>();
    return g_auxGuiSession->start(g_auxGuiLaunchRequest);
}
