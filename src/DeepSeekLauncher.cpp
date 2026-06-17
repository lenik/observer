#include "DeepSeekLauncher.h"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <cstring>
#include <vector>

#if defined(__unix__)
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

wxString g_deepSeekBrowserPrompt;

namespace {

wxString selfExecutable() {
#if defined(__linux__)
    char resolved[PATH_MAX] = {};
    const ssize_t length = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
    if (length > 0) {
        resolved[length] = '\0';
        return wxString::FromUTF8(resolved);
    }
#endif
    return wxStandardPaths::Get().GetExecutablePath();
}

wxString writePromptTempFile(const wxString &prompt) {
    wxString path = wxFileName::CreateTempFileName("oremind-deepseek-");
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

#if defined(__unix__)
pid_t g_deepSeekChild = 0;

void stopDeepSeekChild() {
    if (g_deepSeekChild <= 0) {
        return;
    }

    if (kill(g_deepSeekChild, 0) == 0) {
        kill(g_deepSeekChild, SIGTERM);
        int status = 0;
        waitpid(g_deepSeekChild, &status, 0);
    }
    g_deepSeekChild = 0;
}
#endif

} // namespace

bool parseDeepSeekBrowserArg(int argc, char **argv, wxString &prompt) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--deepseek-browser") != 0) {
            continue;
        }
        if (i + 1 >= argc) {
            return false;
        }

        const char *arg = argv[i + 1];
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
    return false;
}

bool launchDeepSeekBrowser(const wxString &prompt) {
    if (prompt.empty()) {
        return false;
    }

    const wxString promptPath = writePromptTempFile(prompt);
    if (promptPath.empty()) {
        return false;
    }

    const wxString executable = selfExecutable();
    const wxString promptArg = wxString::Format("@%s", promptPath);

#if defined(__unix__)
    stopDeepSeekChild();

    const pid_t pid = fork();
    if (pid < 0) {
        wxRemoveFile(promptPath);
        return false;
    }
    if (pid == 0) {
        const wxCharBuffer exeBytes = executable.utf8_str();
        const wxCharBuffer argBytes = promptArg.utf8_str();
        execl(exeBytes.data(), exeBytes.data(), "--deepseek-browser", argBytes.data(), nullptr);
        _exit(1);
    }

    g_deepSeekChild = pid;
    return true;
#else
    wxArrayString args;
    args.Add(executable);
    args.Add("--deepseek-browser");
    args.Add(promptArg);

    std::vector<wxCharBuffer> buffers;
    std::vector<char *> argv;
    buffers.reserve(args.size());
    argv.reserve(args.size() + 1);
    for (const wxString &arg : args) {
        buffers.push_back(arg.utf8_str());
        argv.push_back(buffers.back().data());
    }
    argv.push_back(nullptr);

    return wxExecute(argv.data(), wxEXEC_ASYNC) > 0;
#endif
}
