using System;
using System.Collections.Generic;
using EpicenterApp.Models;

namespace EpicenterApp.Services;

public interface IAudioEndpointService : IDisposable
{
    event EventHandler? DefaultDeviceChanged;
    AudioEndpointInfo? GetDefaultRenderEndpoint();
    IReadOnlyList<AudioEndpointInfo> GetRenderEndpoints();
    void Refresh();
}
