using EpicenterApp.Models;

namespace EpicenterApp.Services;

public interface ISettingsStorageService
{
    UserSettings LoadSettings();
    void SaveSettings(UserSettings settings);
}
