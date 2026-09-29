#include "PinnedWindow.h"

#include "MediaFolder.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr const wchar_t* kWindowClass = L"SnipTextProUltraPinned";

// The frame is drawn rather than supplied by a window style. WS_BORDER
// renders as a single system-coloured hairline that vanishes against a pale
// desktop, and the whole point of a pin is being able to tell where the
// picture stops and the screen behind it starts. Two rings — pale inside,
// dark outside — read against both.
constexpr int      kBorder      = 3;
constexpr COLORREF kBorderInner = RGB(255, 255, 255);
constexpr COLORREF kBorderOuter = RGB(32, 36, 40);

constexpr double kMinScale  = 0.10;
constexpr double kMaxScale  = 4.00;
constexpr double kScaleStep = 1.10;

// Below this a pin is a coloured speck with no grab area left on it, which
// is a quick way to lose one behind the pointer.
constexpr int kMinPixels = 48;

enum : UINT {
    kCmdEdit = 1,
    kCmdCopy,
    kCmdSave,
    kCmdActualSize,
    kCmdCloseOthers,
    kCmdClose,
};

// Every live pin, in creation order. Static rather than held by App because
// LiveCount and CloseAll are asked for while the tray menu is being built,
// and that code has no business owning a pointer to each window.
std::vector<PinnedWindow*>& Registry() {
    static std::vector<PinnedWindow*> registry;
    return registry;
}

} // namespace

// ---------------------------------------------------------------------------

PinnedWindow::PinnedWindow(std::unique_ptr<Bitmap> image,
                           CloseCallback onClose, EditCallback onEdit)
    : image_(std::move(image)),
      onClose_(std::move(onClose)),
      onEdit_(std::move(onEdit)) {}

PinnedWindow::~PinnedWindow() {
    // Normally already gone — the window is destroyed first and the object
    // reaped afterwards. This covers the path where the owner drops the
    // object while the window is still up, which would otherwise leave a
    // topmost window on screen whose proc points at freed memory.
    if (hwnd_) {
        HWND doomed = hwnd_;
        hwnd_ = nullptr;
        ::SetWindowLongPtrW(doomed, GWLP_USERDATA, 0);
        ::DestroyWindow(doomed);
    }
    Registry().erase(std::remove(Registry().begin(), Registry().end(), this),
                     Registry().end());
}

PinnedWindow* PinnedWindow::Open(std::unique_ptr<Bitmap> image,
                                 const RECT& capturedFrom,
                                 CloseCallback onClose,
                                 EditCallback onEdit) {
    if (!image || !image->IsValid()) return nullptr;

    // `new` rather than make_unique: the constructor is private, and
    // make_unique is not a friend. Wrapped immediately so a failed Create
    // cannot leak it.
    std::unique_ptr<PinnedWindow> window(
        new PinnedWindow(std::move(image), std::move(onClose), std::move(onEdit)));
    if (!window->Create(capturedFrom)) return nullptr;

    PinnedWindow* raw = window.release();
    Registry().push_back(raw);
    return raw;
}

int PinnedWindow::LiveCount() {
    return static_cast<int>(Registry().size());
}

void PinnedWindow::CloseAll() {
    // Copied first. Destroying a window runs its close callback, which the
    // owner uses to erase the entry — mutating the container being iterated.
    const std::vector<PinnedWindow*> snapshot = Registry();
    for (PinnedWindow* pin : snapshot) {
        if (pin && pin->hwnd_) ::DestroyWindow(pin->hwnd_);
    }
}

bool PinnedWindow::Create(const RECT& capturedFrom) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW description{};
        description.cbSize      = sizeof(description);
        // DBLCLKS so a double-click can hand the picture to the editor.
        description.style       = CS_DBLCLKS;
        description.lpfnWndProc = &PinnedWindow::WindowProc;
        description.hInstance   = ::GetModuleHandleW(nullptr);
        description.hCursor     = ::LoadCursorW(nullptr, IDC_SIZEALL);
        // No background brush: the window is painted edge to edge, and a
        // brush would show as a flash of grey on every resize.
        description.hbrBackground = nullptr;
        description.lpszClassName = kWindowClass;
        registered = ::RegisterClassExW(&description) != 0;
    }
    if (!registered) return false;

    const int width  = image_->Width()  + kBorder * 2;
    const int height = image_->Height() + kBorder * 2;

    // Opens exactly over the region it was cut from, so the pin appears to
    // lift off the screen in place rather than materialising somewhere else
    // and making you find it. The border is the only offset.
    int x = capturedFrom.left - kBorder;
    int y = capturedFrom.top  - kBorder;

    // Clamped to the monitor the capture came from. A region taken at the
    // very edge of the screen would otherwise put a few pixels of frame off
    // the side, and a pin you cannot grab is a pin you cannot close.
    HMONITOR monitor = ::MonitorFromRect(&capturedFrom, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && ::GetMonitorInfoW(monitor, &info)) {
        x = (std::max)(static_cast<int>(info.rcWork.left),
                       (std::min)(x, static_cast<int>(info.rcWork.right)  - width));
        y = (std::max)(static_cast<int>(info.rcWork.top),
                       (std::min)(y, static_cast<int>(info.rcWork.bottom) - height));
    }

    hwnd_ = ::CreateWindowExW(
        // TOOLWINDOW keeps it out of the taskbar and out of Alt-Tab: a pin is
        // a sticky note, not a document. TOPMOST is the entire feature.
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClass, L"SnipTextProUltra", WS_POPUP,
        x, y, width, height,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    // Kept out of every capture the program takes, the same way the
    // recording indicator is. A pin sits on top of the screen by design, so
    // without this you could not screenshot or record the area underneath
    // one — and the reason you pinned it is usually that you are about to
    // work on what it is covering. Its own pixels are still reachable
    // through Copy and Save on the context menu.
    util::ExcludeFromCapture(hwnd_);

    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return true;
}

// --- message routing -------------------------------------------------------

LRESULT CALLBACK PinnedWindow::WindowProc(HWND hwnd, UINT message,
                                          WPARAM wParam, LPARAM lParam) {
    PinnedWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PinnedWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<PinnedWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT PinnedWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        OnPaint();
        return 0;

    // The whole window is the picture, and the picture is repainted in one
    // blit. Letting Windows erase first would show the desktop through it for
    // a frame on every drag step.
    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        // Brings this pin in front of the other pins. It DOES take focus —
        // no WM_MOUSEACTIVATE handler, so DefWindowProc activates — and
        // that is the deliberate choice: refusing activation would make Esc
        // and Ctrl+C unreachable, and a window you can only close from a
        // context menu is worse than one that costs you your caret when you
        // deliberately click it. A pin never takes focus on its own.
        ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RECT frame{};
        ::GetWindowRect(hwnd_, &frame);
        POINT cursor{};
        ::GetCursorPos(&cursor);
        dragOffset_.x = cursor.x - frame.left;
        dragOffset_.y = cursor.y - frame.top;
        dragging_ = true;
        ::SetCapture(hwnd_);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!dragging_) return 0;
        POINT cursor{};
        ::GetCursorPos(&cursor);
        ::SetWindowPos(hwnd_, nullptr,
                       cursor.x - dragOffset_.x, cursor.y - dragOffset_.y, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ::ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        // Capture can be taken away — by an Alt-Tab, a lock screen, a UAC
        // prompt. Without this the window keeps following the pointer after
        // the button is long since up.
        dragging_ = false;
        return 0;

    case WM_LBUTTONDBLCLK:
        // A double-click also delivered a WM_LBUTTONDOWN, which started a
        // drag that no button-up will now finish.
        if (dragging_) { dragging_ = false; ::ReleaseCapture(); }
        HandToEditor();
        return 0;

    case WM_MOUSEWHEEL: {
        const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (notches == 0) return 0;
        double factor = 1.0;
        for (int i = 0; i < std::abs(notches); ++i) {
            factor *= (notches > 0) ? kScaleStep : (1.0 / kScaleStep);
        }
        POINT cursor{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };   // already screen
        ApplyScale(scale_ * factor, cursor);
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT screen{};
        ::GetCursorPos(&screen);
        ShowContextMenu(screen);
        return 0;
    }

    case WM_KEYDOWN:
        // Reaches the FOCUSED pin, which is the one you last clicked — key
        // messages are not routed by cursor position.
        if (wParam == VK_ESCAPE) { ::DestroyWindow(hwnd_); return 0; }
        if (wParam == 'C' && (::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            CopyToClipboard();
            return 0;
        }
        return 0;

    case WM_DESTROY:
        // Cleared before the callback, so the owner's deferred reap finds an
        // object that no longer believes it owns a window and will not try to
        // destroy it a second time.
        hwnd_ = nullptr;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        Registry().erase(std::remove(Registry().begin(), Registry().end(), this),
                         Registry().end());
        if (onClose_) onClose_(this);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

// --- painting --------------------------------------------------------------

void PinnedWindow::OnPaint() {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(hwnd_, &paint);
    if (!dc) { ::EndPaint(hwnd_, &paint); return; }

    RECT client{};
    ::GetClientRect(hwnd_, &client);

    if (image_ && image_->MemoryDC()) {
        const int pictureWidth  = util::RectWidth(client)  - kBorder * 2;
        const int pictureHeight = util::RectHeight(client) - kBorder * 2;

        if (pictureWidth > 0 && pictureHeight > 0) {
            if (pictureWidth == image_->Width() && pictureHeight == image_->Height()) {
                ::BitBlt(dc, kBorder, kBorder, pictureWidth, pictureHeight,
                         image_->MemoryDC(), 0, 0, SRCCOPY);
            } else {
                // HALFTONE always, at every zoom. The editor learned this the
                // expensive way: COLORONCOLOR drops rows and columns, which
                // deletes one-pixel glyph stems, and a pin is nearly always
                // pinned because it has text on it.
                ::SetStretchBltMode(dc, HALFTONE);
                ::SetBrushOrgEx(dc, 0, 0, nullptr);
                ::StretchBlt(dc, kBorder, kBorder, pictureWidth, pictureHeight,
                             image_->MemoryDC(), 0, 0,
                             image_->Width(), image_->Height(), SRCCOPY);
            }
        }
    }

    // Two rings, drawn as frames so the middle is never touched — the picture
    // is already there and overpainting it would cost a second full blit.
    ScopedBrush outer(::CreateSolidBrush(kBorderOuter));
    ScopedBrush inner(::CreateSolidBrush(kBorderInner));
    if (outer) ::FrameRect(dc, &client, outer.get());
    RECT middle = client;
    ::InflateRect(&middle, -1, -1);
    if (inner) ::FrameRect(dc, &middle, inner.get());
    RECT innermost = middle;
    ::InflateRect(&innermost, -1, -1);
    if (outer) ::FrameRect(dc, &innermost, outer.get());

    ::EndPaint(hwnd_, &paint);
}

// --- actions ---------------------------------------------------------------

void PinnedWindow::ApplyScale(double scale, POINT anchorScreen) {
    if (!image_) return;
    scale = (std::max)(kMinScale, (std::min)(kMaxScale, scale));

    int pictureWidth  = static_cast<int>(std::lround(image_->Width()  * scale));
    int pictureHeight = static_cast<int>(std::lround(image_->Height() * scale));
    if (pictureWidth < kMinPixels || pictureHeight < kMinPixels) {
        // Refuse rather than clamp one axis: clamping would change the aspect
        // ratio, and a pin that distorts as you zoom out is worse than one
        // that simply stops.
        return;
    }

    RECT frame{};
    ::GetWindowRect(hwnd_, &frame);

    // Zoom around the pointer: the pixel under the cursor stays under the
    // cursor. Without this, zooming walks the picture out from under you and
    // you spend the next second dragging it back.
    const double previousWidth  = (std::max)(1, util::RectWidth(frame)  - kBorder * 2);
    const double previousHeight = (std::max)(1, util::RectHeight(frame) - kBorder * 2);
    const double fractionX = (anchorScreen.x - (frame.left + kBorder)) / previousWidth;
    const double fractionY = (anchorScreen.y - (frame.top  + kBorder)) / previousHeight;

    const int left = static_cast<int>(std::lround(anchorScreen.x - fractionX * pictureWidth))
                     - kBorder;
    const int top  = static_cast<int>(std::lround(anchorScreen.y - fractionY * pictureHeight))
                     - kBorder;

    scale_ = scale;
    ::SetWindowPos(hwnd_, nullptr, left, top,
                   pictureWidth + kBorder * 2, pictureHeight + kBorder * 2,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void PinnedWindow::ShowContextMenu(POINT screen) {
    ScopedMenu menu(::CreatePopupMenu());
    if (!menu) return;

    ::AppendMenuW(menu.get(), MF_STRING, kCmdEdit, L"Open in Editor");
    ::AppendMenuW(menu.get(), MF_STRING, kCmdCopy, L"Copy");
    ::AppendMenuW(menu.get(), MF_STRING, kCmdSave, L"Save…");
    ::AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.get(),
                  MF_STRING | (std::fabs(scale_ - 1.0) < 0.001 ? MF_GRAYED : 0),
                  kCmdActualSize,
                  util::Format(L"Actual Size (now %d%%)",
                               static_cast<int>(std::lround(scale_ * 100))).c_str());
    ::AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.get(), MF_STRING | (LiveCount() > 1 ? 0 : MF_GRAYED),
                  kCmdCloseOthers, L"Close Every Other Pin");
    ::AppendMenuW(menu.get(), MF_STRING, kCmdClose, L"Close");

    // The window must be foreground or the menu will not dismiss when you
    // click away from it — the standard tray-menu problem, and this window
    // deliberately never takes focus on its own.
    ::SetForegroundWindow(hwnd_);
    const int choice = ::TrackPopupMenuEx(menu.get(),
                                          TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                          screen.x, screen.y, hwnd_, nullptr);
    switch (choice) {
    case kCmdEdit:        HandToEditor(); break;
    case kCmdCopy:        CopyToClipboard(); break;
    case kCmdSave:        SaveAsPng(); break;
    case kCmdActualSize: {
        RECT frame{};
        ::GetWindowRect(hwnd_, &frame);
        POINT centre{ (frame.left + frame.right) / 2, (frame.top + frame.bottom) / 2 };
        ApplyScale(1.0, centre);
        break;
    }
    case kCmdCloseOthers: {
        const std::vector<PinnedWindow*> snapshot = Registry();
        for (PinnedWindow* pin : snapshot) {
            if (pin && pin != this && pin->hwnd_) ::DestroyWindow(pin->hwnd_);
        }
        break;
    }
    case kCmdClose:       ::DestroyWindow(hwnd_); break;
    default: break;
    }
}

void PinnedWindow::CopyToClipboard() {
    if (!image_) return;
    if (!image_->CopyToClipboard(hwnd_)) ::MessageBeep(MB_ICONWARNING);
}

void PinnedWindow::SaveAsPng() {
    if (!image_) return;
    std::vector<BYTE> png = image_->EncodePng();
    if (png.empty()) { ::MessageBeep(MB_ICONWARNING); return; }

    MediaFolder& folder = MediaFolder::Screenshots();
    folder.EnsureDirectoryExists();

    std::wstring name = L"SnipTextProUltra " + util::FileNameTimestamp() + L".png";
    std::vector<wchar_t> buffer(name.begin(), name.end());
    buffer.resize(MAX_PATH, L'\0');

    const std::wstring initialDirectory = folder.Directory();

    OPENFILENAMEW dialog{};
    dialog.lStructSize     = sizeof(dialog);
    dialog.hwndOwner       = hwnd_;
    dialog.lpstrFilter     = L"PNG image\0*.png\0All files\0*.*\0";
    dialog.lpstrFile       = buffer.data();
    dialog.nMaxFile        = MAX_PATH;
    dialog.lpstrDefExt     = L"png";
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!::GetSaveFileNameW(&dialog)) return;

    ScopedFile file(::CreateFileW(buffer.data(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        ::MessageBoxW(hwnd_, L"That file couldn't be written.", L"SnipTextProUltra",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    DWORD written = 0;
    if (!::WriteFile(file.get(), png.data(), static_cast<DWORD>(png.size()),
                     &written, nullptr) ||
        written != png.size()) {
        ::MessageBoxW(hwnd_, L"The file couldn't be written completely. "
                             L"It may be incomplete — try saving somewhere else.",
                      L"SnipTextProUltra", MB_OK | MB_ICONWARNING);
    }
}

void PinnedWindow::HandToEditor() {
    if (!onEdit_ || !image_) return;

    // A COPY, via the whole-image crop. The pin stays exactly as it was, and
    // the editor owns pixels that closing the pin cannot take away.
    std::unique_ptr<Bitmap> copy =
        image_->Crop(util::MakeRect(0, 0, image_->Width(), image_->Height()));
    if (!copy) { ::MessageBeep(MB_ICONWARNING); return; }

    onEdit_(std::move(copy));
}
