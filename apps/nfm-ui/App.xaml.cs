using System;
using System.Linq;
using System.Windows;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Store;
using NetworkFlowMonitor.Views;

namespace NetworkFlowMonitor;

public partial class App : Application {
    private TelemetryStore? _store;
    private NamedPipeClient? _client;
    private DockCapsuleWindow? _capsuleWindow;
    private MonitorPanelWindow? _panelWindow;
    private MonitorPresentationHost? _host;

    protected override void OnStartup(StartupEventArgs e) {
        base.OnStartup(e);

        if (e.Args.Contains("--surface-spike", StringComparer.OrdinalIgnoreCase) ||
            e.Args.Contains("--spike", StringComparer.OrdinalIgnoreCase)) {
            var spikeWindow = new WpfSurfaceSpikeWindow();
            spikeWindow.Show();
            return;
        }

        if (e.Args.Contains("--tracer", StringComparer.OrdinalIgnoreCase) ||
            e.Args.Contains("--tracer-bullet", StringComparer.OrdinalIgnoreCase)) {
            var mainWindow = new MainWindow();
            mainWindow.Show();
            return;
        }

        // Production dual-window presentation host
        _store = new TelemetryStore();
        _client = new NamedPipeClient(_store);
        _capsuleWindow = new DockCapsuleWindow();
        _panelWindow = new MonitorPanelWindow();

        _host = new MonitorPresentationHost(_store, _capsuleWindow, _panelWindow);
        _host.Start();
        _client.Start();
    }

    protected override async void OnExit(ExitEventArgs e) {
        _host?.Dispose();
        if (_client != null) {
            await _client.DisposeAsync();
        }
        base.OnExit(e);
    }
}
