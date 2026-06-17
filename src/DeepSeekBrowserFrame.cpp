#include "DeepSeekBrowserFrame.h"

#include "DeepSeekWebViewSetup.h"

#include <wx/sizer.h>
#include <wx/webview.h>

namespace {

constexpr int kMaxRpaAttempts = 60;
constexpr int kRpaIntervalMs = 750;
constexpr int kRpaSettleMs = 3000;
constexpr const char *kDeepSeekChatUrl = "https://chat.deepseek.com/";

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

bool isDeepSeekChatReadyUrl(const wxString &url) {
    if (!url.Contains("deepseek.com")) {
        return false;
    }
    if (url.Contains("login") || url.Contains("signin") || url.Contains("sign_in") ||
        url.Contains("passport") || url.Contains("auth")) {
        return false;
    }
    return true;
}

bool pageLooksLikeLoginGate(wxWebView *webView) {
    if (webView == nullptr) {
        return false;
    }
    const wxString title = webView->GetCurrentTitle().Lower();
    if (title.Contains("login") || title.Contains(wxString::FromUTF8("登录")) ||
        title.Contains(wxString::FromUTF8("验证"))) {
        return true;
    }
    return false;
}

} // namespace

DeepSeekBrowserFrame::DeepSeekBrowserFrame(const wxString &prompt)
    : wxFrame(nullptr, wxID_ANY, "DeepSeek", wxDefaultPosition, wxSize(960, 720),
              wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
      m_settleTimer(this),
      m_rpaTimer(this) {
    m_prompt = prompt;

    auto *root = new wxBoxSizer(wxVERTICAL);
    m_webView = wxWebView::New(this, wxID_ANY, wxWebViewDefaultURLStr, wxDefaultPosition,
                               wxDefaultSize, wxWebViewBackendWebKit);
    if (m_webView == nullptr) {
        m_webView = wxWebView::New(this, wxID_ANY, wxWebViewDefaultURLStr);
    }

    if (m_webView == nullptr) {
        return;
    }

    applyDeepSeekWebViewSettings(m_webView);
    m_webView->LoadURL(wxString::FromUTF8(kDeepSeekChatUrl));

    root->Add(m_webView, 1, wxEXPAND);
    SetSizer(root);
    SetSize(wxSize(960, 720));
    CentreOnScreen();

    Bind(wxEVT_CLOSE_WINDOW, &DeepSeekBrowserFrame::onClose, this);
    Bind(wxEVT_WEBVIEW_LOADED, &DeepSeekBrowserFrame::onLoaded, this, m_webView->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &DeepSeekBrowserFrame::onNavigated, this, m_webView->GetId());
    m_settleTimer.Bind(wxEVT_TIMER, &DeepSeekBrowserFrame::onSettleTimer, this);
    m_rpaTimer.Bind(wxEVT_TIMER, &DeepSeekBrowserFrame::onTimer, this);
}

void DeepSeekBrowserFrame::setPrompt(const wxString &prompt) {
    if (!prompt.empty()) {
        m_prompt = prompt;
    }
    scheduleRpaAfterLoad();
    activateWindow();
}

void DeepSeekBrowserFrame::activateWindow() {
    if (IsIconized()) {
        Iconize(false);
    }
    Show(true);
    Raise();
    SetFocus();
    if (m_webView != nullptr) {
        m_webView->SetFocus();
    }
}

void DeepSeekBrowserFrame::stopRpa() {
    if (m_settleTimer.IsRunning()) {
        m_settleTimer.Stop();
    }
    if (m_rpaTimer.IsRunning()) {
        m_rpaTimer.Stop();
    }
}

void DeepSeekBrowserFrame::scheduleRpaAfterLoad() {
    stopRpa();
    m_attempts = 0;
    if (m_webView == nullptr) {
        return;
    }
    if (!isDeepSeekChatReadyUrl(m_webView->GetCurrentURL()) || pageLooksLikeLoginGate(m_webView)) {
        return;
    }
    m_webView->RunScript("window.__oremindDeepSeekSent = false;");
    m_settleTimer.Start(kRpaSettleMs, wxTIMER_ONE_SHOT);
}

void DeepSeekBrowserFrame::startRpaAttempts() {
    m_attempts = 0;
    m_rpaTimer.Start(1, wxTIMER_ONE_SHOT);
}

void DeepSeekBrowserFrame::onClose(wxCloseEvent &event) {
    event.Skip();
    stopRpa();
    Destroy();
}

void DeepSeekBrowserFrame::onLoaded(wxWebViewEvent &event) {
    event.Skip();
    if (m_webView == nullptr) {
        return;
    }

    const wxString url = event.GetURL();
    if (!url.Contains("deepseek.com")) {
        return;
    }

    m_webView->SetFocus();
    if (isDeepSeekChatReadyUrl(url) && !pageLooksLikeLoginGate(m_webView)) {
        scheduleRpaAfterLoad();
    } else {
        stopRpa();
    }
}

void DeepSeekBrowserFrame::onNavigated(wxWebViewEvent &event) {
    event.Skip();
    if (m_webView == nullptr) {
        return;
    }

    const wxString url = event.GetURL();
    if (!url.Contains("deepseek.com")) {
        stopRpa();
        return;
    }

    // SPA may navigate before the chat UI is ready; wait for onLoaded instead.
    stopRpa();
}

void DeepSeekBrowserFrame::onSettleTimer(wxTimerEvent &) {
    if (m_webView == nullptr) {
        stopRpa();
        return;
    }
    if (!isDeepSeekChatReadyUrl(m_webView->GetCurrentURL()) || pageLooksLikeLoginGate(m_webView)) {
        stopRpa();
        return;
    }
    startRpaAttempts();
}

void DeepSeekBrowserFrame::onTimer(wxTimerEvent &) {
    if (m_webView == nullptr) {
        stopRpa();
        return;
    }
    if (!isDeepSeekChatReadyUrl(m_webView->GetCurrentURL()) || pageLooksLikeLoginGate(m_webView)) {
        stopRpa();
        return;
    }

    if (trySubmitPrompt()) {
        stopRpa();
        return;
    }

    if (++m_attempts >= kMaxRpaAttempts) {
        stopRpa();
        return;
    }

    m_rpaTimer.Start(kRpaIntervalMs, wxTIMER_ONE_SHOT);
}

wxString DeepSeekBrowserFrame::buildSubmitScript() const {
    const wxString escaped = EscapeForJsString(m_prompt);
    return wxString::Format(
        R"js(
(function() {
  if (window.__oremindDeepSeekSent) {
    return "done";
  }
  const prompt = "%s";
  function visible(el) {
    if (!el) {
      return false;
    }
    const style = window.getComputedStyle(el);
    return style.display !== "none" && style.visibility !== "hidden" && el.offsetParent !== null;
  }
  const blockers = Array.from(document.querySelectorAll(
    'iframe[src*="captcha"], iframe[src*="geetest"], iframe[src*="recaptcha"], ' +
    '[class*="captcha"], [id*="captcha"], .geetest, [data-testid*="captcha"]'));
  if (blockers.some(visible)) {
    return "blocked";
  }
  function setNativeValue(input, value) {
    input.focus();
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
  }
  const inputs = Array.from(document.querySelectorAll(
    'textarea, input[type="text"], [contenteditable="true"], [contenteditable=""], [role="textbox"]'));
  const candidates = inputs.filter(visible).sort(function(a, b) {
    const ra = a.getBoundingClientRect();
    const rb = b.getBoundingClientRect();
    return (rb.bottom - ra.bottom) || (rb.width - ra.width);
  });
  const input = candidates[0];
  if (!input) {
    return "pending";
  }
  const current = ("value" in input ? input.value : input.textContent || "").trim();
  if (current && current !== prompt) {
    window.__oremindDeepSeekSent = true;
    return "user";
  }
  setNativeValue(input, prompt);
  const buttons = Array.from(document.querySelectorAll('button, [role="button"]'));
  const sendBtn = buttons.find(function(btn) {
    if (!visible(btn) || btn.disabled) {
      return false;
    }
    const label = (btn.innerText || btn.getAttribute("aria-label") ||
      btn.getAttribute("title") || "").trim();
    return /发送|send|submit/i.test(label);
  });
  if (sendBtn) {
    sendBtn.click();
    window.__oremindDeepSeekSent = true;
    return "sent";
  }
  const container = input.closest("form") || input.parentElement;
  if (container) {
    const nearby = Array.from(container.querySelectorAll("button"))
      .filter(function(btn) { return visible(btn) && !btn.disabled; });
    if (nearby.length > 0) {
      nearby[nearby.length - 1].click();
      window.__oremindDeepSeekSent = true;
      return "sent";
    }
  }
  return "filled";
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
        return false;
    }
    return handleRpaResult(result);
}

bool DeepSeekBrowserFrame::handleRpaResult(const wxString &result) {
    wxString status(result);
    status.Trim(true).Trim(false);
    status.MakeLower();
    return status == "sent" || status == "done" || status == "user";
}
