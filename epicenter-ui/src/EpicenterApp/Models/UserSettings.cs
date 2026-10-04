namespace EpicenterApp.Models;

public class UserSettings
{
    public double Intensity { get; set; } = 75.0;
    public int PresetId { get; set; } = 0;
    public bool IsEnabled { get; set; } = true;
    public bool IsAbEpicenter { get; set; } = true;
    public int SchemaVersion { get; set; } = 1;
}
