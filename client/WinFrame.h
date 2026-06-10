// AeroMesh custom window frame helper.
//
// Turns the QML window into a borderless-looking window that Windows still
// treats as a normal, DWM-managed top-level window. That is what lets
// Windows 11 show the "Snap Layouts" flyout when the pointer hovers the
// maximize button: we report HTMAXBUTTON for that region from WM_NCHITTEST,
// and we strip the standard title bar in WM_NCCALCSIZE so the QML content
// covers the whole window. We also ask DWM to round the window corners.
//
// The maximize button position is computed natively from the known title-bar
// button layout (three 44x40 buttons in the top-right, 6px right margin) so it
// stays correct regardless of QML animations, transforms or load timing.
//
// All Windows-specific code is guarded by _WIN32; other platforms get a
// no-op stub so the cross-platform build keeps working. ASCII only (the
// project is built without /utf-8).

#pragma once

#include <QObject>
#include <QWindow>

#ifdef _WIN32

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QGuiApplication>

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

// These may be missing on older SDKs; define fallbacks just in case.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

class WinFrame : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit WinFrame(QObject* parent = nullptr) : QObject(parent) {}

    // Attach to the top-level window. Keeps the window a normal Windows window
    // (so snapping, shadows and Snap Layouts work) but removes the visible
    // title bar via WM_NCCALCSIZE and rounds the corners via DWM.
    Q_INVOKABLE void attach(QWindow* window) {
        if (!window)
            return;
        m_window = window;
        m_hwnd = reinterpret_cast<HWND>(window->winId());
        if (!m_hwnd)
            return;

        // Ensure the styles Windows needs for resizing, maximizing and the
        // Snap Layouts affordance are present.
        LONG_PTR style = GetWindowLongPtr(m_hwnd, GWL_STYLE);
        style |= (WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                  WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        SetWindowLongPtr(m_hwnd, GWL_STYLE, style);

        // Ask DWM to round the window corners (Windows 11). Harmless on 10.
        DWORD pref = DWMWCP_ROUND;
        DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref,
                              sizeof(pref));

        qApp->installNativeEventFilter(this);

        // Force WM_NCCALCSIZE so the caption is removed immediately.
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    // Kept for QML/C++ compatibility; the hit region is now computed natively.
    Q_INVOKABLE void setMaxButtonRect(qreal, qreal, qreal, qreal) {}

    bool nativeEventFilter(const QByteArray& type, void* message,
                           qintptr* result) override {
        if (type != QByteArrayLiteral("windows_generic_MSG"))
            return false;
        MSG* msg = static_cast<MSG*>(message);
        if (!m_hwnd || !msg || msg->hwnd != m_hwnd)
            return false;

        switch (msg->message) {
        case WM_NCCALCSIZE: {
            if (msg->wParam == FALSE)
                return false;
            NCCALCSIZE_PARAMS* params =
                reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            RECT& rc = params->rgrc[0];
            if (isMaximized()) {
                // Add the frame back when maximized so content is not clipped
                // and the taskbar stays uncovered.
                const int fx = GetSystemMetrics(SM_CXFRAME) +
                               GetSystemMetrics(SM_CXPADDEDBORDER);
                const int fy = GetSystemMetrics(SM_CYFRAME) +
                               GetSystemMetrics(SM_CXPADDEDBORDER);
                rc.left += fx;
                rc.right -= fx;
                rc.top += fy;
                rc.bottom -= fy;
            }
            // Otherwise leave the rect untouched: the client area covers the
            // whole window (no title bar). Resizing is handled in NCHITTEST.
            if (result)
                *result = 0;
            return true;
        }
        case WM_NCHITTEST: {
            const int gx = GET_X_LPARAM(msg->lParam);
            const int gy = GET_Y_LPARAM(msg->lParam);
            RECT wr;
            if (!GetWindowRect(m_hwnd, &wr))
                return false;
            const int lx = gx - wr.left;          // physical px from left
            const int ly = gy - wr.top;           // physical px from top
            const int w = wr.right - wr.left;
            const int h = wr.bottom - wr.top;
            const qreal dpr =
                m_window ? m_window->devicePixelRatio() : 1.0;
            const bool maximized = isMaximized();

            int fx = 0;
            int fy = 0;
            if (maximized) {
                fx = GetSystemMetrics(SM_CXFRAME) +
                     GetSystemMetrics(SM_CXPADDEDBORDER);
                fy = GetSystemMetrics(SM_CYFRAME) +
                     GetSystemMetrics(SM_CXPADDEDBORDER);
            }

            // Maximize button region, computed from the QML title-bar layout:
            // three 44x40 buttons in the top-right, 6px right margin. The
            // maximize button is the middle one (second from the right).
            // -> HTMAXBUTTON makes Windows 11 show Snap Layouts.
            const int btnW = static_cast<int>(44 * dpr);
            const int btnH = static_cast<int>(40 * dpr);
            const int rightMargin = static_cast<int>(6 * dpr);
            const int bx = (w - fx) - rightMargin - 2 * btnW;
            const int bw = btnW;
            const int by = fy;
            const int bh = btnH;
            if (lx >= bx && lx < bx + bw && ly >= by && ly < by + bh) {
                if (result)
                    *result = HTMAXBUTTON;
                return true;
            }

            if (!maximized) {
                const int border = static_cast<int>(8 * dpr);
                const bool left = lx < border;
                const bool right = lx >= w - border;
                const bool top = ly < border;
                const bool bottom = ly >= h - border;
                if (top && left) {
                    if (result) *result = HTTOPLEFT;
                    return true;
                }
                if (top && right) {
                    if (result) *result = HTTOPRIGHT;
                    return true;
                }
                if (bottom && left) {
                    if (result) *result = HTBOTTOMLEFT;
                    return true;
                }
                if (bottom && right) {
                    if (result) *result = HTBOTTOMRIGHT;
                    return true;
                }
                if (left) {
                    if (result) *result = HTLEFT;
                    return true;
                }
                if (right) {
                    if (result) *result = HTRIGHT;
                    return true;
                }
                if (top) {
                    if (result) *result = HTTOP;
                    return true;
                }
                if (bottom) {
                    if (result) *result = HTBOTTOM;
                    return true;
                }
            }

            // Everything else is client: QML handles dragging, the min/close
            // buttons, and all content.
            if (result)
                *result = HTCLIENT;
            return true;
        }
        case WM_NCLBUTTONDOWN: {
            // Swallow the press on the maximize button; we toggle on release.
            if (msg->wParam == HTMAXBUTTON) {
                if (result)
                    *result = 0;
                return true;
            }
            return false;
        }
        case WM_NCLBUTTONUP: {
            if (msg->wParam == HTMAXBUTTON) {
                ShowWindow(m_hwnd, isMaximized() ? SW_RESTORE : SW_MAXIMIZE);
                if (result)
                    *result = 0;
                return true;
            }
            return false;
        }
        default:
            return false;
        }
    }

private:
    bool isMaximized() const {
        if (!m_hwnd)
            return false;
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(m_hwnd, &wp))
            return wp.showCmd == SW_SHOWMAXIMIZED;
        return false;
    }

    QWindow* m_window = nullptr;
    HWND m_hwnd = nullptr;
};

#else  // non-Windows: no-op stub so the build stays cross-platform.

class WinFrame : public QObject {
    Q_OBJECT
public:
    explicit WinFrame(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void attach(QWindow*) {}
    Q_INVOKABLE void setMaxButtonRect(qreal, qreal, qreal, qreal) {}
};

#endif
