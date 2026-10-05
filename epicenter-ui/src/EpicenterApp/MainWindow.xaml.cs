using System.Runtime.InteropServices;
using Windows.System;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using EpicenterApp.Models;
using EpicenterApp.ViewModels;

namespace EpicenterApp;

public sealed partial class MainWindow : Window
{
    public MainViewModel ViewModel { get; } = new();

    public MainWindow()
    {
        InitializeComponent();

        Title = "EpicenterDSP Windows — Professional";
        ConfigureBrandWindow();

        // Keyboard navigation across the entire application
        if (Content is UIElement rootElement)
        {
            rootElement.KeyDown += OnRootKeyDown;
        }

        Closed += (_, _) => ViewModel.Dispose();
    }

    private void ConfigureBrandWindow()
    {
        // Use the physical working area so centering also works with taskbars,
        // secondary screens and non-100% display scaling.
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var dpi = GetDpiForWindow(hwnd);
        var scale = dpi == 0 ? 1.0 : dpi / 96.0;
        var display = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest);
        if (display is not null)
        {
            var work = display.WorkArea;
            var margin = (int)Math.Round(16 * scale);
            var width = Math.Max(1, Math.Min((int)Math.Round(920 * scale), work.Width - 2 * margin));
            var height = Math.Max(1, Math.Min((int)Math.Round(750 * scale), work.Height - 2 * margin));
            AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(
                work.X + (work.Width - width) / 2,
                work.Y + (work.Height - height) / 2, width, height));
        }

        var iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcon.ico");
        if (System.IO.File.Exists(iconPath)) AppWindow.SetIcon(iconPath);

        if (AppWindowTitleBar.IsCustomizationSupported())
        {
            var titleBar = AppWindow.TitleBar;
            var black = Windows.UI.Color.FromArgb(255, 15, 15, 15);
            var gray = Windows.UI.Color.FromArgb(255, 168, 168, 168);
            titleBar.BackgroundColor = black;
            titleBar.ForegroundColor = Colors.White;
            titleBar.ButtonBackgroundColor = black;
            titleBar.ButtonForegroundColor = Colors.White;
            titleBar.InactiveBackgroundColor = black;
            titleBar.InactiveForegroundColor = gray;
            titleBar.ButtonInactiveBackgroundColor = black;
            titleBar.ButtonInactiveForegroundColor = gray;
            titleBar.ButtonHoverBackgroundColor = Windows.UI.Color.FromArgb(255, 48, 48, 48);
            titleBar.ButtonPressedBackgroundColor = Windows.UI.Color.FromArgb(255, 255, 0, 38);
        }
    }

    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern uint GetDpiForWindow(nint hwnd);

    private void OnKnobValueChanged(object? sender, double value) => ViewModel.Intensity = value;

    // The two power buttons are selectors, not two identical toggle buttons.
    private void OnPowerOffClick(object sender, RoutedEventArgs e)
    {
        if (ViewModel.IsEnabled) ViewModel.TogglePower();
    }

    private void OnPowerOnClick(object sender, RoutedEventArgs e)
    {
        if (!ViewModel.IsEnabled) ViewModel.TogglePower();
    }

    private void OnRootKeyDown(object sender, KeyRoutedEventArgs e)
    {
        // Escape closes the drawer if open
        if (e.Key == VirtualKey.Escape && ViewModel.IsDrawerOpen)
        {
            ViewModel.CloseDrawer();
            e.Handled = true;
            return;
        }

        switch (e.Key)
        {
            case VirtualKey.Space:
                ViewModel.TogglePower();
                e.Handled = true;
                break;
            case VirtualKey.A:
                ViewModel.SelectOriginalA();
                e.Handled = true;
                break;
            case VirtualKey.B:
                ViewModel.SelectEpicenterB();
                e.Handled = true;
                break;
            case VirtualKey.Up:
            case VirtualKey.Right:
                ViewModel.AdjustIntensity(+1.0);
                e.Handled = true;
                break;
            case VirtualKey.Down:
            case VirtualKey.Left:
                ViewModel.AdjustIntensity(-1.0);
                e.Handled = true;
                break;
            case VirtualKey.PageUp:
                ViewModel.AdjustIntensity(+10.0);
                e.Handled = true;
                break;
            case VirtualKey.PageDown:
                ViewModel.AdjustIntensity(-10.0);
                e.Handled = true;
                break;
        }
    }

    // Header navigation handlers
    private void OnHeaderPresetsButtonClick(object sender, RoutedEventArgs e) => ViewModel.ToggleDrawer(0);
    private void OnHeaderSettingsButtonClick(object sender, RoutedEventArgs e) => ViewModel.ToggleDrawer(1);
    private void OnHeaderStatusBadgeClick(object sender, RoutedEventArgs e) => ViewModel.ToggleDrawer(2);

    // Drawer tab switch handlers
    private void OnSelectTab0Click(object sender, RoutedEventArgs e) => ViewModel.OpenDrawer(0);
    private void OnSelectTab1Click(object sender, RoutedEventArgs e) => ViewModel.OpenDrawer(1);
    private void OnSelectTab2Click(object sender, RoutedEventArgs e) => ViewModel.OpenDrawer(2);

    // Preset activation handlers
    private void OnActivatePreset0Click(object sender, RoutedEventArgs e) => ViewModel.ApplyPreset(PresetItem.BuiltInPresets[0]);
    private void OnActivatePreset1Click(object sender, RoutedEventArgs e) => ViewModel.ApplyPreset(PresetItem.BuiltInPresets[1]);
    private void OnActivatePreset2Click(object sender, RoutedEventArgs e) => ViewModel.ApplyPreset(PresetItem.BuiltInPresets[2]);
    private void OnActivatePreset3Click(object sender, RoutedEventArgs e) => ViewModel.ApplyPreset(PresetItem.BuiltInPresets[3]);
    private void OnActivatePreset4Click(object sender, RoutedEventArgs e) => ViewModel.ApplyPreset(PresetItem.BuiltInPresets[4]);
}
