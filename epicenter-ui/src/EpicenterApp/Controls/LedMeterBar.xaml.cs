using System.Diagnostics;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace EpicenterApp.Controls;

public sealed partial class LedMeterBar : UserControl
{
    public static readonly DependencyProperty IsAvailableProperty = DependencyProperty.Register(
        nameof(IsAvailable), typeof(bool), typeof(LedMeterBar), new PropertyMetadata(true, OnLevelsChangedStatic));
    public bool IsAvailable { get => (bool)GetValue(IsAvailableProperty); set => SetValue(IsAvailableProperty, value); }
    public static readonly DependencyProperty TitleProperty =
        DependencyProperty.Register(
            nameof(Title),
            typeof(string),
            typeof(LedMeterBar),
            new PropertyMetadata("LEVELS", OnTitleChangedStatic));

    public static readonly DependencyProperty PeakDbfsProperty =
        DependencyProperty.Register(
            nameof(PeakDbfs),
            typeof(double),
            typeof(LedMeterBar),
            new PropertyMetadata(-96.0, OnLevelsChangedStatic));

    public static readonly DependencyProperty RmsDbfsProperty =
        DependencyProperty.Register(
            nameof(RmsDbfs),
            typeof(double),
            typeof(LedMeterBar),
            new PropertyMetadata(-96.0, OnLevelsChangedStatic));

    public static readonly DependencyProperty IsActiveProperty =
        DependencyProperty.Register(
            nameof(IsActive),
            typeof(bool),
            typeof(LedMeterBar),
            new PropertyMetadata(true, OnLevelsChangedStatic));

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public double PeakDbfs
    {
        get => (double)GetValue(PeakDbfsProperty);
        set => SetValue(PeakDbfsProperty, value);
    }

    public double RmsDbfs
    {
        get => (double)GetValue(RmsDbfsProperty);
        set => SetValue(RmsDbfsProperty, value);
    }

    public bool IsActive
    {
        get => (bool)GetValue(IsActiveProperty);
        set => SetValue(IsActiveProperty, value);
    }

    private double _peakHoldDbfs = -96.0;
    private double _peakHoldUntil;
    private double _clipHoldUntil;
    private double _lastVisualTime;
    private bool _isClipping;
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly DispatcherTimer _decayTimer = new() { Interval = TimeSpan.FromMilliseconds(50) };
    private const double HoldSeconds = 2.0;
    private const double PeakDecayDbPerSecond = 18.0;

    public LedMeterBar()
    {
        InitializeComponent();
        _decayTimer.Tick += (_, _) => UpdateVisuals();
        Loaded += (_, _) => { UpdateVisuals(); _decayTimer.Start(); };
        Unloaded += (_, _) => _decayTimer.Stop();
        SizeChanged += (_, _) => UpdateVisuals();
        PeakRail.SizeChanged += (_, _) => UpdateVisuals();
        RmsRail.SizeChanged += (_, _) => UpdateVisuals();
    }

    private static void OnTitleChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is LedMeterBar m)
        {
            if (m.TitleTextBlock is not null) m.TitleTextBlock.Text = (string)e.NewValue;
        }
    }

    private static void OnLevelsChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is LedMeterBar m)
        {
            m.UpdateVisuals();
        }
    }

    public void UpdateLevels(double peakDb, double rmsDb)
    {
        PeakDbfs = peakDb;
        RmsDbfs = rmsDb;
    }

    private void UpdateVisuals()
    {
        if (ReadoutTextBlock == null) return;
        double now = _clock.Elapsed.TotalSeconds;
        bool available = IsActive && IsAvailable;
        double peak = available && double.IsFinite(PeakDbfs) ? PeakDbfs : -96.0;
        double rms = available && double.IsFinite(RmsDbfs) ? RmsDbfs : -96.0;

        if (!available)
        {
            _peakHoldDbfs = -96.0;
            _peakHoldUntil = _clipHoldUntil = 0.0;
        }

        // Monotonic time, not callback counts: updating peak and RMS separately
        // must not halve the hold period. Timer also clears latches on steady audio.
        if (available && peak >= -0.1) _clipHoldUntil = now + HoldSeconds;
        _isClipping = available && now < _clipHoldUntil;

        if (available && peak >= _peakHoldDbfs)
        {
            _peakHoldDbfs = peak;
            _peakHoldUntil = now + HoldSeconds;
        }
        else if (available && now > _peakHoldUntil)
        {
            double decaySeconds = Math.Max(0.0, now - Math.Max(_lastVisualTime, _peakHoldUntil));
            _peakHoldDbfs = Math.Max(peak, _peakHoldDbfs - PeakDecayDbPerSecond * decaySeconds);
        }
        _lastVisualTime = now;

        UpdateClipVisuals();
        ClipBadge.Opacity = available ? 1.0 : 0.4;
        ReadoutTextBlock.Text = !available ? "N/A" : peak <= -96.0 ? "−∞ dBFS" : $"{peak:F1} dBFS";

        // IPC carries aggregate peak/RMS only. Each row shows its actual metric.
        double peakWidth = Math.Max(0.0, PeakRail.ActualWidth);
        double rmsWidth = Math.Max(0.0, RmsRail.ActualWidth);
        PeakBar.Width = NormalizeDbfs(peak) * peakWidth;
        RmsBar.Width = NormalizeDbfs(rms) * rmsWidth;
        PeakNeedle.Visibility = available && _peakHoldDbfs > -48.0 ? Visibility.Visible : Visibility.Collapsed;
        Canvas.SetLeft(PeakNeedle, NormalizeDbfs(_peakHoldDbfs) * Math.Max(0.0, peakWidth - 2.0));
    }

    private static double NormalizeDbfs(double db)
    {
        if (db <= -48.0) return 0.0;
        if (db >= 0.0) return 1.0;
        // Linear scale from -48 dBFS to 0 dBFS
        return (db + 48.0) / 48.0;
    }

    private void UpdateClipVisuals()
    {
        if (_isClipping)
        {
            ClipBadge.Background = new SolidColorBrush(ColorHelper.FromArgb(255, 239, 68, 68)); // Bright red
            ClipBadge.BorderBrush = new SolidColorBrush(ColorHelper.FromArgb(255, 255, 100, 100));
            ClipTextBlock.Foreground = new SolidColorBrush(Colors.White);
        }
        else
        {
            ClipBadge.Background = new SolidColorBrush(ColorHelper.FromArgb(255, 43, 21, 21)); // Dark subdued red
            ClipBadge.BorderBrush = new SolidColorBrush(ColorHelper.FromArgb(255, 77, 28, 28));
            ClipTextBlock.Foreground = new SolidColorBrush(ColorHelper.FromArgb(255, 128, 51, 51));
        }
    }

    private void OnClipBadgePointerPressed(object sender, PointerRoutedEventArgs e)
    {
        // Click on CLIP badge resets clipping latch immediately
        _isClipping = false;
        _clipHoldUntil = 0.0;
        UpdateClipVisuals();
        e.Handled = true;
    }
}
