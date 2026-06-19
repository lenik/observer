#include "LayoutDiag.h"

#include "AppConfig.h"
#include "QuoteProvider.h"
#include "UiTheme.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string escapeTomlString(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    escaped.push_back('"');
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

void writeSnapshot(std::ostream &out, const ObservationLayoutSnapshot &snapshot) {
    out << "quote = " << escapeTomlString(snapshot.quote) << '\n';
    out << "window_width = " << snapshot.windowWidth << '\n';
    out << "window_height = " << snapshot.windowHeight << '\n';
    out << "quote_x = " << snapshot.quoteX << '\n';
    out << "quote_y = " << snapshot.quoteY << '\n';
    out << "quote_width = " << snapshot.quoteWidth << '\n';
    out << "quote_height = " << snapshot.quoteHeight << '\n';
}

} // namespace

int runLayoutDiagnostics(const std::string &outputPath) {
    RemindPromptDefaults defaults;
    defaults.theme = normalizeUiTheme(appConfig().theme);
    if (defaults.theme.empty()) {
        defaults.theme = "light";
    }
    defaults.opacityPercent = appConfig().opacityPercent;
    defaults.intervalSeconds = appConfig().intervalSeconds;
    defaults.weekStartsMonday = appConfig().weekStartsMonday;

    QuoteProvider provider;
    defaults.quotes = provider.quotes();
    defaults.quoteIndex = provider.randomIndex();
    defaults.quote = defaults.quotes[defaults.quoteIndex];

    const ObservationLayoutSnapshot snapshot = RemindDialog::captureLayoutSnapshot(defaults);

    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::fprintf(stderr, "layout diag: cannot write %s\n", outputPath.c_str());
        return 1;
    }

    writeSnapshot(out, snapshot);

    std::fprintf(stdout, "layout diag: wrote %s\n", outputPath.c_str());
    return 0;
}
