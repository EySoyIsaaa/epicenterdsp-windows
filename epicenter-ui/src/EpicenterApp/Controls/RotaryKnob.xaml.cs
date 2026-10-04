using System;
using System.Globalization;
using Windows.Foundation;
using Windows.System;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace EpicenterApp.Controls;

public sealed partial class RotaryKnob : UserControl
{
    public static readonly DependencyProperty ValueProperty =
        DependencyProperty.Register(
            nameof(Value),
            typeof(double),
            typeof(RotaryKnob),
            new PropertyMetadata(75.0, OnValueChangedStatic));

    public static readonly DependencyProperty DefaultValueProperty =
        DependencyProperty.Register(
            nameof(DefaultValue),
            typeof(double),
            typeof(RotaryKnob),
            new PropertyMetadata(75.0));

    public static readonly DependencyProperty IsActiveProperty =
        DependencyProperty.Register(
            nameof(IsActive),
            typeof(bool),
            typeof(RotaryKnob),
            new PropertyMetadata(true, OnIsActiveChangedStatic));

    public double Value
    {
        get => (double)GetValue(ValueProperty);
        set => SetValue(ValueProperty, Math.Clamp(value, 0.0, 100.0));
    }

    public double DefaultValue
    {
        get => (double)GetValue(DefaultValueProperty);
        set => SetValue(DefaultValueProperty, value);
    }

    public bool IsActive
    {
        get => (bool)GetValue(IsActiveProperty);
        set => SetValue(IsActiveProperty, value);
    }

    private bool _isDragging;
    private Point _startPoint;
    private double _startValue;
    private bool _isFocused;

    public RotaryKnob()
    {
        InitializeComponent();
        Loaded += (_, _) => UpdateVisuals();
    }

    private static void OnValueChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is RotaryKnob knob)
        {
            knob.UpdateVisuals();
        }
    }

    private static void OnIsActiveChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is RotaryKnob knob)
        {
            knob.UpdateVisuals();
        }
    }

    public static string FormatValue(double val)
    {
        return (Math.Abs(val - Math.Round(val)) >= 0.05)
            ? val.ToString("F1", CultureInfo.InvariantCulture)
            : Math.Round(val).ToString("F0");
    }

    private void UpdateVisuals()
    {
        double val = Value;

        // Display fractional precision when fine-tuning or decimal value exists
        ValueTextBlock.Text = FormatValue(val);
        ValueTextBlock.FontSize = (Math.Abs(val - Math.Round(val)) >= 0.05) ? 28 : 34;

        // Dim visual styling when bypass or inactive
        if (IsActive)
        {
            ActiveArcPath.Opacity = 1.0;
            ValueTextBlock.Foreground = (Brush)Application.Current.Resources["TextPrimaryHigh"];
        }
        else
        {
            ActiveArcPath.Opacity = 0.35;
            ValueTextBlock.Foreground = (Brush)Application.Current.Resources["TextSecondaryMuted"];
        }

        UpdateFocusStroke();
        DrawArcs(val);
    }

    private void UpdateFocusStroke()
    {
        if (InnerDialEllipse == null) return;

        if (_isFocused)
        {
            InnerDialEllipse.Stroke = (Brush)Application.Current.Resources["EpicenterAmberGold"];
            InnerDialEllipse.StrokeThickness = 2;
        }
        else
        {
            InnerDialEllipse.Stroke = new SolidColorBrush(ColorHelper.FromArgb(255, 42, 48, 64));
            InnerDialEllipse.StrokeThickness = 1;
        }
    }

    private void DrawArcs(double value)
    {
        const double cx = 80.0;
        const double cy = 73.0;
        const double radius = 58.0;

        // Background track: -135 deg to +135 deg (270 deg total)
        Point trackStart = ComputePoint(cx, cy, radius, -135.0);
        Point trackEnd = ComputePoint(cx, cy, radius, 135.0);

        var trackGeom = new PathGeometry();
        var trackFig = new PathFigure { StartPoint = trackStart, IsClosed = false };
        trackFig.Segments.Add(new ArcSegment
        {
            Point = trackEnd,
            Size = new Size(radius, radius),
            IsLargeArc = true,
            SweepDirection = SweepDirection.Clockwise
        });
        trackGeom.Figures.Add(trackFig);
        TrackPath.Data = trackGeom;

        // Active Arc: -135 deg to (-135 + (val / 100) * 270) deg
        if (value <= 0.05)
        {
            ActiveArcPath.Data = null;
            return;
        }

        double sweep = (value / 100.0) * 270.0;
        double currentAngle = -135.0 + sweep;
        Point arcEnd = ComputePoint(cx, cy, radius, currentAngle);

        var activeGeom = new PathGeometry();
        var activeFig = new PathFigure { StartPoint = trackStart, IsClosed = false };
        activeFig.Segments.Add(new ArcSegment
        {
            Point = arcEnd,
            Size = new Size(radius, radius),
            IsLargeArc = sweep > 180.0,
            SweepDirection = SweepDirection.Clockwise
        });
        activeGeom.Figures.Add(activeFig);
        ActiveArcPath.Data = activeGeom;
    }

    private static Point ComputePoint(double cx, double cy, double r, double angleDegrees)
    {
        double rad = angleDegrees * (Math.PI / 180.0);
        double x = cx + r * Math.Sin(rad);
        double y = cy - r * Math.Cos(rad);
        return new Point(x, y);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        _isDragging = true;
        _startPoint = e.GetCurrentPoint(this).Position;
        _startValue = Value;
        CapturePointer(e.Pointer);
        Focus(FocusState.Programmatic);
        e.Handled = true;
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_isDragging) return;

        Point currentPoint = e.GetCurrentPoint(this).Position;
        double deltaY = _startPoint.Y - currentPoint.Y; // Drag up increases

        // Holding Shift provides fine-tuning
        bool isShift = (e.KeyModifiers & VirtualKeyModifiers.Shift) != 0;
        double sensitivity = isShift ? 0.1 : 0.5;

        double newValue = _startValue + (deltaY * sensitivity);
        Value = Math.Clamp(newValue, 0.0, 100.0);
        e.Handled = true;
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        if (_isDragging)
        {
            _isDragging = false;
            ReleasePointerCapture(e.Pointer);
            e.Handled = true;
        }
    }

    private void OnPointerCaptureLost(object sender, PointerRoutedEventArgs e)
    {
        _isDragging = false;
    }

    private void OnPointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        int delta = e.GetCurrentPoint(this).Properties.MouseWheelDelta;
        bool isShift = (e.KeyModifiers & VirtualKeyModifiers.Shift) != 0;
        double step = isShift ? 0.2 : 1.0;

        Value = Math.Clamp(Value + (delta > 0 ? step : -step), 0.0, 100.0);
        e.Handled = true;
    }

    private void OnDoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        Value = DefaultValue;
        e.Handled = true;
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        switch (e.Key)
        {
            case VirtualKey.Up:
            case VirtualKey.Right:
                Value = Math.Clamp(Value + 1.0, 0.0, 100.0);
                e.Handled = true;
                break;
            case VirtualKey.Down:
            case VirtualKey.Left:
                Value = Math.Clamp(Value - 1.0, 0.0, 100.0);
                e.Handled = true;
                break;
            case VirtualKey.PageUp:
                Value = Math.Clamp(Value + 10.0, 0.0, 100.0);
                e.Handled = true;
                break;
            case VirtualKey.PageDown:
                Value = Math.Clamp(Value - 10.0, 0.0, 100.0);
                e.Handled = true;
                break;
            case VirtualKey.Home:
                Value = 0.0;
                e.Handled = true;
                break;
            case VirtualKey.End:
                Value = 100.0;
                e.Handled = true;
                break;
        }
    }

    private void OnGotFocus(object sender, RoutedEventArgs e)
    {
        _isFocused = true;
        UpdateFocusStroke();
    }

    private void OnLostFocus(object sender, RoutedEventArgs e)
    {
        _isFocused = false;
        UpdateFocusStroke();
    }
}
