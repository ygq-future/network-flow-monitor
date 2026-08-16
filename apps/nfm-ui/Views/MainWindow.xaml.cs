using System;
using System.Globalization;
using System.Windows;
using System.Windows.Media;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Views;

public partial class MainWindow : Window
{
    private readonly TelemetryStore _store;
    private readonly NamedPipeClient _client;

    public MainWindow() : this(new TelemetryStore())
    {
    }

    public MainWindow(TelemetryStore store)
    {
        InitializeComponent();
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _client = new NamedPipeClient(_store);

        _store.SnapshotUpdated += OnSnapshotUpdated;

        Loaded += (_, _) => _client.Start();
        Closed += async (_, _) => await _client.DisposeAsync();
    }

    private void OnSnapshotUpdated(TelemetrySnapshot snapshot)
    {
        Dispatcher.BeginInvoke(() =>
        {
            DownloadRateText.Text = FormatRate(snapshot.DownloadBytesPerSec);
            UploadRateText.Text = FormatRate(snapshot.UploadBytesPerSec);
            SeqText.Text = $"Seq: {snapshot.Seq}";
            InstanceText.Text = $"Collector PID: {(snapshot.CollectorPid != 0 ? snapshot.CollectorPid.ToString() : "N/A")}";

            switch (snapshot.ConnectionState)
            {
                case CollectorConnectionState.Connected:
                    StatusText.Text = "Connected";
                    StatusText.Foreground = new SolidColorBrush(Color.FromRgb(34, 197, 94)); // Green
                    StatusBadge.Background = new SolidColorBrush(Color.FromRgb(20, 83, 45));
                    break;
                case CollectorConnectionState.Connecting:
                    StatusText.Text = "Connecting…";
                    StatusText.Foreground = new SolidColorBrush(Color.FromRgb(234, 179, 8)); // Yellow
                    StatusBadge.Background = new SolidColorBrush(Color.FromRgb(113, 63, 18));
                    break;
                case CollectorConnectionState.Offline:
                    StatusText.Text = "Collector Offline";
                    StatusText.Foreground = new SolidColorBrush(Color.FromRgb(239, 68, 68)); // Red
                    StatusBadge.Background = new SolidColorBrush(Color.FromRgb(127, 29, 29));
                    break;
                default:
                    StatusText.Text = "Disconnected";
                    StatusText.Foreground = new SolidColorBrush(Color.FromRgb(161, 161, 170));
                    StatusBadge.Background = new SolidColorBrush(Color.FromRgb(39, 39, 42));
                    break;
            }
        });
    }

    public static string FormatRate(ulong bytesPerSec)
    {
        if (bytesPerSec < 1024)
        {
            return $"{bytesPerSec} B/s";
        }
        if (bytesPerSec < 1024 * 1024)
        {
            return $"{(bytesPerSec / 1024.0):F1} KB/s";
        }
        if (bytesPerSec < 1024 * 1024 * 1024)
        {
            return $"{(bytesPerSec / (1024.0 * 1024.0)):F1} MB/s";
        }
        return $"{(bytesPerSec / (1024.0 * 1024.0 * 1024.0)):F2} GB/s";
    }
}
