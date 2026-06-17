#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <memory>

namespace {

constexpr int kShadowSize = 20;
constexpr int kCornerRadius = 12;

class TransparentPanel : public wxPanel {
  public:
    explicit TransparentPanel(wxWindow *parent) : wxPanel() {
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
        Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    }
};

class TransparentFrame : public wxFrame {
  public:
    TransparentFrame()
        : wxFrame() {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Create(nullptr, wxID_ANY, "Transparent Frame Test", wxDefaultPosition, wxSize(440, 340),
               wxFRAME_NO_TASKBAR | wxBORDER_NONE);

            // SetTransparent(255);

        SetBackgroundColour(wxColour(0, 0, 0, 0));
        Bind(wxEVT_PAINT, &TransparentFrame::onPaint, this);
        Bind(wxEVT_KEY_DOWN, &TransparentFrame::onKeyDown, this);

        auto *shell = new wxBoxSizer(wxVERTICAL);
        auto *content = new TransparentPanel(this);
        auto *inner = new wxBoxSizer(wxVERTICAL);

        auto *title = new wxStaticText(content, wxID_ANY, "Transparent Frame Test");
        title->SetForegroundColour(wxColour(80, 60, 90));
        auto *hint = new wxStaticText(content, wxID_ANY, "Outer area should show desktop through.");
        hint->SetForegroundColour(wxColour(120, 100, 130));
        auto *esc = new wxStaticText(content, wxID_ANY, "Press Esc to close.");
        esc->SetForegroundColour(wxColour(120, 100, 130));

        wxFont font = title->GetFont();
        font.SetPointSize(font.GetPointSize() + 2);
        font.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(font);

        inner->Add(title, 0, wxALL, 24);
        inner->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 24);
        inner->Add(esc, 0, wxLEFT | wxRIGHT | wxBOTTOM, 24);
        content->SetSizer(inner);
        shell->Add(content, 1, wxEXPAND | wxALL, kShadowSize);
        SetSizer(shell);
    }

  private:
    void onKeyDown(wxKeyEvent &event) {
        if (event.GetKeyCode() == WXK_ESCAPE) {
            Close(true);
            return;
        }
        event.Skip();
    }

    void onPaint(wxPaintEvent &event) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        if (size.x <= 0 || size.y <= 0) {
            event.Skip();
            return;
        }

        dc.Clear();

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) {
            event.Skip();
            return;
        }

        const int contentX = kShadowSize;
        const int contentY = kShadowSize;
        const int contentW = size.x - kShadowSize * 2;
        const int contentH = size.y - kShadowSize * 2;
        if (contentW <= 0 || contentH <= 0) {
            event.Skip();
            return;
        }

        gc->SetPen(*wxTRANSPARENT_PEN);
        for (int layer = 0; layer < kShadowSize; ++layer) {
            const int alpha = (kShadowSize - layer) * 2;
            gc->SetBrush(wxColour(0, 0, 0, alpha));
            gc->DrawRoundedRectangle(contentX - layer, contentY - layer, contentW + layer * 2,
                                     contentH + layer * 2, kCornerRadius + layer);
        }

        gc->SetPen(wxPen(wxColour(180, 180, 190), 1));
        gc->SetBrush(wxColour(252, 240, 248));
        gc->DrawRoundedRectangle(contentX, contentY, contentW, contentH, kCornerRadius);

        event.Skip();
    }
};

class TestApp : public wxApp {
  public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto *frame = new TransparentFrame();
        frame->Centre();
        frame->Show(true);
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(TestApp);
