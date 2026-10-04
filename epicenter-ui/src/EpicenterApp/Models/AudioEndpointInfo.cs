namespace EpicenterApp.Models;

public record AudioEndpointInfo(
    string Id,
    string Name,
    bool IsDefault,
    string Status,
    bool IsApoBound = false
);
