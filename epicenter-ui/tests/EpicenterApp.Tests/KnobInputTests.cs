using EpicenterApp.Controls;

namespace EpicenterApp.Tests;

[TestClass]
public class KnobInputTests
{
    [TestMethod]
    public void KnobClampHandlesBoundsAndNonFiniteInput()
    {
        Assert.AreEqual(0.0, KnobInputMath.Clamp(-20));
        Assert.AreEqual(100.0, KnobInputMath.Clamp(140));
        Assert.AreEqual(0.0, KnobInputMath.Clamp(double.NaN));
        Assert.AreEqual(0.0, KnobInputMath.Clamp(double.PositiveInfinity));
    }

    [TestMethod]
    public void KnobDragUsesIncrementalFineAdjustmentWithoutJump()
    {
        double value = KnobInputMath.Drag(75.0, 20.0, false);
        Assert.AreEqual(80.0, value, 0.000001);
        value = KnobInputMath.Drag(value, 1.0, true);
        Assert.AreEqual(80.025, value, 0.000001);
        Assert.AreEqual(79.775, KnobInputMath.Drag(value, -10.0, true), 0.000001);
        Assert.AreEqual(100.0, KnobInputMath.Drag(value, 1000.0, false));
        Assert.AreEqual(0.0, KnobInputMath.Drag(value, -1000.0, false));
    }

    [TestMethod]
    public void KnobWheelPreservesDirectionMagnitudeAndFinePrecision()
    {
        Assert.AreEqual(75.5, KnobInputMath.Wheel(75.0, 120, false));
        Assert.AreEqual(74.0, KnobInputMath.Wheel(75.0, -240, false));
        Assert.AreEqual(75.05, KnobInputMath.Wheel(75.0, 120, true), 0.000001);
        Assert.AreEqual(75.025, KnobInputMath.Wheel(75.0, 60, true), 0.000001);
        Assert.AreEqual(75.0, KnobInputMath.Wheel(75.0, 0, false));
    }

    [TestMethod]
    public void KnobFollowIsIndependentOfFrameFrequency()
    {
        double coarse = 10.0;
        double fine = 10.0;
        for (int i = 0; i < 30; i++) coarse = KnobInputMath.Follow(coarse, 90.0, 1.0 / 30.0);
        for (int i = 0; i < 144; i++) fine = KnobInputMath.Follow(fine, 90.0, 1.0 / 144.0);
        Assert.AreEqual(coarse, fine, 0.00000001);
        Assert.AreEqual(KnobInputMath.Follow(10.0, 90.0, 1.0), coarse, 0.00000001);
    }

    [TestMethod]
    public void KnobFollowIsMonotonicAndCannotOvershoot()
    {
        double current = 0.0;
        for (int i = 0; i < 100; i++)
        {
            double next = KnobInputMath.Follow(current, 80.0, 1.0 / 60.0);
            Assert.IsTrue(next >= current && next <= 80.0);
            current = next;
        }
        for (int i = 0; i < 100; i++)
        {
            double next = KnobInputMath.Follow(current, 20.0, 1.0 / 60.0);
            Assert.IsTrue(next <= current && next >= 20.0);
            current = next;
        }
    }

    [TestMethod]
    public void KnobFollowReversesImmediatelyAndStaysInRange()
    {
        double ascending = KnobInputMath.Follow(50.0, 100.0, 1.0 / 60.0);
        double reversed = KnobInputMath.Follow(ascending, 0.0, 1.0 / 60.0);
        Assert.IsTrue(ascending > 50.0 && ascending < 100.0);
        Assert.IsTrue(reversed < ascending && reversed > 0.0);
        Assert.AreEqual(100.0, KnobInputMath.Follow(-20.0, 140.0, 1000.0));
        Assert.AreEqual(20.0, KnobInputMath.Follow(20.0, 80.0, 0.0));
        Assert.AreEqual(20.0, KnobInputMath.Follow(20.0, 80.0, double.NaN));
        Assert.AreEqual(0.0, KnobInputMath.Follow(double.NaN, 80.0, -1.0));
    }
}
