using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using NetworkFlowMonitor.Ipc;
using NetworkFlowMonitor.Store;

namespace NetworkFlowMonitor.Views;

public sealed record RankingRowItem(
    string DisplayName,
    string ExecutablePath,
    string DownloadRateText,
    string UploadRateText
);

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
            DownloadRateText.Text = RateFormatter.FormatRate(snapshot.DownloadBytesPerSec);
            UploadRateText.Text = RateFormatter.FormatRate(snapshot.UploadBytesPerSec);
            SeqText.Text = $"Seq: {snapshot.Seq}";

            var statusDiag = snapshot.LatestStatus != null
                ? $"ETW: {(snapshot.LatestStatus.EtwSessionActive ? "Active" : "Inactive")} | PIDs: {snapshot.LatestStatus.ActivePids} | Drop: {snapshot.LatestStatus.DroppedPackets}"
                : $"PID: {(snapshot.CollectorPid != 0 ? snapshot.CollectorPid.ToString() : "N/A")}";
            DiagnosticsText.Text = statusDiag;

            switch (snapshot.ConnectionState)
            {
                case CollectorConnectionState.Connected:
                    switch (snapshot.DataState)
                    {
                        case TelemetryDataState.Warmup:
                            StatusText.Text = "Collecting data…";
                            StatusText.Foreground = new SolidColorBrush(Color.FromRgb(234, 179, 8)); // Yellow
                            StatusBadge.Background = new SolidColorBrush(Color.FromRgb(113, 63, 18));
                            break;
                        case TelemetryDataState.Idle:
                            StatusText.Text = "No network activity";
                            StatusText.Foreground = new SolidColorBrush(Color.FromRgb(161, 161, 170));
                            StatusBadge.Background = new SolidColorBrush(Color.FromRgb(39, 39, 42));
                            break;
                        default:
                            StatusText.Text = "Connected";
                            StatusText.Foreground = new SolidColorBrush(Color.FromRgb(34, 197, 94)); // Green
                            StatusBadge.Background = new SolidColorBrush(Color.FromRgb(20, 83, 45));
                            break;
                    }
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

            // Ranking table display
            if (snapshot.ConnectionState != CollectorConnectionState.Connected)
            {
                RankingStatusText.Text = "Collector offline / Disconnected";
                RankingStatusText.Visibility = Visibility.Visible;
                RankingItemsList.ItemsSource = null;
            }
            else if (snapshot.DataState == TelemetryDataState.Warmup)
            {
                RankingStatusText.Text = "Collecting data…";
                RankingStatusText.Visibility = Visibility.Visible;
                RankingItemsList.ItemsSource = null;
            }
            else if (snapshot.DataState == TelemetryDataState.Idle)
            {
                RankingStatusText.Text = "No network activity";
                RankingStatusText.Visibility = Visibility.Visible;
                RankingItemsList.ItemsSource = null;
            }
            else
            {
                RankingStatusText.Visibility = Visibility.Collapsed;
                var items = snapshot.LatestRanking?.Applications?
                    .Select(app => new RankingRowItem(
                        DisplayName: string.IsNullOrWhiteSpace(app.DisplayName) ? app.Id : app.DisplayName,
                        ExecutablePath: app.ExecutablePath,
                        DownloadRateText: RateFormatter.FormatRate(app.DownloadBytesPerSec),
                        UploadRateText: RateFormatter.FormatRate(app.UploadBytesPerSec)
                    ))
                    .ToList() ?? [];
                RankingItemsList.ItemsSource = items;
            }
        });
    }

    public static string FormatRate(ulong bytesPerSec) => RateFormatter.FormatRate(bytesPerSec);
}
