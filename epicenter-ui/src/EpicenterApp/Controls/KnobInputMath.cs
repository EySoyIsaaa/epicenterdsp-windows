using System;

namespace EpicenterApp.Controls;

/// <summary>Platform-independent input math used by the native knob.</summary>
public static class KnobInputMath
{
    public static double Clamp(double value) => double.IsFinite(value) ? Math.Clamp(value, 0.0, 100.0) : 0.0;

    // Incremental deltas prevent a value jump when Shift changes mid-drag.
    public static double Drag(double current, double upwardPixels, bool fine) =>
        Clamp(Clamp(current) + upwardPixels * (fine ? 0.025 : 0.25));

    public static double Wheel(double current, int delta, bool fine) =>
        Clamp(Clamp(current) + delta / 120.0 * (fine ? 0.05 : 0.5));

    // Exact exponential response: elapsed time, not frame/input frequency,
    // controls the animation speed. Moving/reversing targets do not reset it.
    public static double Follow(double current, double target, double elapsedSeconds)
    {
        current = Clamp(current);
        target = Clamp(target);
        if (!double.IsFinite(elapsedSeconds) || elapsedSeconds <= 0) return current;
        const double responseSeconds = 0.065;
        double amount = 1.0 - Math.Exp(-elapsedSeconds / responseSeconds);
        return Clamp(current + (target - current) * amount);
    }
}
