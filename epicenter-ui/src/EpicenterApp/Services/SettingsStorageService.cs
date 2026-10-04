using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using EpicenterApp.Models;

namespace EpicenterApp.Services;

[JsonSourceGenerationOptions(WriteIndented = true, PropertyNameCaseInsensitive = true)]
[JsonSerializable(typeof(UserSettings))]
internal partial class UserSettingsJsonContext : JsonSerializerContext
{
}

public class SettingsStorageService : ISettingsStorageService
{
    private readonly string _filePath;
    private readonly object _fileLock = new();

    public SettingsStorageService(string? customFilePath = null)
    {
        if (!string.IsNullOrWhiteSpace(customFilePath))
        {
            _filePath = customFilePath;
        }
        else
        {
            string appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string appFolder = Path.Combine(appData, "EpicenterDSP");
            _filePath = Path.Combine(appFolder, "settings.json");
        }
    }

    public UserSettings LoadSettings()
    {
        lock (_fileLock)
        {
            try
            {
                if (!File.Exists(_filePath))
                {
                    return CreateDefaultSettings();
                }

                string json = File.ReadAllText(_filePath);
                if (string.IsNullOrWhiteSpace(json))
                {
                    return CreateDefaultSettings();
                }

                var settings = JsonSerializer.Deserialize(json, UserSettingsJsonContext.Default.UserSettings);
                if (settings == null)
                {
                    return CreateDefaultSettings();
                }

                // Sanitize parameters against out-of-range or corrupted values
                SanitizeSettings(settings);
                return settings;
            }
            catch
            {
                // In case of any I/O error or JSON parsing error, fall back gracefully to defaults
                return CreateDefaultSettings();
            }
        }
    }

    public void SaveSettings(UserSettings settings)
    {
        if (settings == null) return;

        lock (_fileLock)
        {
            try
            {
                SanitizeSettings(settings);

                string? dir = Path.GetDirectoryName(_filePath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }

                string json = JsonSerializer.Serialize(settings, UserSettingsJsonContext.Default.UserSettings);

                // Atomic write via temporary file
                string tempFile = _filePath + ".tmp";
                File.WriteAllText(tempFile, json);
                File.Move(tempFile, _filePath, overwrite: true);
            }
            catch
            {
                // Silently ignore save errors to avoid disrupting UI or audio
            }
        }
    }

    private static UserSettings CreateDefaultSettings()
    {
        return new UserSettings
        {
            Intensity = 75.0,
            PresetId = 0,
            IsEnabled = true,
            IsAbEpicenter = true,
            SchemaVersion = 1
        };
    }

    private static void SanitizeSettings(UserSettings s)
    {
        if (double.IsNaN(s.Intensity) || double.IsInfinity(s.Intensity) || s.Intensity < 0.0)
        {
            s.Intensity = 0.0;
        }
        else if (s.Intensity > 100.0)
        {
            s.Intensity = 100.0;
        }

        if (s.PresetId < 0 || s.PresetId > 4)
        {
            s.PresetId = 0;
        }
    }
}
