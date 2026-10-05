using System;
using System.Diagnostics;
using System.Globalization;
using Windows.Foundation;
using Windows.System;
using Microsoft.UI;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;

namespace EpicenterApp.Controls;

public sealed partial class RotaryKnob : UserControl
{
    public static readonly DependencyProperty ValueProperty =
        DependencyProperty.Register(nameof(Value), typeof(double), typeof(RotaryKnob),
            new PropertyMetadata(75.0, OnValueChangedStatic));

    public static readonly DependencyProperty DefaultValueProperty =
        DependencyProperty.Register(nameof(DefaultValue), typeof(double), typeof(RotaryKnob),
            new PropertyMetadata(75.0));

    public static readonly DependencyProperty IsActiveProperty =
        DependencyProperty.Register(nameof(IsActive), typeof(bool), typeof(RotaryKnob),
            new PropertyMetadata(true, OnIsActiveChangedStatic));

    public double Value
    {
        get => (double)GetValue(ValueProperty);
        set => SetValue(ValueProperty, KnobInputMath.Clamp(value));
    }

    public double DefaultValue
    {
        get => (double)GetValue(DefaultValueProperty);
        set => SetValue(DefaultValueProperty, KnobInputMath.Clamp(value));
    }

    public bool IsActive
    {
        get => (bool)GetValue(IsActiveProperty);
        set => SetValue(IsActiveProperty, value);
    }

    // User input only: presets/model updates do not feed back into the model.
    // One-way Value binding + this event avoids replacing a binding expression.
    public event EventHandler<double>? ValueChanged;

    private uint? _dragPointerId;
    private double _previousPointerY;
    private bool _isFocused;
    private bool _ready;
    private double _displayValue = 75.0;
    private long _previousFrameTimestamp;
    private readonly DispatcherTimer _animationTimer = new() { Interval = TimeSpan.FromMilliseconds(16) };

    public RotaryKnob()
    {
        InitializeComponent();
        _ready = true;
        _animationTimer.Tick += OnAnimationTick;
        // Receive handled events too, including input routed through ScrollViewer.
        AddHandler(PointerPressedEvent, new PointerEventHandler(OnPointerPressed), true);
        AddHandler(PointerMovedEvent, new PointerEventHandler(OnPointerMoved), true);
        AddHandler(PointerReleasedEvent, new PointerEventHandler(OnPointerReleased), true);
        AddHandler(PointerCanceledEvent, new PointerEventHandler(OnPointerCanceled), true);
        AddHandler(PointerCaptureLostEvent, new PointerEventHandler(OnPointerCanceled), true);
        AddHandler(PointerWheelChangedEvent, new PointerEventHandler(OnPointerWheelChanged), true);
        DoubleTapped += OnDoubleTapped;
        KeyDown += OnKeyDown;
        GotFocus += OnGotFocus;
        LostFocus += OnLostFocus;
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _displayValue = KnobInputMath.Clamp(Value);
        CreateTicks();
        DrawTrack();
        UpdateVisualState();
        DrawDisplayValue();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _animationTimer.Stop();
        _dragPointerId = null;
        ReleasePointerCaptures();
    }

    private static void OnValueChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is RotaryKnob knob && knob._ready)
        {
            knob.UpdateVisualState();
            knob.AnimateToValue();
        }
    }

    private static void OnIsActiveChangedStatic(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is RotaryKnob knob && knob._ready) knob.UpdateVisualState();
    }

    public static string FormatValue(double value)
    {
        value = KnobInputMath.Clamp(value);
        return Math.Abs(value - Math.Round(value)) >= 0.05
            ? value.ToString("F1", CultureInfo.InvariantCulture)
            : Math.Round(value).ToString("F0", CultureInfo.InvariantCulture);
    }

    private void UpdateVisualState()
    {
        if (!_ready) return;
        bool reconstructing = IsActive && Value > 0.0;
        ActiveArcPath.Opacity = reconstructing ? 1.0 : 0.25;
        PositionIndicator.Opacity = reconstructing ? 1.0 : 0.45;
        ValueTextBlock.Foreground = new SolidColorBrush(ColorHelper.FromArgb(255,
            (byte)(IsActive ? 244 : 157), (byte)(IsActive ? 244 : 157), (byte)(IsActive ? 245 : 163)));
        UpdateFocusStroke();
    }

    private void UpdateFocusStroke()
    {
        if (!_ready) return;
        bool highlighted = _isFocused || _dragPointerId.HasValue;
        InnerDialEllipse.Stroke = new SolidColorBrush(highlighted
            ? ColorHelper.FromArgb(255, 255, 0, 38)
            : ColorHelper.FromArgb(255, 75, 75, 79));
        InnerDialEllipse.StrokeThickness = highlighted ? 2 : 1;
    }

    private void AnimateToValue()
    {
        if (!IsLoaded)
        {
            _displayValue = Value;
            DrawDisplayValue();
            return;
        }
        if (!_animationTimer.IsEnabled)
        {
            _previousFrameTimestamp = Stopwatch.GetTimestamp();
            _animationTimer.Start();
        }
        // Frequent pointer events only replace Value, the current target.
        // They never restart the clock or delay the next visual update.
    }

    private void OnAnimationTick(object? sender, object e)
    {
        long now = Stopwatch.GetTimestamp();
        double elapsed = Stopwatch.GetElapsedTime(_previousFrameTimestamp, now).TotalSeconds;
        _previousFrameTimestamp = now;
        _displayValue = KnobInputMath.Follow(_displayValue, Value, elapsed);
        if (Math.Abs(_displayValue - Value) <= 0.015)
        {
            _displayValue = Value;
            _animationTimer.Stop();
        }
        DrawDisplayValue();
    }

    private void DrawDisplayValue()
    {
        // The number, rotating marker and red arc share one visual value.
        // DSP/ValueChanged use Value immediately; smoothing is UI-only.
        ValueTextBlock.Text = FormatValue(_displayValue);
        PercentLabel.Text = _displayValue <= 0.0 ? "BYPASS" : "%";
        PercentLabel.FontSize = _displayValue <= 0.0 ? 9 : 12;
        DrawActiveArc(_displayValue);
    }

    private void CreateTicks()
    {
        TickCanvas.Children.Clear();
        for (int i = 0; i <= 40; i++)
        {
            double angle = -135.0 + i * 270.0 / 40.0;
            Point a = ComputePoint(100.0, 100.0, i % 5 == 0 ? 82.0 : 85.0, angle);
            Point b = ComputePoint(100.0, 100.0, 89.0, angle);
            TickCanvas.Children.Add(new Line
            {
                X1 = a.X, Y1 = a.Y, X2 = b.X, Y2 = b.Y,
                Stroke = new SolidColorBrush(ColorHelper.FromArgb(255, 88, 88, 93)),
                StrokeThickness = i % 5 == 0 ? 1.5 : 1.0
            });
        }
    }

    private void DrawTrack() => TrackPath.Data = CreateArc(270.0);

    private void DrawActiveArc(double value)
    {
        if (!_ready) return;
        double sweep = KnobInputMath.Clamp(value) * 2.7;
        ActiveArcPath.Data = sweep <= 0.001 ? null : CreateArc(sweep);
        IndicatorRotation.Angle = -135.0 + sweep;
    }

    private static PathGeometry CreateArc(double sweep)
    {
        const double radius = 95.0;
        var figure = new PathFigure { StartPoint = ComputePoint(100.0, 100.0, radius, -135.0) };
        figure.Segments.Add(new ArcSegment
        {
            Point = ComputePoint(100.0, 100.0, radius, -135.0 + sweep),
            Size = new Size(radius, radius),
            IsLargeArc = sweep > 180.0,
            SweepDirection = SweepDirection.Clockwise
        });
        var geometry = new PathGeometry();
        geometry.Figures.Add(figure);
        return geometry;
    }

    private static Point ComputePoint(double cx, double cy, double radius, double degrees)
    {
        double radians = degrees * Math.PI / 180.0;
        return new Point(cx + radius * Math.Sin(radians), cy - radius * Math.Cos(radians));
    }

    private void SetFromInput(double value)
    {
        double next = KnobInputMath.Clamp(value);
        if (Math.Abs(Value - next) < 0.00001) return;
        Value = next;
        ValueChanged?.Invoke(this, next);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (_dragPointerId.HasValue || !IsEnabled) return;
        var point = e.GetCurrentPoint(this);
        if (e.Pointer.PointerDeviceType == PointerDeviceType.Mouse && !point.Properties.IsLeftButtonPressed) return;
        Focus(FocusState.Pointer);
        if (!CapturePointer(e.Pointer)) return;
        _dragPointerId = e.Pointer.PointerId;
        _previousPointerY = point.Position.Y;
        UpdateFocusStroke();
        e.Handled = true;
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (_dragPointerId != e.Pointer.PointerId) return;
        var point = e.GetCurrentPoint(this);
        double y = point.Position.Y;
        bool fine = (e.KeyModifiers & VirtualKeyModifiers.Shift) != 0;
        SetFromInput(KnobInputMath.Drag(Value, _previousPointerY - y, fine));
        _previousPointerY = y;
        e.Handled = true;
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        if (_dragPointerId != e.Pointer.PointerId) return;
        _dragPointerId = null;
        ReleasePointerCapture(e.Pointer);
        UpdateFocusStroke();
        e.Handled = true;
    }

    private void OnPointerCanceled(object sender, PointerRoutedEventArgs e)
    {
        if (_dragPointerId != e.Pointer.PointerId) return;
        _dragPointerId = null;
        ReleasePointerCapture(e.Pointer);
        UpdateFocusStroke();
    }

    private void OnPointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        if (!IsEnabled) return;
        int delta = e.GetCurrentPoint(this).Properties.MouseWheelDelta;
        bool fine = (e.KeyModifiers & VirtualKeyModifiers.Shift) != 0;
        Focus(FocusState.Pointer);
        SetFromInput(KnobInputMath.Wheel(Value, delta, fine));
        e.Handled = true;
    }

    private void OnDoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        SetFromInput(DefaultValue);
        e.Handled = true;
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        bool fine = (InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Shift) &
            Windows.UI.Core.CoreVirtualKeyStates.Down) != 0;
        double step = fine ? 0.1 : 1.0;
        switch (e.Key)
        {
            case VirtualKey.Up:
            case VirtualKey.Right: SetFromInput(Value + step); break;
            case VirtualKey.Down:
            case VirtualKey.Left: SetFromInput(Value - step); break;
            case VirtualKey.PageUp: SetFromInput(Value + 10.0); break;
            case VirtualKey.PageDown: SetFromInput(Value - 10.0); break;
            case VirtualKey.Home: SetFromInput(0.0); break;
            case VirtualKey.End: SetFromInput(100.0); break;
            default: return;
        }
        e.Handled = true;
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
