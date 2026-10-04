using Windows.System;
using Microsoft.UI;
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

        // Configure window size and title
        Title = "Epicenter DSP Professional 1.0";
        AppWindow.Resize(new Windows.Graphics.SizeInt32(780, 560));

        // Keyboard navigation across the entire application
        if (Content is UIElement rootElement)
        {
            rootElement.KeyDown += OnRootKeyDown;
        }

        Closed += (_, _) => ViewModel.Dispose();
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
