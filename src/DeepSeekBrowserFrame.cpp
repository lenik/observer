#include "DeepSeekBrowserFrame.h"

#include "DeepSeekWebViewSetup.h"

#include <wx/sizer.h>
#include <wx/statusbr.h>

#include <cstdio>

namespace {

constexpr int kMaxRpaAttempts = 90;
constexpr int kRpaIntervalMs = 750;
constexpr int kRpaInitialDelayMs = 2000;
constexpr int kLoginWatchIntervalMs = 2500;
constexpr int kMaxLoginWatchTicks = 60;
constexpr bool kDeepSeekSubmitEnabled = true;
constexpr const char *kDeepSeekChatUrl = "https://chat.deepseek.com/";

bool isAuthPath(const wxString &url) {
    const wxString lower = url.Lower();
    return lower.Contains("sign_in") || lower.Contains("signin") || lower.Contains("/login") ||
           lower.Contains("passport") || lower.Contains("/auth");
}

wxString EscapeForJsString(const wxString &text) {
    wxString escaped;
    escaped.reserve(text.length() + 16);
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it) {
        const wchar_t ch = *it;
        switch (ch) {
        case L'\\':
            escaped += "\\\\";
            break;
        case L'"':
            escaped += "\\\"";
            break;
        case L'\n':
            escaped += "\\n";
            break;
        case L'\r':
            escaped += "\\r";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

bool pageLooksLikeLoginGate(DeepSeekWebViewPane *webView, DeepSeekPageMode pageMode) {
    if (webView == nullptr) {
        return false;
    }
    if (pageMode == DeepSeekPageMode::ChatReady) {
        return false;
    }
    const wxString url = webView->GetCurrentURL().Lower();
    if (isAuthPath(url)) {
        return pageMode != DeepSeekPageMode::ChatReady;
    }
    if (pageMode == DeepSeekPageMode::LoginForm) {
        return true;
    }
    const wxString title = webView->GetCurrentTitle().Lower();
    if (title.Contains("login") || title.Contains("sign in") ||
        title.Contains(wxString::FromUTF8("登录"))) {
        return true;
    }
    return false;
}

bool shouldRunRpa(DeepSeekWebViewPane *webView, DeepSeekPageMode pageMode) {
    if (webView == nullptr) {
        return false;
    }
    if (pageMode == DeepSeekPageMode::ChatReady) {
        return webView->GetCurrentURL().Contains("deepseek.com");
    }
    if (pageLooksLikeLoginGate(webView, pageMode)) {
        return false;
    }
    return webView->GetCurrentURL().Contains("deepseek.com");
}

wxString sanitizeForStatusText(wxString text) {
    wxString out;
    out.reserve(text.length());
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it) {
        const wchar_t ch = *it;
        if (ch >= 0xD800 && ch <= 0xDFFF) {
            continue;
        }
        if (ch == 0xFFFE || ch == 0xFFFF || ch > 0x10FFFF) {
            continue;
        }
        if ((ch >= 0xFDD0 && ch <= 0xFDEF) || (ch & 0xFFFE) == 0xFFFE) {
            continue;
        }
        out += ch;
    }
    return out;
}

wxString promptSearchPrefix(const wxString &text) {
    wxString subject = text;
    subject.Trim(true).Trim(false);
    if (subject.length() <= 7) {
        return subject;
    }
    return subject.substr(0, 7);
}

bool isBlankWebUrl(const wxString &url) {
    return url.empty() || url == wxString("about:blank");
}

wxString buildChatReadyProbeScript() {
    return wxString(
        R"js(
(function() {
  function visible(el) {
    if (!el) return false;
    const s = window.getComputedStyle(el);
    if (s.display === "none" || s.visibility === "hidden" || s.opacity === "0") return false;
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }
  const chat = document.querySelector("textarea#chat-input,#chat-input,textarea[data-testid='chat-input']");
  if (chat && visible(chat)) return "chat-ready";
  const anyTa = Array.from(document.querySelectorAll("textarea")).filter(visible);
  if (anyTa.length > 0) return "chat-ready";
  const ce = Array.from(document.querySelectorAll('[contenteditable="true"],[role="textbox"]')).filter(visible);
  if (ce.length > 0) return "chat-ready";
  const login = document.querySelector(
    'input[type="tel"], input[type="password"], input[placeholder*="phone"], input[placeholder*="手机"], input[placeholder*="验证码"]');
  if (login && visible(login)) return "login-form";
  return "unknown";
})();
)js");
}

DeepSeekPageMode parseChatReadyProbe(const wxString &raw) {
    wxString key(raw);
    key.Trim(true).Trim(false);
    if (key.length() >= 2 && key.StartsWith('"') && key.EndsWith('"')) {
        key = key.Mid(1, key.length() - 2);
    }
    key.MakeLower();
    if (key == "chat-ready") {
        return DeepSeekPageMode::ChatReady;
    }
    if (key == "login-form") {
        return DeepSeekPageMode::LoginForm;
    }
    return DeepSeekPageMode::Unknown;
}

} // namespace

DeepSeekBrowserFrame::DeepSeekBrowserFrame(wxWindow *parent, const wxString &prompt,
                                           const wxString &searchQuote)
    : wxFrame(parent, wxID_ANY, "DeepSeek", wxDefaultPosition, wxSize(960, 720),
              wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
      m_rpaTimer(this),
      m_loginWatchTimer(this) {
    m_prompt = prompt;
    m_searchQuote = searchQuote;

    auto *root = new wxBoxSizer(wxVERTICAL);
    m_webView = new DeepSeekWebViewPane(this, wxID_ANY);

    if (m_webView == nullptr || !m_webView->isReady()) {
        return;
    }

    root->Add(m_webView, 1, wxEXPAND);
    SetSizer(root);
    CreateStatusBar(1);
    m_statusBar = GetStatusBar();
    if (m_statusBar != nullptr) {
        m_statusBar->Bind(wxEVT_LEFT_DOWN, &DeepSeekBrowserFrame::onStatusBarClick, this);
    }
    SetSize(wxSize(960, 720));
    CentreOnScreen();
    refreshStatusBar();

    Bind(wxEVT_CLOSE_WINDOW, &DeepSeekBrowserFrame::onClose, this);
    Bind(wxEVT_SHOW, &DeepSeekBrowserFrame::onShow, this);
    Bind(wxEVT_CHAR_HOOK, &DeepSeekBrowserFrame::onCharHook, this);
    m_webView->Bind(wxEVT_CHAR_HOOK, &DeepSeekBrowserFrame::onCharHook, this);
    m_webView->SetEventHandlers(
        [this](const wxString &url) { onLoaded(url); },
        [this](const wxString &message) { onError(message); },
        [this]() { onTitleChanged(); });
    Bind(wxEVT_TIMER, &DeepSeekBrowserFrame::onRpaTimer, this, m_rpaTimer.GetId());
    Bind(wxEVT_TIMER, &DeepSeekBrowserFrame::onLoginWatchTimer, this, m_loginWatchTimer.GetId());

    CallAfter([this]() {
        Raise();
        if (m_webView != nullptr) {
            m_webView->SetFocus();
        }
    });
}

void DeepSeekBrowserFrame::setPrompt(const wxString &prompt, const wxString &searchQuote) {
    if (prompt.empty()) {
        return;
    }
    const bool promptChanged = prompt != m_prompt || searchQuote != m_searchQuote;
    m_prompt = prompt;
    m_searchQuote = searchQuote;
    if (m_rpaFinished && !promptChanged) {
        refreshStatusBar();
        return;
    }
    if (promptChanged) {
        m_rpaFinished = false;
        m_rpaGaveUp = false;
        m_historySearchDone = false;
        scheduleRpaAfterLoad(true, true);
    } else if (!m_rpaFinished && !m_rpaGaveUp) {
        scheduleRpaAfterLoad(false, false);
    }
    handleAuthPageTransition();
    refreshStatusBar();
}

void DeepSeekBrowserFrame::activateWindow() {
    if (IsIconized()) {
        Iconize(false);
    }
    Show(true);
    Raise();
    SetFocus();
    ensureDeepSeekLoaded();
    if (m_webView != nullptr) {
        m_webView->SetFocus();
    }
}

void DeepSeekBrowserFrame::ensureDeepSeekLoaded() {
    if (m_webView == nullptr) {
        return;
    }
    const wxString url = m_webView->GetCurrentURL();
    if (!isBlankWebUrl(url) && url.Contains("deepseek.com")) {
        return;
    }
    m_webView->LoadURL(wxString::FromUTF8(kDeepSeekChatUrl));
    refreshStatusBar();
}

void DeepSeekBrowserFrame::onShow(wxShowEvent &event) {
    if (event.IsShown()) {
        ensureDeepSeekLoaded();
    }
    event.Skip();
}

void DeepSeekBrowserFrame::stopRpa() {
    m_rpaInitialWait = false;
    if (m_rpaTimer.IsRunning()) {
        m_rpaTimer.Stop();
    }
    refreshStatusBar();
}

void DeepSeekBrowserFrame::stopLoginWatch() {
    m_loginWatchTicks = 0;
    if (m_loginWatchTimer.IsRunning()) {
        m_loginWatchTimer.Stop();
    }
}

void DeepSeekBrowserFrame::startLoginWatch() {
    if (m_loginWatchTimer.IsRunning()) {
        return;
    }
    m_loginWatchTicks = 0;
    m_loginWatchTimer.Start(kLoginWatchIntervalMs, wxTIMER_ONE_SHOT);
    refreshStatusBar();
}

void DeepSeekBrowserFrame::promoteToChatPage() {
    if (m_webView == nullptr) {
        return;
    }
    m_webView->LoadURL(wxString::FromUTF8(kDeepSeekChatUrl));
    refreshStatusBar();
}

wxString DeepSeekBrowserFrame::buildChatReadyProbeScript() const {
    return ::buildChatReadyProbeScript();
}

bool DeepSeekBrowserFrame::updateChatReadyProbe() {
    if (m_webView == nullptr) {
        return false;
    }
    wxString result;
    if (!m_webView->RunScript(buildChatReadyProbeScript(), &result)) {
        return false;
    }
    m_pageMode = parseChatReadyProbe(result);
    return true;
}

void DeepSeekBrowserFrame::handleAuthPageTransition() {
    if (m_webView == nullptr) {
        return;
    }
    updateChatReadyProbe();
    const wxString url = m_webView->GetCurrentURL();
    if (!isAuthPath(url) && m_pageMode != DeepSeekPageMode::LoginForm) {
        stopLoginWatch();
        if (!m_rpaCycleActive && !m_rpaFinished && !m_rpaGaveUp && shouldRunRpa(m_webView, m_pageMode)) {
            scheduleRpaAfterLoad();
        }
        return;
    }

    if (m_pageMode == DeepSeekPageMode::ChatReady) {
        stopLoginWatch();
        promoteToChatPage();
        if (!m_rpaFinished && !m_rpaGaveUp) {
            scheduleRpaAfterLoad(true);
        }
        return;
    }

    startLoginWatch();
    promoteToChatPage();
    refreshStatusBar();
}

void DeepSeekBrowserFrame::onLoginWatchTimer(wxTimerEvent &) {
    if (m_webView == nullptr) {
        stopLoginWatch();
        return;
    }

    ++m_loginWatchTicks;
    updateChatReadyProbe();

    if (m_pageMode == DeepSeekPageMode::ChatReady || !isAuthPath(m_webView->GetCurrentURL())) {
        stopLoginWatch();
        if (!isAuthPath(m_webView->GetCurrentURL()) && !m_rpaFinished && !m_rpaGaveUp) {
            scheduleRpaAfterLoad(true);
        } else {
            promoteToChatPage();
            scheduleRpaAfterLoad(true);
        }
        refreshStatusBar();
        return;
    }

    promoteToChatPage();

    if (m_loginWatchTicks >= kMaxLoginWatchTicks) {
        stopLoginWatch();
        refreshStatusBar();
        return;
    }

    m_loginWatchTimer.Start(kLoginWatchIntervalMs, wxTIMER_ONE_SHOT);
    refreshStatusBar();
}

wxString DeepSeekBrowserFrame::scriptResultHint(const wxString &result) const {
    wxString key(result);
    key.Trim(true).Trim(false);
    key.MakeLower();
    if (key == "pending") {
        return wxString::FromUTF8("未找到可见的聊天输入框");
    }
    if (key == "blocked") {
        return wxString::FromUTF8("检测到验证码/人机验证，需人工处理");
    }
    if (key == "filled") {
        return wxString::FromUTF8("文本已写入，发送按钮/Enter 未确认成功");
    }
    if (key == "sent") {
        return wxString::FromUTF8("已发送（仅尝试一次，不再重复）");
    }
    if (key == "done") {
        return wxString::FromUTF8("此前已成功注入，跳过重复发送");
    }
    if (key == "opened") {
        return wxString::FromUTF8("已打开匹配的历史对话（前7字相同）");
    }
    if (key == "not-found") {
        return wxString::FromUTF8("历史未找到，已关闭搜索并将发送提问");
    }
    if (key == "history-pending") {
        return wxString::FromUTF8("正在搜索历史对话…");
    }
    if (key == "page-pending") {
        return wxString::FromUTF8("等待聊天页渲染完成（就绪后再搜索）");
    }
    if (key == "search-clicked") {
        return wxString::FromUTF8("已点击搜索图标，等待搜索框出现");
    }
    if (key == "search-hotkey") {
        return wxString::FromUTF8("已发送 Ctrl+K（原生按键），等待搜索面板出现");
    }
    if (key == "search-waiting") {
        return wxString::FromUTF8("已发送 Ctrl+K，等待搜索面板渲染（不再重复按键）");
    }
    if (key == "need-focus") {
        return wxString::FromUTF8("请切换到 DeepSeek 窗口（不会自动置顶）");
    }
    if (key == "search-typed") {
        return wxString::FromUTF8("已在搜索框输入前7字，等待匹配结果");
    }
    if (key == "search-results-pending") {
        return wxString::FromUTF8("等待搜索结果（暂无相关结果时将关闭搜索并提问）");
    }
    if (key == "match-clicked") {
        return wxString::FromUTF8("已点击匹配的搜索结果，等待打开对话");
    }
    if (key == "search-no-trigger") {
        return wxString::FromUTF8("尚未找到搜索图标（放大镜），继续重试");
    }
    if (key == "sidebar-expanding") {
        return wxString::FromUTF8("侧边栏已收起，正在点击展开…");
    }
    if (key == "user") {
        return wxString::FromUTF8("输入框已有其他内容，避免覆盖用户输入");
    }
    if (key == "script-error") {
        return wxString::FromUTF8("RunScript 失败（页面未就绪或 JS 异常）");
    }
    if (key.empty() || key == "-") {
        return wxString::FromUTF8("尚无脚本结果");
    }
    return wxString::FromUTF8("未知状态: ") + sanitizeForStatusText(result);
}

wxString DeepSeekBrowserFrame::currentPhaseDescription() const {
    if (m_webView == nullptr) {
        return wxString::FromUTF8("WebView 不可用");
    }
    if (m_prompt.empty()) {
        return wxString::FromUTF8("无待注入问题（prompt 为空）");
    }
    if (pageLooksLikeLoginGate(m_webView, m_pageMode)) {
        if (m_pageMode == DeepSeekPageMode::ChatReady || m_loginWatchTimer.IsRunning()) {
            return wxString::FromUTF8("已登录，正在进入聊天页…");
        }
        return wxString::FromUTF8("等待登录：请在浏览器内完成登录，登录后自动注入");
    }
    if (m_rpaFinished) {
        if (!kDeepSeekSubmitEnabled && m_historySearchDone) {
            const wxString result = m_lastScriptResult.Lower();
            if (result == "opened") {
                return wxString::FromUTF8("已打开历史对话（发送已暂停，待指令）");
            }
            if (result == "not-found") {
                return wxString::FromUTF8("历史搜索未找到（发送已暂停，待指令）");
            }
            return wxString::FromUTF8("历史搜索阶段结束（发送已暂停，待指令）");
        }
        return wxString::FromUTF8("已完成");
    }
    if (m_rpaGaveUp) {
        return wxString::FromUTF8("已放弃（达到最大重试次数）");
    }
    if (!m_historySearchDone && m_rpaCycleActive) {
        return wxString::FromUTF8("搜索历史对话（匹配问题前7字）…");
    }
    if (isBlankWebUrl(m_webView->GetCurrentURL()) ||
        !m_webView->GetCurrentURL().Contains("deepseek.com")) {
        return wxString::FromUTF8("正在加载 https://chat.deepseek.com/ …");
    }
    if (m_rpaInitialWait && m_rpaTimer.IsRunning()) {
        return wxString::Format(wxString::FromUTF8("等待页面就绪（初始延迟 %d ms，等待 SPA 渲染）"),
                                kRpaInitialDelayMs);
    }
    if (m_rpaTimer.IsRunning()) {
        if (!kDeepSeekSubmitEnabled) {
            return wxString::Format(wxString::FromUTF8("历史搜索重试 %d/%d（间隔 %d ms）"),
                                    m_attempts + 1, kMaxRpaAttempts, kRpaIntervalMs);
        }
        return wxString::Format(wxString::FromUTF8("注入重试中 %d/%d（间隔 %d ms）"),
                                m_attempts + 1, kMaxRpaAttempts, kRpaIntervalMs);
    }
    if (m_rpaCycleActive && m_attempts > 0) {
        if (!kDeepSeekSubmitEnabled) {
            return wxString::Format(wxString::FromUTF8("历史搜索重试 %d/%d（间隔 %d ms）"),
                                    m_attempts + 1, kMaxRpaAttempts, kRpaIntervalMs);
        }
        return wxString::Format(wxString::FromUTF8("注入重试中 %d/%d（间隔 %d ms）"),
                                m_attempts + 1, kMaxRpaAttempts, kRpaIntervalMs);
    }
    if (!shouldRunRpa(m_webView, m_pageMode)) {
        return wxString::FromUTF8("空闲（当前 URL 不在 DeepSeek 域）");
    }
    return wxString::FromUTF8("空闲（等待 LOADED/TITLE 事件触发调度）");
}

void DeepSeekBrowserFrame::refreshStatusBar() {
    if (m_statusBar == nullptr) {
        return;
    }

    const wxString phase = currentPhaseDescription();
    const wxString scriptResult =
        m_lastScriptResult.empty() ? wxString("-") : sanitizeForStatusText(m_lastScriptResult);
    const wxString scriptHint = scriptResultHint(m_lastScriptResult);

    wxString url;
    wxString title;
    if (m_webView != nullptr) {
        url = sanitizeForStatusText(m_webView->GetCurrentURL());
        title = sanitizeForStatusText(m_webView->GetCurrentTitle());
    }
    if (url.length() > 72) {
        url = url.substr(0, 69) + "...";
    }
    if (title.length() > 48) {
        title = title.substr(0, 45) + "...";
    }

    wxString timers;
    timers << (m_rpaInitialWait && m_rpaTimer.IsRunning() ? wxString("settle+ ") : wxString())
           << (m_rpaTimer.IsRunning() && !m_rpaInitialWait ? wxString("retry+") : wxString());
    if (timers.empty()) {
        timers = "-";
    }

    const wxString loginFlag =
        m_webView != nullptr && pageLooksLikeLoginGate(m_webView, m_pageMode) ? wxString("yes")
                                                                              : wxString("no");

    wxString promptPreview = sanitizeForStatusText(m_prompt);
    promptPreview.Replace("\n", "\\n");
    promptPreview.Replace("\r", "");
    if (promptPreview.length() > 40) {
        promptPreview = promptPreview.substr(0, 37) + "...";
    }

    const wxString probe =
        m_lastProbeSummary.empty() ? wxString("-") : sanitizeForStatusText(m_lastProbeSummary);

    const wxString text = wxString::Format(
        wxString::FromUTF8("阶段: %s | 脚本: %s - %s | 探测: %s | 尝试 %d/%d | 定时器: %s | "
                           "登录页: %s | 问题 %zu 字符 \"%s\" | URL: %s | 标题: %s"),
        phase, scriptResult, scriptHint, probe, m_attempts, kMaxRpaAttempts, timers, loginFlag,
        static_cast<size_t>(m_prompt.length()), promptPreview,
        url.empty() ? wxString("-") : url, title.empty() ? wxString("-") : title);

    m_statusBar->SetStatusText(text, 0);
}

void DeepSeekBrowserFrame::scheduleRpaAfterLoad(bool force, bool resetSentFlags) {
    if (m_webView == nullptr || m_prompt.empty()) {
        refreshStatusBar();
        return;
    }
    if (m_rpaFinished) {
        refreshStatusBar();
        return;
    }
    if (force) {
        m_rpaCycleActive = false;
    }
    if (!force && (m_rpaCycleActive || m_rpaFinished || m_rpaGaveUp)) {
        return;
    }
    if (!force && (m_rpaTimer.IsRunning() || m_rpaInitialWait)) {
        return;
    }
    if (!shouldRunRpa(m_webView, m_pageMode)) {
        handleAuthPageTransition();
        refreshStatusBar();
        return;
    }
    stopRpa();
    m_attempts = 0;
    if (resetSentFlags) {
        m_rpaFinished = false;
        m_rpaGaveUp = false;
        m_historySearchDone = false;
        m_searchHotkeySent = false;
        m_searchHotkeyWaitTicks = 0;
        m_webView->RunScript(
            "window.__oremindDeepSeekSent = false; window.__oremindDeepSeekSendAttempted = false; "
            "window.__oremindDeepSeekHistoryDone = false; window.__oremindDeepSeekHistoryOpened = false; "
            "window.__oremindDeepSeekSearchOpened = false; window.__oremindDeepSeekSearchTyped = false; "
            "window.__oremindDeepSeekHotkeySent = false; window.__oremindDeepSeekSearchWait = 0; "
            "window.__oremindDeepSeekMatchClicked = false; window.__oremindDeepSeekMatchWait = 0;");
    }
    m_rpaCycleActive = true;
    m_rpaInitialWait = true;
    if (resetSentFlags) {
        m_lastScriptResult.clear();
        m_lastProbeSummary.clear();
    }
    m_rpaTimer.Start(kRpaInitialDelayMs, wxTIMER_ONE_SHOT);
    refreshStatusBar();
}

void DeepSeekBrowserFrame::onClose(wxCloseEvent &event) {
    stopRpa();
    stopLoginWatch();
    event.Skip();
}

void DeepSeekBrowserFrame::onCharHook(wxKeyEvent &event) {
    if (event.GetKeyCode() == WXK_ESCAPE) {
        stopRpa();
        Close();
        return;
    }
    event.Skip();
}

void DeepSeekBrowserFrame::onStatusBarClick(wxMouseEvent &event) {
    if (m_statusBar != nullptr) {
        const wxString text = m_statusBar->GetStatusText(0);
        const wxCharBuffer utf8 = text.utf8_str();
        if (utf8.data() != nullptr) {
            std::fwrite(utf8.data(), 1, utf8.length(), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }
    }
    event.Skip();
}

void DeepSeekBrowserFrame::onLoaded(const wxString &url) {
    if (m_webView == nullptr) {
        return;
    }

    const wxString loadedUrl = url.empty() ? m_webView->GetCurrentURL() : url;
    if (!loadedUrl.Contains("deepseek.com") && !isBlankWebUrl(loadedUrl)) {
        return;
    }

    handleAuthPageTransition();
}

void DeepSeekBrowserFrame::onError(const wxString &message) {
    if (m_webView == nullptr) {
        return;
    }
    m_lastScriptResult = wxString::Format("load-error:%s", message.c_str());
    ensureDeepSeekLoaded();
    refreshStatusBar();
}

void DeepSeekBrowserFrame::onTitleChanged() {
    if (m_webView == nullptr) {
        return;
    }
    handleAuthPageTransition();
}

void DeepSeekBrowserFrame::onRpaTimer(wxTimerEvent &) {
    if (m_webView == nullptr) {
        stopRpa();
        refreshStatusBar();
        return;
    }
    if (!m_webView->GetCurrentURL().Contains("deepseek.com")) {
        ensureDeepSeekLoaded();
        if (m_rpaInitialWait) {
            m_rpaTimer.Start(kRpaInitialDelayMs, wxTIMER_ONE_SHOT);
        } else {
            m_rpaTimer.Start(kRpaIntervalMs, wxTIMER_ONE_SHOT);
        }
        refreshStatusBar();
        return;
    }
    if (!shouldRunRpa(m_webView, m_pageMode)) {
        stopRpa();
        handleAuthPageTransition();
        refreshStatusBar();
        return;
    }

    const bool initialPass = m_rpaInitialWait;
    if (m_rpaInitialWait) {
        m_rpaInitialWait = false;
    }

    wxString probeResult;
    if (m_webView->RunScript(buildProbeScript(), &probeResult)) {
        probeResult.Trim(true).Trim(false);
        if (probeResult.length() >= 2 && probeResult.StartsWith('"') && probeResult.EndsWith('"')) {
            probeResult = probeResult.Mid(1, probeResult.length() - 2);
        }
        m_lastProbeSummary = sanitizeForStatusText(probeResult);
    }

    refreshStatusBar();

    updateChatReadyProbe();

    if (!m_historySearchDone) {
        if (tryHistorySearch()) {
            m_historySearchDone = true;
            m_rpaFinished = true;
            stopRpa();
            refreshStatusBar();
            return;
        }
        const wxString historyStatus = m_lastScriptResult.Lower();
        if (historyStatus == "not-found") {
            m_historySearchDone = true;
            if (!kDeepSeekSubmitEnabled) {
                m_rpaFinished = true;
                stopRpa();
                refreshStatusBar();
                return;
            }
        } else {
            if (!initialPass) {
                ++m_attempts;
            }
            if (m_attempts >= kMaxRpaAttempts) {
                m_rpaGaveUp = true;
                stopRpa();
                refreshStatusBar();
                return;
            }
            m_rpaTimer.Start(kRpaIntervalMs, wxTIMER_ONE_SHOT);
            refreshStatusBar();
            return;
        }
    }

    if (!kDeepSeekSubmitEnabled) {
        refreshStatusBar();
        return;
    }

    if (trySubmitPrompt()) {
        m_rpaFinished = true;
        stopRpa();
        refreshStatusBar();
        return;
    }

    if (!initialPass) {
        ++m_attempts;
    }
    if (m_attempts >= kMaxRpaAttempts) {
        m_rpaGaveUp = true;
        stopRpa();
        refreshStatusBar();
        return;
    }

    m_rpaTimer.Start(kRpaIntervalMs, wxTIMER_ONE_SHOT);
    refreshStatusBar();
}

wxString DeepSeekBrowserFrame::buildHistorySearchScript() const {
    const wxString prefix = EscapeForJsString(promptSearchPrefix(m_searchQuote));
    return wxString::Format(
        R"js(
(function() {
  if (window.__oremindDeepSeekHistoryOpened) {
    return "opened";
  }
  if (window.__oremindDeepSeekHistoryDone) {
    return "not-found";
  }
  const prefix = "%s";
  if (!prefix) {
    window.__oremindDeepSeekHistoryDone = true;
    return "not-found";
  }
  function visible(el) {
    if (!el) return false;
    const s = window.getComputedStyle(el);
    if (s.display === "none" || s.visibility === "hidden" || s.opacity === "0") return false;
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }
  function normalize(text) {
    return (text || "").replace(/\s+/g, " ").trim();
  }
  function titleMatches(title) {
    const t = normalize(title);
    const p = normalize(prefix);
    if (!p || !t) return false;
    const n = Math.min(t.length, p.length);
    if (t.substring(0, n) === p.substring(0, n)) return true;
    const strip = function(s) {
      return s.replace(/[，。！？、；：""''\s,.!?;:'"]/g, "");
    };
    const ts = strip(t);
    const ps = strip(p);
    if (!ts || !ps) return false;
    const m = Math.min(ts.length, ps.length, 7);
    if (m >= 3 && ts.substring(0, m) === ps.substring(0, m)) return true;
    const head = ps.substring(0, Math.min(5, ps.length));
    return head.length >= 3 && ts.indexOf(head) >= 0;
  }
  function isSearchResultNode(node) {
    if (!node || !visible(node)) return false;
    const tag = (node.tagName || "").toUpperCase();
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "BUTTON") return false;
    if (isChatComposerInput(node)) return false;
    const title = nodeTitle(node);
    if (title.length < 2) return false;
    if (/暂无相关结果|无相关结果|未找到相关|no related results|no results found/i.test(title)) {
      return false;
    }
    return true;
  }
  function listSearchResultItems() {
    if (hasSearchEmptyResult()) {
      return [];
    }
    const seen = new Set();
    const items = [];
    const push = function(node) {
      if (!isSearchResultNode(node)) return;
      if (!inSearchPalette(node) && !isSearchPaletteOpen()) return;
      const key = nodeTitle(node) + "|" + node.getBoundingClientRect().top;
      if (seen.has(key)) return;
      seen.add(key);
      items.push({ node: node, title: nodeTitle(node) });
    };
    const selectors = [
      '[cmdk-item]',
      '[role="listbox"] > *',
      '[role="option"]',
      '[class*="search"] li',
      '[class*="search"] [class*="item"]',
      '[class*="result"]',
      'a[href*="/a/chat/"]',
      '[data-value]'
    ];
    for (let s = 0; s < selectors.length; s++) {
      document.querySelectorAll(selectors[s]).forEach(push);
    }
    const root = findSearchInput();
    if (root) {
      const box = root.closest(
        '[role="dialog"], [cmdk-root], [class*="cmdk"], [class*="command"], [class*="palette"], [class*="search"]');
      if (box) {
        box.querySelectorAll("div, li, a, span").forEach(function(node) {
          const title = nodeTitle(node);
          if (title.length >= 4 && title.length <= 120) push(node);
        });
      }
    }
    if (items.length === 0 && window.__oremindDeepSeekSearchTyped) {
      document.querySelectorAll("div, li, a, span").forEach(function(node) {
        if (!visible(node)) return;
        const rect = node.getBoundingClientRect();
        if (rect.top > window.innerHeight * 0.62 || rect.height < 8) return;
        const title = nodeTitle(node);
        if (title.length < 4 || title.length > 120) return;
        if (!titleMatches(title)) return;
        push(node);
      });
    }
    return items;
  }
  function isPageReadyForSearch() {
    if (document.readyState !== "complete") return false;
    let taVis = 0;
    document.querySelectorAll("textarea").forEach(function(t) {
      if (visible(t)) taVis++;
    });
    return taVis >= 1;
  }
  function inSearchPalette(el) {
    if (!el) return false;
    return !!el.closest(
      '[role="dialog"], [data-radix-portal], [data-radix-popper-content-wrapper], ' +
      '[cmdk-root], [class*="cmdk"], [class*="command"], [class*="palette"], [class*="search"]');
  }
  function reactProps(el) {
    const key = Object.keys(el).find(function(k) { return k.indexOf("__reactProps$") === 0; });
    return key ? el[key] : null;
  }
  function clickTarget(el) {
    return el.querySelector(".ds-icon-button__hover-bg") ||
      el.querySelector("svg") ||
      el;
  }
  function clickElement(el) {
    const target = clickTarget(el);
    target.focus();
    target.scrollIntoView({ block: "center", inline: "nearest" });
    let node = el;
    while (node) {
      const props = reactProps(node);
      const fakeEvent = {
        preventDefault: function() {},
        stopPropagation: function() {},
        nativeEvent: {},
        currentTarget: node,
        target: node
      };
      if (props && props.onClick) {
        try { props.onClick(fakeEvent); } catch (err) {}
      }
      node = node.parentElement;
      if (!node || node === document.body) break;
    }
    ["pointerover", "pointerenter", "pointerdown", "mousedown", "pointerup", "mouseup", "click"]
      .forEach(function(type) {
        target.dispatchEvent(new MouseEvent(type, {
          bubbles: true,
          cancelable: true,
          view: window,
          buttons: 1
        }));
        el.dispatchEvent(new MouseEvent(type, {
          bubbles: true,
          cancelable: true,
          view: window,
          buttons: 1
        }));
      });
  }
  function setInputValue(input, value) {
    input.focus();
    input.scrollIntoView({ block: "center", inline: "nearest" });
    const tag = (input.tagName || "").toUpperCase();
    if (tag === "TEXTAREA") {
      const desc = Object.getOwnPropertyDescriptor(window.HTMLTextAreaElement.prototype, "value");
      if (desc && desc.set) desc.set.call(input, value);
      else input.value = value;
    } else {
      const desc = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, "value");
      if (desc && desc.set) desc.set.call(input, value);
      else input.value = value;
    }
    const props = reactProps(input);
    const evt = { target: input, currentTarget: input };
    if (props && props.onChange) {
      try { props.onChange(evt); } catch (err) {}
    }
    input.dispatchEvent(new Event("input", { bubbles: true }));
    input.dispatchEvent(new Event("change", { bubbles: true }));
    input.dispatchEvent(new InputEvent("input", {
      bubbles: true,
      data: value,
      inputType: "insertFromPaste"
    }));
  }
  function isChatComposerInput(input) {
    if (!input) return true;
    if (input.id === "chat-input") return true;
    if (input.closest('main, [class*="chat-input"], [class*="composer"]')) return true;
    const tag = (input.tagName || "").toUpperCase();
    if (tag === "TEXTAREA" &&
        !input.closest('[role="dialog"], [class*="modal"], [class*="overlay"], [class*="command"], [class*="palette"]')) {
      return true;
    }
    const ph = normalize(input.getAttribute("placeholder") || "");
    const aria = normalize(input.getAttribute("aria-label") || "");
    if (/message|消息|发送|send|问|chat/i.test(ph + " " + aria)) return true;
    const rect = input.getBoundingClientRect();
    if (tag === "TEXTAREA" && rect.top > window.innerHeight * 0.45) return true;
    return false;
  }
  function isSearchPaletteOpen() {
    const active = document.activeElement;
    if (active && visible(active) &&
        (active.tagName === "INPUT" || active.tagName === "TEXTAREA") &&
        !isChatComposerInput(active)) {
      return true;
    }
    const shells = document.querySelectorAll(
      '[role="dialog"], [data-radix-portal], [data-radix-popper-content-wrapper], ' +
      '[class*="modal"], [class*="overlay"], [class*="command"], [class*="palette"], ' +
      '[class*="cmdk"], [class*="search"]');
    for (const shell of shells) {
      if (!visible(shell)) continue;
      const input = shell.querySelector(
        'input:not([type="file"]):not([type="hidden"]):not([type="checkbox"]), textarea');
      if (input && visible(input) && !isChatComposerInput(input)) {
        return true;
      }
    }
    const inputs = document.querySelectorAll(
      'input[type="search"], input[type="text"], input:not([type="file"]):not([type="hidden"]):not([type="checkbox"])');
    for (const input of inputs) {
      if (!visible(input) || isChatComposerInput(input)) continue;
      const ph = normalize(input.getAttribute("placeholder") || "");
      const aria = normalize(input.getAttribute("aria-label") || "");
      if (ph.includes("搜索") || ph.includes("search") || aria.includes("搜索") || aria.includes("search")) {
        const rect = input.getBoundingClientRect();
        if (rect.top < window.innerHeight * 0.65 && rect.width >= 80) return true;
      }
    }
    for (const input of inputs) {
      if (!visible(input) || isChatComposerInput(input)) continue;
      const rect = input.getBoundingClientRect();
      if (rect.top < window.innerHeight * 0.35 && rect.width >= 160 && rect.height <= 80) {
        return true;
      }
    }
    return false;
  }
  function findSearchInput() {
    if (!isSearchPaletteOpen()) {
      return null;
    }
    const shells = document.querySelectorAll(
      '[role="dialog"], [class*="modal"], [class*="overlay"], [class*="command"], [class*="palette"], [class*="search"]');
    for (const shell of shells) {
      if (!visible(shell)) continue;
      const input = shell.querySelector(
        'input:not([type="file"]):not([type="hidden"]):not([type="checkbox"]), textarea');
      if (input && visible(input) && !isChatComposerInput(input)) {
        return input;
      }
    }
    const candidates = document.querySelectorAll(
      'input[type="search"], input[type="text"], input:not([type="file"]):not([type="hidden"]):not([type="checkbox"])');
    for (const input of candidates) {
      if (!visible(input) || isChatComposerInput(input)) continue;
      const ph = normalize(input.getAttribute("placeholder") || "");
      const aria = normalize(input.getAttribute("aria-label") || "");
      if (ph.includes("搜索") || ph.includes("search") || aria.includes("搜索") || aria.includes("search")) {
        const rect = input.getBoundingClientRect();
        if (rect.top < window.innerHeight * 0.55) return input;
      }
    }
    return null;
  }
  function hasSearchEmptyResult() {
    if (!window.__oremindDeepSeekSearchTyped && !isSearchPaletteOpen()) {
      return false;
    }
    const bodyText = normalize(
      document.body ? (document.body.innerText || document.body.textContent || "") : "");
    if (bodyText.includes("暂无相关结果") || bodyText.includes("无相关结果") ||
        bodyText.includes("未找到相关")) {
      return true;
    }
    if (isSearchPaletteOpen() &&
        /no related results|no results found/i.test(bodyText)) {
      return true;
    }
    const nodes = document.querySelectorAll(
      '[cmdk-empty], [class*="empty"], [class*="no-result"], [role="status"], p, span, div, li');
    for (const node of nodes) {
      if (!visible(node)) continue;
      const text = normalize(node.innerText || node.textContent || "");
      if (!text) continue;
      if (text.includes("暂无相关结果") || text.includes("无相关结果") ||
          text.includes("未找到相关")) {
        return true;
      }
      if (text.length <= 32 && /no related results|no results found/i.test(text)) {
        return true;
      }
    }
    return false;
  }
  function finishSearchNotFound() {
    window.__oremindDeepSeekMatchClicked = false;
    window.__oremindDeepSeekMatchWait = 0;
    closeSearchPalette();
    window.__oremindDeepSeekHistoryDone = true;
    return "not-found";
  }
  function closeSearchPalette() {
    if (!isSearchPaletteOpen()) return;
    const opts = { key: "Escape", code: "Escape", keyCode: 27, bubbles: true, cancelable: true };
    document.dispatchEvent(new KeyboardEvent("keydown", opts));
    document.dispatchEvent(new KeyboardEvent("keyup", opts));
    if (document.activeElement && document.activeElement.blur) {
      document.activeElement.blur();
    }
  }
  function findSidebarHistoryMatch() {
    const links = document.querySelectorAll('a[href*="/a/chat/"]');
    for (const link of links) {
      if (!visible(link)) continue;
      if (inSearchPalette(link)) continue;
      const title = link.innerText || link.textContent || link.getAttribute("title") || "";
      if (titleMatches(title)) return link;
    }
    return null;
  }
  function nodeTitle(node) {
    return normalize(
      node.getAttribute("data-value") || node.getAttribute("aria-label") ||
      node.getAttribute("title") || node.innerText || node.textContent || "");
  }
  function findSearchResultMatch() {
    const items = listSearchResultItems();
    if (items.length === 0) return null;
    for (let i = 0; i < items.length; i++) {
      if (titleMatches(items[i].title)) return items[i].node;
    }
    if (items.length === 1) return items[0].node;
    let best = null;
    let bestScore = 0;
    const ps = normalize(prefix);
    for (let i = 0; i < items.length; i++) {
      const t = items[i].title;
      let score = 0;
      for (let n = Math.min(7, ps.length, t.length); n >= 3; n--) {
        if (t.indexOf(ps.substring(0, n)) >= 0) {
          score = n;
          break;
        }
      }
      if (score > bestScore) {
        bestScore = score;
        best = items[i].node;
      }
    }
    if (best && bestScore >= 3) return best;
    if (items.length === 1) return items[0].node;
    return null;
  }
  function findMatchingHistoryLink() {
    if (window.__oremindDeepSeekSearchTyped || isSearchPaletteOpen()) {
      return findSearchResultMatch();
    }
    return findSidebarHistoryMatch();
  }
  function clickSearchResult(el) {
    const row = el.closest('[cmdk-item], [role="option"], li, a[href*="/a/chat/"]') || el;
    clickElement(row);
    const target = clickTarget(row);
    ["pointerdown", "mousedown", "pointerup", "mouseup", "click"].forEach(function(type) {
      target.dispatchEvent(new MouseEvent(type, {
        bubbles: true,
        cancelable: true,
        view: window,
        buttons: 1
      }));
    });
    target.dispatchEvent(new KeyboardEvent("keydown", {
      key: "Enter",
      code: "Enter",
      keyCode: 13,
      bubbles: true,
      cancelable: true
    }));
  }
  function openMatchingHistory() {
    const link = findMatchingHistoryLink();
    if (!link) return false;
    clickSearchResult(link);
    window.__oremindDeepSeekMatchClicked = true;
    return true;
  }
  if (!isPageReadyForSearch()) {
    return "page-pending";
  }
  if (!window.__oremindDeepSeekSearchTyped && !isSearchPaletteOpen()) {
    const sidebar = findSidebarHistoryMatch();
    if (sidebar) {
      clickSearchResult(sidebar);
      window.__oremindDeepSeekHistoryOpened = true;
      window.__oremindDeepSeekHistoryDone = true;
      return "opened";
    }
  }

  if (window.__oremindDeepSeekSearchTyped && hasSearchEmptyResult()) {
    return finishSearchNotFound();
  }

  if (window.__oremindDeepSeekMatchClicked) {
    if (hasSearchEmptyResult()) {
      return finishSearchNotFound();
    }
    if (!isSearchPaletteOpen()) {
      window.__oremindDeepSeekHistoryOpened = true;
      window.__oremindDeepSeekHistoryDone = true;
      return "opened";
    }
    window.__oremindDeepSeekMatchWait = (window.__oremindDeepSeekMatchWait || 0) + 1;
    if (window.__oremindDeepSeekMatchWait >= 2) {
      window.__oremindDeepSeekMatchClicked = false;
      window.__oremindDeepSeekMatchWait = 0;
      if (hasSearchEmptyResult()) {
        return finishSearchNotFound();
      }
    }
    return "search-results-pending";
  }

  if (!isSearchPaletteOpen()) {
    if (window.__oremindDeepSeekHotkeySent) {
      return "search-waiting";
    }
    return "search-hotkey";
  }
  window.__oremindDeepSeekHotkeySent = false;

  const searchInput = findSearchInput();
  if (!searchInput) {
    return "history-pending";
  }

  if (!window.__oremindDeepSeekSearchTyped) {
    setInputValue(searchInput, prefix);
    window.__oremindDeepSeekSearchTyped = true;
    window.__oremindDeepSeekSearchWait = 0;
    return "search-typed";
  }

  if (hasSearchEmptyResult()) {
    return finishSearchNotFound();
  }

  if (openMatchingHistory()) {
    window.__oremindDeepSeekMatchWait = 0;
    return "match-clicked";
  }

  window.__oremindDeepSeekSearchWait = (window.__oremindDeepSeekSearchWait || 0) + 1;
  if (window.__oremindDeepSeekSearchWait >= 20) {
    return finishSearchNotFound();
  }
  return "search-results-pending";
})();
)js",
        prefix);
}

bool DeepSeekBrowserFrame::tryHistorySearch() {
    if (m_webView == nullptr || m_prompt.empty()) {
        return false;
    }
    if (m_searchQuote.empty()) {
        m_lastScriptResult = "not-found";
        refreshStatusBar();
        return false;
    }
    if (promptSearchPrefix(m_searchQuote).empty()) {
        m_lastScriptResult = "not-found";
        return false;
    }
    wxString result;
    if (!m_webView->RunScript(buildHistorySearchScript(), &result)) {
        m_lastScriptResult = "script-error";
        refreshStatusBar();
        return false;
    }
    wxString status(result);
    status.Trim(true).Trim(false);
    if (status.length() >= 2 && status.StartsWith('"') && status.EndsWith('"')) {
        status = status.Mid(1, status.length() - 2);
    }
    status.MakeLower();
    if (status == "search-hotkey" && m_searchHotkeySent) {
        status = wxString("search-waiting");
        m_lastScriptResult = status;
    } else {
        m_lastScriptResult = sanitizeForStatusText(status);
    }
    if (status == "page-pending") {
        refreshStatusBar();
        return false;
    }
    if (status == "search-waiting" && m_searchHotkeySent) {
        ++m_searchHotkeyWaitTicks;
        if (m_searchHotkeyWaitTicks >= 8) {
            m_searchHotkeySent = false;
            m_searchHotkeyWaitTicks = 0;
            m_webView->RunScript("window.__oremindDeepSeekHotkeySent=false;", nullptr);
        }
    } else if (status != "search-waiting") {
        m_searchHotkeyWaitTicks = 0;
    }
    if (status == "search-hotkey" && !m_searchHotkeySent) {
        if (!IsActive()) {
            m_lastScriptResult = "need-focus";
        } else {
            m_webView->RunScript(
                "if (document.activeElement && document.activeElement.blur) { document.activeElement.blur(); }",
                nullptr);
            m_webView->SendCtrlK();
            m_webView->RunScript(
                "(function(){"
                "var o={key:'k',code:'KeyK',keyCode:75,ctrlKey:true,bubbles:true,cancelable:true};"
                "document.dispatchEvent(new KeyboardEvent('keydown',o));"
                "document.dispatchEvent(new KeyboardEvent('keyup',o));"
                "window.__oremindDeepSeekHotkeySent=true;"
                "})();",
                nullptr);
            m_searchHotkeySent = true;
            m_searchHotkeyWaitTicks = 0;
        }
    } else if (status == "not-found" && m_searchHotkeySent) {
        m_webView->SendEscape();
    } else if (status == "not-found") {
        m_webView->SendEscape();
    }
    refreshStatusBar();
    return status == "opened";
}

wxString DeepSeekBrowserFrame::buildSubmitScript() const {
    const wxString escaped = EscapeForJsString(m_prompt);
    return wxString::Format(
        R"js(
(function() {
  if (window.__oremindDeepSeekSendAttempted || window.__oremindDeepSeekSent) {
    return "done";
  }
  const prompt = "%s";
  function visible(el) {
    if (!el) {
      return false;
    }
    const style = window.getComputedStyle(el);
    if (style.display === "none" || style.visibility === "hidden" || style.opacity === "0") {
      return false;
    }
    const rect = el.getBoundingClientRect();
    return rect.width > 0 && rect.height > 0;
  }
  function queryAllDeep(root, selector) {
    const out = [];
    function walk(node) {
      if (!node) {
        return;
      }
      if (node.querySelectorAll) {
        node.querySelectorAll(selector).forEach(function(el) { out.push(el); });
      }
      if (node.shadowRoot) {
        walk(node.shadowRoot);
      }
      if (node.children) {
        Array.from(node.children).forEach(walk);
      }
    }
    walk(root || document);
    return out;
  }
  function reactProps(el) {
    const key = Object.keys(el).find(function(k) { return k.indexOf("__reactProps$") === 0; });
    return key ? el[key] : null;
  }
  function readInputValue(input) {
    if ("value" in input) {
      return (input.value || "").trim();
    }
    return (input.textContent || input.innerText || "").trim();
  }
  function messageLooksSubmitted(promptText) {
    const probe = promptText.slice(0, Math.min(32, promptText.length));
    if (!probe) {
      return false;
    }
    const nodes = document.querySelectorAll(
      '[data-message-author-role="user"], [class*="UserMessage"], .user-message');
    for (let i = nodes.length - 1; i >= 0; i--) {
      const text = (nodes[i].textContent || "").trim();
      if (text && text.indexOf(probe) >= 0) {
        return true;
      }
    }
    return false;
  }
  const blockers = Array.from(document.querySelectorAll(
    'iframe[src*="captcha"], iframe[src*="geetest"], iframe[src*="recaptcha"], ' +
    '[class*="captcha"], [id*="captcha"], .geetest, [data-testid*="captcha"]'));
  if (blockers.some(visible)) {
    return "blocked";
  }
  function reactInputEvent(input) {
    return {
      target: input,
      currentTarget: input,
      preventDefault: function() {},
      stopPropagation: function() {},
      nativeEvent: {}
    };
  }
  function setNativeValue(input, value) {
    input.focus();
    input.scrollIntoView({ block: "center", inline: "nearest" });
    const tag = input.tagName;
    if (tag === "TEXTAREA" || tag === "INPUT") {
      const proto = tag === "TEXTAREA"
        ? window.HTMLTextAreaElement.prototype
        : window.HTMLInputElement.prototype;
      const desc = Object.getOwnPropertyDescriptor(proto, "value");
      if (desc && desc.set) {
        desc.set.call(input, value);
      } else {
        input.value = value;
      }
      input.dispatchEvent(new Event("input", { bubbles: true }));
      input.dispatchEvent(new Event("change", { bubbles: true }));
      input.dispatchEvent(new InputEvent("input", {
        bubbles: true,
        data: value,
        inputType: "insertFromPaste"
      }));
    } else {
      input.textContent = "";
      if (document.execCommand) {
        document.execCommand("insertText", false, value);
      } else {
        input.textContent = value;
        input.dispatchEvent(new InputEvent("input", {
          bubbles: true,
          data: value,
          inputType: "insertText"
        }));
      }
    }
    const props = reactProps(input);
    const evt = reactInputEvent(input);
    try {
      if (props && props.onChange) {
        props.onChange(evt);
      } else if (props && props.onInput) {
        props.onInput(evt);
      }
    } catch (err) {}
  }
  function isSendEnabled(el) {
    if (!visible(el)) {
      return false;
    }
    if (el.disabled) {
      return false;
    }
    if (el.getAttribute("aria-disabled") === "true") {
      return false;
    }
    const cls = (el.className && el.className.toString()) || "";
    if (/disabled|inactive|cursor-not-allowed/i.test(cls)) {
      return false;
    }
    const style = window.getComputedStyle(el);
    if (parseFloat(style.opacity) < 0.45) {
      return false;
    }
    if (style.pointerEvents === "none") {
      return false;
    }
    return true;
  }
  function clickElement(el) {
    el.focus();
    el.scrollIntoView({ block: "center", inline: "nearest" });
    const props = reactProps(el);
    const fakeEvent = {
      preventDefault: function() {},
      stopPropagation: function() {},
      nativeEvent: {},
      currentTarget: el,
      target: el
    };
    if (props && props.onClick) {
      try {
        props.onClick(fakeEvent);
      } catch (err) {}
    }
    ["pointerover", "pointerenter", "pointerdown", "mousedown", "pointerup", "mouseup", "click"]
      .forEach(function(type) {
        el.dispatchEvent(new MouseEvent(type, {
          bubbles: true,
          cancelable: true,
          view: window,
          buttons: 1
        }));
      });
  }
  function dispatchEnter(input) {
    input.focus();
    ["keydown", "keypress", "keyup"].forEach(function(type) {
      input.dispatchEvent(new KeyboardEvent(type, {
        key: "Enter",
        code: "Enter",
        keyCode: 13,
        which: 13,
        bubbles: true,
        cancelable: true
      }));
    });
  }
  function findChatInput() {
    const selectors = [
      "textarea#chat-input",
      "#chat-input",
      'textarea[data-testid="chat-input"]',
      'textarea[placeholder*="Message DeepSeek"]',
      'textarea[placeholder*="DeepSeek"]',
      'textarea[placeholder*="Send a message"]',
      'textarea[placeholder*="发送"]',
      'textarea[placeholder*="消息"]',
      'div[contenteditable="true"][role="textbox"]',
      '[role="textbox"][contenteditable="true"]',
      '[contenteditable="true"]',
      "textarea",
      'input[type="text"]'
    ];
    for (const selector of selectors) {
      const matches = queryAllDeep(document, selector);
      for (const match of matches) {
        if (visible(match)) {
          return match;
        }
      }
    }
    const inputs = queryAllDeep(document,
      'textarea, input[type="text"], [contenteditable="true"], [contenteditable=""], [role="textbox"]');
    const candidates = inputs.filter(visible).sort(function(a, b) {
      const ra = a.getBoundingClientRect();
      const rb = b.getBoundingClientRect();
      return (rb.bottom - ra.bottom) || (rb.width - ra.width);
    });
    return candidates[0] || null;
  }
  function findSendNearFileInput() {
    const fileInputs = queryAllDeep(document, 'input[type="file"]');
    for (const fileInput of fileInputs) {
      let container = fileInput.parentElement;
      for (let depth = 0; depth < 6 && container; depth++) {
        const icons = Array.from(container.querySelectorAll(
          'div.ds-icon-button, div[class*="ds-icon-button"], [role="button"]'))
          .filter(function(el) { return !el.querySelector('input[type="file"]'); })
          .filter(isSendEnabled);
        if (icons.length > 0) {
          return icons[icons.length - 1];
        }
        container = container.parentElement;
      }
    }
    return null;
  }
  function findSendButton(input) {
    const nearFile = findSendNearFileInput();
    if (nearFile) {
      return nearFile;
    }
    const selectors = [
      'div.ds-icon-button',
      'div[class*="ds-icon-button"]',
      'input[type="file"] + div',
      'button[aria-label="Send message"]',
      'button[aria-label*="Send"]',
      'button[aria-label*="发送"]',
      'button[data-testid="send-button"]',
      'button[type="submit"]'
    ];
    for (const selector of selectors) {
      const matches = queryAllDeep(document, selector);
      for (const match of matches) {
        if (isSendEnabled(match)) {
          return match;
        }
      }
    }
    let node = input;
    for (let depth = 0; depth < 8 && node; depth++) {
      const icons = Array.from(node.querySelectorAll(
        'div.ds-icon-button, div[class*="ds-icon-button"], button, [role="button"]'))
        .filter(isSendEnabled);
      if (icons.length > 0) {
        return icons[icons.length - 1];
      }
      node = node.parentElement;
    }
    const buttons = queryAllDeep(document, 'button, [role="button"], div[tabindex="0"]');
    return buttons.find(function(btn) {
      if (!isSendEnabled(btn)) {
        return false;
      }
      const label = (btn.innerText || btn.getAttribute("aria-label") ||
        btn.getAttribute("title") || "").trim();
      return /发送|send|submit/i.test(label);
    }) || null;
  }
  const input = findChatInput();
  if (!input) {
    return "pending";
  }
  const current = readInputValue(input);
  if (current && current !== prompt) {
    window.__oremindDeepSeekSent = true;
    window.__oremindDeepSeekSendAttempted = true;
    return "user";
  }
  if (current !== prompt) {
    setNativeValue(input, prompt);
  }
  if (messageLooksSubmitted(prompt)) {
    window.__oremindDeepSeekSent = true;
    window.__oremindDeepSeekSendAttempted = true;
    return "sent";
  }
  window.__oremindDeepSeekSendAttempted = true;
  window.__oremindDeepSeekSent = true;
  const sendBtn = findSendButton(input);
  if (sendBtn) {
    clickElement(sendBtn);
    return "sent";
  }
  dispatchEnter(input);
  return "sent";
})();
)js",
        escaped);
}

bool DeepSeekBrowserFrame::trySubmitPrompt() {
    if (m_webView == nullptr || m_prompt.empty()) {
        return false;
    }
    wxString result;
    if (!m_webView->RunScript(buildSubmitScript(), &result)) {
        m_lastScriptResult = "script-error";
        refreshStatusBar();
        return false;
    }
    wxString status(result);
    status.Trim(true).Trim(false);
    if (status.length() >= 2 && status.StartsWith('"') && status.EndsWith('"')) {
        status = status.Mid(1, status.length() - 2);
    }
    m_lastScriptResult = sanitizeForStatusText(status);
    refreshStatusBar();
    return handleRpaResult(result);
}

bool DeepSeekBrowserFrame::handleRpaResult(const wxString &result) {
    wxString status(result);
    status.Trim(true).Trim(false);
    if (status.length() >= 2 && status.StartsWith('"') && status.EndsWith('"')) {
        status = status.Mid(1, status.length() - 2);
    }
    status.MakeLower();
    m_lastScriptResult = sanitizeForStatusText(status);
    if (status == "sent" || status == "done" || status == "user") {
        refreshStatusBar();
        return true;
    }
    refreshStatusBar();
    return false;
}

wxString DeepSeekBrowserFrame::buildProbeScript() const {
    return wxString(
        R"js(
(function() {
  function visible(el) {
    if (!el) return false;
    const s = window.getComputedStyle(el);
    if (s.display === "none" || s.visibility === "hidden" || s.opacity === "0") return false;
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }
  const ta = document.querySelectorAll("textarea").length;
  const taVis = Array.from(document.querySelectorAll("textarea")).filter(visible).length;
  const chat = document.querySelector("textarea#chat-input,#chat-input");
  const chatVis = chat && visible(chat);
  const ce = Array.from(document.querySelectorAll('[contenteditable="true"]')).filter(visible).length;
  const cap = document.querySelectorAll('iframe[src*="captcha"], .geetest').length;
  const fileIn = document.querySelectorAll('input[type="file"]').length;
  const sendIcons = Array.from(document.querySelectorAll(
    'div.ds-icon-button, div[class*="ds-icon-button"]')).filter(visible).length;
  return "textarea " + ta + "/" + taVis + " visible, #chat-input " + (chatVis ? "yes" : "no") +
         ", contenteditable " + ce + ", send-icon " + sendIcons + ", file-in " + fileIn +
         ", captcha " + cap;
})();
)js");
}
