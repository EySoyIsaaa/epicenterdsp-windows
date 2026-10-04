namespace EpicenterApp.Models;

public record PresetItem(
    int Id,
    string Name,
    float DefaultIntensity,
    string Description
)
{
    public static readonly PresetItem[] BuiltInPresets =
    [
        new(0, "Default Studio", 75.0f, "Preset estándar de fábrica (75%)"),
        new(1, "Gentle", 45.0f, "Refuerzo sutil moderado (45%)"),
        new(2, "Deep", 85.0f, "Mayor énfasis en bajas frecuencias (85%)"),
        new(3, "Power", 100.0f, "Máxima reconstrucción armónica (100%)"),
        new(4, "Custom", 75.0f, "Configuración personalizada por el usuario")
    ];
}
