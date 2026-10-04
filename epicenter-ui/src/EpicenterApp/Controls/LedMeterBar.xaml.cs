using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace EpicenterApp.Controls;

public sealed partial class LedMeterBar : UserControl
{
    public static readonly DependencyProperty TitleProperty =
        DependencyProperty.Register(
            nameof(Title),
            typeof(string),
            typeof(LedMeterBar),
            new PropertyMetadata("METER L/R", OnTitleChangedStatic));

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

    private double _peakHoldL = -96.0;
    private double _peakHoldR = -96.0;
    private bool _isClipping;
    private int _clipHoldCounter;

    public LedMeterBar()
    {
        InitializeComponent();
        Loaded += (_, _) => UpdateVisuals();
        SizeChanged += (_, _) => UpdateVisuals();
    }

    private static void OnTitleChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is LedMeterBar m)
        {
            m.TitleTextBlock.Text = (string)e.NewValue;
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
        double totalWidth = ActualWidth > 40 ? ActualWidth - 28 : 200.0;
        if (totalWidth <= 0) return;

        double peak = IsActive ? PeakDbfs : -96.0;
        double rms = IsActive ? RmsDbfs : -96.0;

        // Clip detection with ~2s timed decay hold (60 frames at 30 Hz)
        if (peak >= -0.1)
        {
            _isClipping = true;
            _clipHoldCounter = 60;
        }
        else if (_clipHoldCounter > 0)
        {
            _clipHoldCounter--;
            if (_clipHoldCounter == 0)
            {
                _isClipping = false;
            }
        }

        UpdateClipVisuals();

        // Digital Peak Text
        if (!IsActive || peak <= -80.0)
        {
            ReadoutTextBlock.Text = "-inf dB";
        }
        else
        {
            ReadoutTextBlock.Text = $"{peak:F1} dB";
        }

        // Peak Hold Decay (0.5 dB decay per frame)
        if (peak >= _peakHoldL)
        {
            _peakHoldL = peak;
        }
        else
        {
            _peakHoldL = Math.Max(-96.0, _peakHoldL - 0.6);
        }

        _peakHoldR = _peakHoldL; // Stereo linked peak

        // Normalized widths [0.0, 1.0] from -48 dBFS to 0 dBFS
        double rmsNorm = NormalizeDbfs(rms);
        double peakNorm = NormalizeDbfs(_peakHoldL);

        double rmsWidth = Math.Clamp(rmsNorm * totalWidth, 0.0, totalWidth);
        double peakLeftPos = Math.Clamp(peakNorm * (totalWidth - 2), 0.0, totalWidth - 2);

        LeftRmsBar.Width = rmsWidth;
        RightRmsBar.Width = rmsWidth;

        Canvas.SetLeft(LeftPeakNeedle, peakLeftPos);
        Canvas.SetLeft(RightPeakNeedle, peakLeftPos);
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
        _clipHoldCounter = 0;
        UpdateClipVisuals();
        e.Handled = true;
    }
}
