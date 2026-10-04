#include "AudioBridge.hpp"
#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"EpicenterDSPWindowsMainWindow";
constexpr wchar_t kWindowTitle[] = L"EpicenterDSP Windows";
constexpr UINT_PTR kHealthTimer = 1;
constexpr UINT kHealthTimerMs = 600;
constexpr UINT_PTR kAnimationTimer = 2;
constexpr UINT kAnimationTimerMs = 16;
constexpr int kClientWidth = 460;
constexpr int kClientHeight = 640;
constexpr wchar_t kCableUrl[] = L"https://vb-audio.com/Cable/";
constexpr float kPi = 3.14159265358979323846f;

struct Colors {
    COLORREF background = RGB(7, 8, 10);
    COLORREF panel = RGB(19, 21, 25);
    COLORREF panelHover = RGB(31, 33, 38);
    COLORREF text = RGB(245, 245, 247);
    COLORREF muted = RGB(145, 147, 153);
    COLORREF track = RGB(54, 56, 62);
    COLORREF accent = RGB(255, 18, 42);
    COLORREF danger = RGB(255, 64, 78);
};

struct AppState {
    HWND window = nullptr;
    AudioBridge bridge;
    Colors colors;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT valueFont = nullptr;
    HFONT buttonFont = nullptr;
    HICON appIcon = nullptr;
    float intensity = 60.0f;
    float displayedIntensity = 60.0f;
    float buttonHoverAmount = 0.0f;
    float activeAmount = 0.0f;
    float pulsePhase = 0.0f;
    bool knobDragging = false;
    int dragStartY = 0;
    float dragStartIntensity = 0.0f;
    bool buttonHover = false;
    bool prerequisitesReady = false;
    bool engineAlertShown = false;
    std::wstring status = L"Comprobando audio…";
};

RECT knobRect(const RECT& client) {
    const int cx = (client.right - client.left) / 2;
    return {cx - 145, 160, cx + 145, 450};
}

RECT buttonRect(const RECT& client) {
    const int cx = (client.right - client.left) / 2;
    return {cx - 170, 507, cx + 170, 566};
}

bool pointInRect(const RECT& rect, int x, int y) {
    POINT point{x, y};
    return PtInRect(&rect, point) != FALSE;
}

Gdiplus::Color gdipColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

COLORREF blendColor(COLORREF from, COLORREF to, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    const auto channel = [amount](BYTE a, BYTE b) {
        return static_cast<BYTE>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * amount));
    };
    return RGB(channel(GetRValue(from), GetRValue(to)),
               channel(GetGValue(from), GetGValue(to)),
               channel(GetBValue(from), GetBValue(to)));
}

void roundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float diameter = radius * 2.0f;
    path.Reset();
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void drawCenteredText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}

void drawKnobGeometry(Gdiplus::Graphics& graphics, const RECT& client, const AppState& app) {
    const int cx = (client.right - client.left) / 2;
    constexpr int cy = 310;
    constexpr float startDegrees = 135.0f;
    constexpr float sweepDegrees = 270.0f;
    constexpr int tickCount = 49;
    const float normalized = std::clamp(app.displayedIntensity / 100.0f, 0.0f, 1.0f);

    const Gdiplus::RectF arcRect(static_cast<float>(cx - 108), static_cast<float>(cy - 108), 216.0f, 216.0f);
    Gdiplus::Pen trackPen(Gdiplus::Color(255, 52, 54, 60), 9.0f);
    trackPen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    graphics.DrawArc(&trackPen, arcRect, startDegrees, sweepDegrees);

    if (normalized > 0.001f) {
        const BYTE pulse = static_cast<BYTE>(42.0f + (app.bridge.isRunning() ? 28.0f * (0.5f + 0.5f * std::sin(app.pulsePhase)) : 0.0f));
        Gdiplus::Pen glowWide(Gdiplus::Color(pulse, 255, 8, 35), 22.0f);
        glowWide.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        graphics.DrawArc(&glowWide, arcRect, startDegrees, sweepDegrees * normalized);
        Gdiplus::Pen glow(Gdiplus::Color(100, 255, 12, 38), 14.0f);
        glow.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        graphics.DrawArc(&glow, arcRect, startDegrees, sweepDegrees * normalized);
        Gdiplus::Pen progress(Gdiplus::Color(255, 255, 18, 42), 8.0f);
        progress.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        graphics.DrawArc(&progress, arcRect, startDegrees, sweepDegrees * normalized);
    }

    for (int i = 0; i < tickCount; ++i) {
        const float progress = static_cast<float>(i) / static_cast<float>(tickCount - 1);
        const float angle = (startDegrees + progress * sweepDegrees) * kPi / 180.0f;
        const float inner = 122.0f;
        const float outer = (i % 4 == 0) ? 134.0f : 130.0f;
        const Gdiplus::PointF from(static_cast<float>(cx) + std::cos(angle) * inner,
                                   static_cast<float>(cy) + std::sin(angle) * inner);
        const Gdiplus::PointF to(static_cast<float>(cx) + std::cos(angle) * outer,
                                 static_cast<float>(cy) + std::sin(angle) * outer);
        const Gdiplus::Color color = progress <= normalized
            ? Gdiplus::Color(235, 255, 18, 42)
            : Gdiplus::Color(255, 64, 66, 72);
        Gdiplus::Pen tick(color, (i % 4 == 0) ? 3.0f : 2.0f);
        tick.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        graphics.DrawLine(&tick, from, to);
    }

    Gdiplus::SolidBrush shadow(Gdiplus::Color(185, 0, 0, 0));
    graphics.FillEllipse(&shadow, static_cast<float>(cx - 88), static_cast<float>(cy - 80), 176.0f, 180.0f);
    Gdiplus::RectF knobOuter(static_cast<float>(cx - 86), static_cast<float>(cy - 86), 172.0f, 172.0f);
    Gdiplus::LinearGradientBrush outerBrush(knobOuter,
        Gdiplus::Color(255, 94, 96, 102), Gdiplus::Color(255, 21, 22, 26), Gdiplus::LinearGradientModeForwardDiagonal);
    graphics.FillEllipse(&outerBrush, knobOuter);
    Gdiplus::Pen outerRim(Gdiplus::Color(255, 116, 118, 124), 2.0f);
    graphics.DrawEllipse(&outerRim, knobOuter);

    Gdiplus::RectF knobFace(static_cast<float>(cx - 75), static_cast<float>(cy - 75), 150.0f, 150.0f);
    Gdiplus::LinearGradientBrush faceBrush(knobFace,
        Gdiplus::Color(255, 42, 44, 50), Gdiplus::Color(255, 12, 13, 16), Gdiplus::LinearGradientModeVertical);
    graphics.FillEllipse(&faceBrush, knobFace);
    Gdiplus::Pen faceRim(Gdiplus::Color(255, 5, 5, 7), 3.0f);
    graphics.DrawEllipse(&faceRim, knobFace);
    Gdiplus::Pen highlight(Gdiplus::Color(110, 214, 216, 220), 2.0f);
    graphics.DrawArc(&highlight, knobFace, 205.0f, 120.0f);

    const float indicatorAngle = (startDegrees + normalized * sweepDegrees) * kPi / 180.0f;
    const Gdiplus::PointF start(static_cast<float>(cx) + std::cos(indicatorAngle) * 31.0f,
                                static_cast<float>(cy) + std::sin(indicatorAngle) * 31.0f);
    const Gdiplus::PointF end(static_cast<float>(cx) + std::cos(indicatorAngle) * 62.0f,
                              static_cast<float>(cy) + std::sin(indicatorAngle) * 62.0f);
    Gdiplus::Pen indicatorShadow(Gdiplus::Color(180, 0, 0, 0), 9.0f);
    indicatorShadow.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    graphics.DrawLine(&indicatorShadow, start, end);
    Gdiplus::Pen indicator(normalized > 0.0f ? Gdiplus::Color(255, 255, 18, 42) : Gdiplus::Color(255, 122, 124, 130), 5.0f);
    indicator.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    graphics.DrawLine(&indicator, start, end);
    Gdiplus::SolidBrush cap(Gdiplus::Color(255, 255, 18, 42));
    if (normalized > 0.0f) graphics.FillEllipse(&cap, static_cast<float>(cx - 4), static_cast<float>(cy - 4), 8.0f, 8.0f);
}

void paintWindow(HWND window, AppState& app) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);

    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    Gdiplus::Graphics graphics(memory);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const Gdiplus::RectF full(0.0f, 0.0f, static_cast<float>(client.right), static_cast<float>(client.bottom));
    Gdiplus::LinearGradientBrush background(full,
        Gdiplus::Color(255, 13, 14, 17), Gdiplus::Color(255, 5, 6, 8), Gdiplus::LinearGradientModeVertical);
    graphics.FillRectangle(&background, full);

    Gdiplus::GraphicsPath headerPath;
    const Gdiplus::RectF header(24.0f, 18.0f, static_cast<float>(client.right - 48), 118.0f);
    roundedPath(headerPath, header, 18.0f);
    Gdiplus::LinearGradientBrush headerBrush(header,
        Gdiplus::Color(255, 26, 27, 31), Gdiplus::Color(255, 13, 14, 17), Gdiplus::LinearGradientModeHorizontal);
    graphics.FillPath(&headerBrush, &headerPath);
    Gdiplus::Pen headerBorder(Gdiplus::Color(255, 55, 56, 62), 1.0f);
    graphics.DrawPath(&headerBorder, &headerPath);
    Gdiplus::Pen brandLine(Gdiplus::Color(255, 255, 18, 42), 2.0f);
    graphics.DrawLine(&brandLine, 146.0f, 107.0f, static_cast<float>(client.right - 45), 107.0f);

    Gdiplus::GraphicsPath cardPath;
    const Gdiplus::RectF card(24.0f, 149.0f, static_cast<float>(client.right - 48), 309.0f);
    roundedPath(cardPath, card, 24.0f);
    Gdiplus::LinearGradientBrush cardBrush(card,
        Gdiplus::Color(255, 21, 22, 26), Gdiplus::Color(255, 10, 11, 14), Gdiplus::LinearGradientModeVertical);
    graphics.FillPath(&cardBrush, &cardPath);
    Gdiplus::Pen cardBorder(Gdiplus::Color(255, 44, 46, 52), 1.0f);
    graphics.DrawPath(&cardBorder, &cardPath);

    drawKnobGeometry(graphics, client, app);

    const RECT button = buttonRect(client);
    const bool active = app.bridge.isRunning();
    const COLORREF baseButton = blendColor(app.colors.panel, RGB(54, 17, 24), app.buttonHoverAmount);
    const COLORREF activeButton = blendColor(baseButton, RGB(157, 7, 30), app.activeAmount);
    const Gdiplus::RectF buttonShape(static_cast<float>(button.left), static_cast<float>(button.top),
                                     static_cast<float>(button.right - button.left), static_cast<float>(button.bottom - button.top));
    Gdiplus::GraphicsPath buttonPath;
    roundedPath(buttonPath, buttonShape, 17.0f);
    Gdiplus::LinearGradientBrush buttonBrush(buttonShape, gdipColor(activeButton),
        gdipColor(blendColor(activeButton, RGB(10, 11, 14), 0.28f)), Gdiplus::LinearGradientModeVertical);
    graphics.FillPath(&buttonBrush, &buttonPath);
    const BYTE buttonBorderAlpha = static_cast<BYTE>(130.0f + 110.0f * std::max(app.buttonHoverAmount, app.activeAmount));
    Gdiplus::Pen buttonBorder(Gdiplus::Color(buttonBorderAlpha, 255, 18, 42), 1.5f);
    graphics.DrawPath(&buttonBorder, &buttonPath);

    const float dotX = 46.0f;
    const float dotY = 481.0f;
    const Gdiplus::Color statusDot = active
        ? Gdiplus::Color(255, 255, 18, 42)
        : (app.prerequisitesReady ? Gdiplus::Color(255, 140, 142, 148) : Gdiplus::Color(255, 255, 64, 78));
    if (active) {
        Gdiplus::SolidBrush dotGlow(Gdiplus::Color(75, 255, 18, 42));
        graphics.FillEllipse(&dotGlow, dotX - 6.0f, dotY - 6.0f, 12.0f, 12.0f);
    }
    Gdiplus::SolidBrush dotBrush(statusDot);
    graphics.FillEllipse(&dotBrush, dotX - 3.0f, dotY - 3.0f, 6.0f, 6.0f);
    graphics.Flush();

    if (app.appIcon) DrawIconEx(memory, 37, 29, app.appIcon, 88, 88, 0, nullptr, DI_NORMAL);
    RECT titleRect{146, 40, client.right - 38, 72};
    drawCenteredText(memory, L"EPICENTER DSP", titleRect, app.titleFont, app.colors.text,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitleRect{146, 72, client.right - 38, 101};
    drawCenteredText(memory, L"WINDOWS · REAL-TIME BASS", subtitleRect, app.bodyFont, app.colors.muted,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT processorLabel{35, 159, client.right - 35, 187};
    drawCenteredText(memory, L"SUBHARMONIC BASS PROCESSOR", processorLabel, app.bodyFont, app.colors.muted);
    const int cx = (client.right - client.left) / 2;
    constexpr int cy = 310;
    wchar_t value[16];
    std::swprintf(value, 16, L"%.0f%%", app.displayedIntensity);
    RECT valueRect{cx - 73, cy - 30, cx + 73, cy + 21};
    drawCenteredText(memory, value, valueRect, app.valueFont, app.colors.text);
    RECT knobLabel{cx - 85, cy + 23, cx + 85, cy + 50};
    drawCenteredText(memory, app.intensity <= 0.0f ? L"BYPASS" : L"INTENSIDAD", knobLabel, app.bodyFont,
                     app.intensity <= 0.0f ? app.colors.muted : app.colors.accent);

    RECT statusRect{58, 465, client.right - 34, 497};
    const COLORREF statusColor = app.bridge.isRunning()
        ? app.colors.accent
        : (app.prerequisitesReady ? app.colors.muted : app.colors.danger);
    drawCenteredText(memory, app.status, statusRect, app.bodyFont, statusColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    drawCenteredText(memory, active ? L"DESACTIVAR" : L"ACTIVAR", button, app.buttonFont,
                     active ? app.colors.text : blendColor(app.colors.muted, app.colors.text, app.buttonHoverAmount));

    RECT footer{24, 572, client.right - 24, 597};
    drawCenteredText(memory, L"0% = BYPASS · RUEDA O ARRASTRA LA PERILLA", footer, app.bodyFont, app.colors.muted);
    RECT labs{24, 606, client.right - 24, 630};
    drawCenteredText(memory, L"By EpicenterDSP Labs", labs, app.buttonFont, app.colors.accent);

    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(window, &paint);
}

void saveIntensity(float intensity) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\EpicenterDSP Windows", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const DWORD value = static_cast<DWORD>(std::lround(intensity));
        RegSetValueExW(key, L"Intensity", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

float loadIntensity() {
    HKEY key = nullptr;
    DWORD value = 60;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\EpicenterDSP Windows", 0, KEY_QUERY_VALUE, &key);
    if (openResult != ERROR_SUCCESS) {
        // Preserve the user's previous knob setting when upgrading the old name.
        openResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\EpicenterWindows", 0, KEY_QUERY_VALUE, &key);
    }
    if (openResult == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"Intensity", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS || type != REG_DWORD) value = 60;
        RegCloseKey(key);
    }
    return static_cast<float>(std::min<DWORD>(value, 100));
}

void setIntensity(AppState& app, float value) {
    app.intensity = std::clamp(std::round(value), 0.0f, 100.0f);
    app.bridge.setIntensity(app.intensity);
    saveIntensity(app.intensity);
    InvalidateRect(app.window, nullptr, FALSE);
}

void refreshProbe(AppState& app) {
    std::wstring detail;
    app.prerequisitesReady = app.bridge.probe(detail);
    app.status = std::move(detail);
    InvalidateRect(app.window, nullptr, FALSE);
}

void openCablePage(HWND owner) {
    ShellExecuteW(owner, L"open", kCableUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

void toggleEngine(AppState& app) {
    if (app.bridge.isRunning()) {
        app.bridge.stop();
        app.engineAlertShown = false;
        refreshProbe(app);
        return;
    }

    std::wstring error;
    if (!app.bridge.start(app.intensity, error)) {
        app.status = error;
        app.prerequisitesReady = false;
        InvalidateRect(app.window, nullptr, FALSE);
        if (error.find(L"VB-CABLE") != std::wstring::npos) {
            const int choice = MessageBoxW(app.window,
                L"Windows necesita un dispositivo virtual para que Epicenter pueda interceptar el audio antes de que llegue a tus bocinas.\n\n"
                L"Instala VB-CABLE desde su página oficial, reinicia Windows una vez y vuelve a pulsar Activar.\n\n"
                L"¿Abrir la página oficial ahora?",
                L"Falta VB-CABLE", MB_ICONINFORMATION | MB_YESNO);
            if (choice == IDYES) openCablePage(app.window);
        } else {
            MessageBoxW(app.window, error.c_str(), L"Epicenter no pudo activarse", MB_ICONERROR | MB_OK);
        }
        return;
    }

    app.prerequisitesReady = true;
    app.status = L"ACTIVO · salida: " + app.bridge.outputName();
    app.engineAlertShown = false;
    InvalidateRect(app.window, nullptr, FALSE);
}

void createFonts(AppState& app, UINT dpi) {
    const auto scaled = [dpi](int points) { return -MulDiv(points, static_cast<int>(dpi), 72); };
    app.titleFont = CreateFontW(scaled(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Bahnschrift");
    app.bodyFont = CreateFontW(scaled(8), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app.valueFont = CreateFontW(scaled(30), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Bahnschrift");
    app.buttonFont = CreateFontW(scaled(10), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Bahnschrift");
}

void destroyFonts(AppState& app) {
    if (app.titleFont) DeleteObject(app.titleFont);
    if (app.bodyFont) DeleteObject(app.bodyFont);
    if (app.valueFont) DeleteObject(app.valueFont);
    if (app.buttonFont) DeleteObject(app.buttonFont);
    app.titleFont = app.bodyFont = app.valueFont = app.buttonFont = nullptr;
}

void updateAnimations(AppState& app) {
    const float previousIntensity = app.displayedIntensity;
    const float previousHover = app.buttonHoverAmount;
    const float previousActive = app.activeAmount;
    app.displayedIntensity += (app.intensity - app.displayedIntensity) * 0.16f;
    if (std::fabs(app.displayedIntensity - app.intensity) < 0.02f) app.displayedIntensity = app.intensity;
    const float hoverTarget = app.buttonHover ? 1.0f : 0.0f;
    app.buttonHoverAmount += (hoverTarget - app.buttonHoverAmount) * 0.18f;
    const float activeTarget = app.bridge.isRunning() ? 1.0f : 0.0f;
    app.activeAmount += (activeTarget - app.activeAmount) * 0.14f;
    app.pulsePhase += 0.065f;
    if (app.pulsePhase > kPi * 2.0f) app.pulsePhase -= kPi * 2.0f;

    const bool changed = std::fabs(previousIntensity - app.displayedIntensity) > 0.001f ||
                         std::fabs(previousHover - app.buttonHoverAmount) > 0.001f ||
                         std::fabs(previousActive - app.activeAmount) > 0.001f ||
                         app.bridge.isRunning();
    if (changed) InvalidateRect(app.window, nullptr, FALSE);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppState*>(create->lpCreateParams);
            app->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            return TRUE;
        }
        case WM_CREATE: {
            if (!app) return -1;
            const UINT dpi = GetDpiForWindow(window);
            createFonts(*app, dpi);
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
            const DWORD corner = 2;
            DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
            const COLORREF captionColor = RGB(8, 9, 11);
            const COLORREF borderColor = RGB(82, 13, 25);
            const COLORREF captionText = RGB(220, 220, 224);
            DwmSetWindowAttribute(window, 35, &captionColor, sizeof(captionColor));
            DwmSetWindowAttribute(window, 34, &borderColor, sizeof(borderColor));
            DwmSetWindowAttribute(window, 36, &captionText, sizeof(captionText));
            SetTimer(window, kHealthTimer, kHealthTimerMs, nullptr);
            SetTimer(window, kAnimationTimer, kAnimationTimerMs, nullptr);
            refreshProbe(*app);
            return 0;
        }
        case WM_PAINT:
            if (app) paintWindow(window, *app);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN: {
            if (!app) break;
            RECT client{};
            GetClientRect(window, &client);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (pointInRect(knobRect(client), x, y)) {
                app->knobDragging = true;
                app->dragStartY = y;
                app->dragStartIntensity = app->intensity;
                SetCapture(window);
                return 0;
            }
            if (pointInRect(buttonRect(client), x, y)) {
                toggleEngine(*app);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (!app) break;
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (app->knobDragging) {
                setIntensity(*app, app->dragStartIntensity + static_cast<float>(app->dragStartY - y) * 0.55f);
                return 0;
            }
            RECT client{};
            GetClientRect(window, &client);
            const bool hovered = pointInRect(buttonRect(client), x, y);
            if (hovered != app->buttonHover) {
                app->buttonHover = hovered;
                InvalidateRect(window, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (app && app->buttonHover) {
                app->buttonHover = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (app && app->knobDragging) {
                app->knobDragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (app) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                setIntensity(*app, app->intensity + static_cast<float>(delta * 3));
            }
            return 0;
        case WM_KEYDOWN:
            if (app && (wParam == VK_UP || wParam == VK_RIGHT)) setIntensity(*app, app->intensity + 1.0f);
            else if (app && (wParam == VK_DOWN || wParam == VK_LEFT)) setIntensity(*app, app->intensity - 1.0f);
            else if (app && wParam == VK_SPACE) toggleEngine(*app);
            else break;
            return 0;
        case WM_TIMER:
            if (app && wParam == kAnimationTimer) {
                updateAnimations(*app);
                return 0;
            }
            if (app && wParam == kHealthTimer && app->bridge.isRunning() && !app->bridge.isHealthy()) {
                const std::wstring error = app->bridge.lastError();
                app->bridge.stop();
                app->status = error.empty() ? L"El motor de audio se detuvo." : error;
                InvalidateRect(window, nullptr, FALSE);
                if (!app->engineAlertShown) {
                    app->engineAlertShown = true;
                    MessageBoxW(window, app->status.c_str(), L"Epicenter se desactivó", MB_ICONERROR | MB_OK);
                }
            }
            return 0;
        case WM_DPICHANGED:
            if (app) {
                destroyFonts(*app);
                createFonts(*app, HIWORD(wParam));
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        case WM_CLOSE:
            if (app) app->bridge.stop();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (app) {
                KillTimer(window, kHealthTimer);
                KillTimer(window, kAnimationTimer);
                destroyFonts(*app);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void startWatchdog() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return;
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --watchdog " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    if (commandLine && std::wcsncmp(commandLine, L"--watchdog ", 11) == 0) {
        const unsigned long pid = std::wcstoul(commandLine + 11, nullptr, 10);
        return AudioBridge::runWatchdog(pid);
    }

    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\EpicenterDSPWindows.Singleton.1");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) {
        MessageBoxW(nullptr, L"No se pudo iniciar el sistema de audio de Windows.", kWindowTitle, MB_ICONERROR | MB_OK);
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    AudioBridge::recoverStaleRouting();
    startWatchdog();
    SetCurrentProcessExplicitAppUserModelID(L"EpicenterDSP.Windows");

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"No se pudo iniciar el render de la interfaz.", kWindowTitle, MB_ICONERROR | MB_OK);
        CoUninitialize();
        if (mutex) CloseHandle(mutex);
        return 2;
    }

    HICON appIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                                  256, 256, LR_DEFAULTCOLOR));
    HICON appIconSmall = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                                       16, 16, LR_DEFAULTCOLOR));

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = appIcon;
    windowClass.hIconSm = appIconSmall;
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        if (appIcon) DestroyIcon(appIcon);
        if (appIconSmall) DestroyIcon(appIconSmall);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        if (mutex) CloseHandle(mutex);
        return 2;
    }

    AppState app;
    app.intensity = loadIntensity();
    app.displayedIntensity = app.intensity;
    app.appIcon = appIcon;
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT desired{0, 0, kClientWidth, kClientHeight};
    AdjustWindowRectExForDpi(&desired, style, FALSE, 0, GetDpiForSystem());
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    const int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    const int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, style,
                                  x, y, width, height, nullptr, nullptr, instance, &app);
    if (!window) {
        if (appIcon) DestroyIcon(appIcon);
        if (appIconSmall) DestroyIcon(appIconSmall);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        if (mutex) CloseHandle(mutex);
        return 3;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (appIcon) DestroyIcon(appIcon);
    if (appIconSmall) DestroyIcon(appIconSmall);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return static_cast<int>(message.wParam);
}
