#ifndef DEEPSEEK_WEBVIEW_SETUP_H
#define DEEPSEEK_WEBVIEW_SETUP_H

class wxWebView;

void prepareDeepSeekWebViewEnvironment();
void configureDeepSeekWebCache();
void applyDeepSeekWebViewSettings(wxWebView *webView);

#endif
