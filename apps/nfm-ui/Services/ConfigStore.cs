using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Services;

public class ConfigStore {
    private readonly string _configPath;
    public string ConfigPath => _configPath;
    private static readonly JsonSerializerOptions _jsonOptions = new() {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = {
            new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower)
        }
    };

    public ConfigStore(string? configPath = null) {
        _configPath = configPath ?? GetDefaultConfigPath();
    }

    public static string GetDefaultConfigPath() {
        var baseDir = AppContext.BaseDirectory;
        return Path.Combine(baseDir, "NetworkFlowMonitor.json");
    }

    public AppConfigModel Load() {
        if (!File.Exists(_configPath)) {
            return AppConfigModel.Default;
        }

        try {
            var json = File.ReadAllText(_configPath);
            var model = JsonSerializer.Deserialize<AppConfigModel>(json, _jsonOptions);
            return model ?? AppConfigModel.Default;
        } catch (Exception ex) {
            PreserveDamagedFile(_configPath, ex);
            return AppConfigModel.Default;
        }
    }

    public bool Save(AppConfigModel config) {
        try {
            var dir = Path.GetDirectoryName(_configPath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) {
                Directory.CreateDirectory(dir);
            }

            var tempPath = _configPath + ".tmp." + Guid.NewGuid().ToString("N");
            var json = JsonSerializer.Serialize(config, _jsonOptions);

            File.WriteAllText(tempPath, json);

            // Atomic file replace
            if (File.Exists(_configPath)) {
                File.Move(tempPath, _configPath, overwrite: true);
            } else {
                File.Move(tempPath, _configPath);
            }
            return true;
        } catch {
            return false;
        }
    }

    private static void PreserveDamagedFile(string originalPath, Exception _) {
        try {
            var dir = Path.GetDirectoryName(originalPath) ?? AppContext.BaseDirectory;
            var timestamp = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss");
            var damagedPath = Path.Combine(dir, $"{Path.GetFileNameWithoutExtension(originalPath)}.{timestamp}.damaged.json");
            if (File.Exists(originalPath)) {
                File.Copy(originalPath, damagedPath, overwrite: true);
            }
        } catch {
            // Best effort preservation
        }
    }
}
