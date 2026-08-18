using System;
using System.Windows.Forms;
using NetworkFlowMonitor.Services;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class TrayIconManagerTests {
    [Fact]
    public void Constructor_InitializesNotifyIconWithProperties() {
        using var tray = new TrayIconManager(
            onToggleMonitor: () => { },
            onOpenSettings: () => { },
            onExit: () => { },
            onTogglePin: () => { },
            onRestartCollector: () => { }
        );

        Assert.NotNull(tray.NotifyIcon);
        Assert.Equal(LocalizationService.BrandName, tray.NotifyIcon.Text);
        Assert.NotNull(tray.ContextMenuStrip);
        Assert.Equal(7, tray.ContextMenuStrip.Items.Count);
        Assert.True(tray.IsPanelVisible);
        Assert.False(tray.IsPinned);
        Assert.True(tray.IsCollectorOnline);
    }

    [Fact]
    public void Constructor_NullArguments_ThrowsArgumentNullException() {
        var dummy = () => { };

        Assert.Throws<ArgumentNullException>(() => new TrayIconManager(null!, dummy, dummy, dummy, dummy));
        Assert.Throws<ArgumentNullException>(() => new TrayIconManager(dummy, null!, dummy, dummy, dummy));
        Assert.Throws<ArgumentNullException>(() => new TrayIconManager(dummy, dummy, null!, dummy, dummy));
        Assert.Throws<ArgumentNullException>(() => new TrayIconManager(dummy, dummy, dummy, null!, dummy));
        Assert.Throws<ArgumentNullException>(() => new TrayIconManager(dummy, dummy, dummy, dummy, null!));
    }

    [Fact]
    public void ContextMenu_InitialItemsMatchState_VisibleAndOnline() {
        LocalizationService.CurrentLanguage = AppLanguage.English;

        try {
            using var tray = new TrayIconManager(
                onToggleMonitor: () => { },
                onOpenSettings: () => { },
                onExit: () => { },
                onTogglePin: () => { },
                onRestartCollector: () => { },
                initialPanelVisible: true,
                initialPinned: false,
                initialCollectorOnline: true
            );

            var items = tray.ContextMenuStrip!.Items;
            Assert.Equal(7, items.Count);

            // Item 0: Hide Monitor
            var showHideItem = Assert.IsType<ToolStripMenuItem>(items[0]);
            Assert.Equal(LocalizationService.HideMonitor, showHideItem.Text);

            // Item 1: Pin
            var pinItem = Assert.IsType<ToolStripMenuItem>(items[1]);
            Assert.Equal(LocalizationService.Pin, pinItem.Text);
            Assert.False(pinItem.Checked);

            // Item 2: Separator
            Assert.IsType<ToolStripSeparator>(items[2]);

            // Item 3: Restart Collector
            var collectorItem = Assert.IsType<ToolStripMenuItem>(items[3]);
            Assert.Equal(LocalizationService.RestartCollector, collectorItem.Text);

            // Item 4: Settings
            var settingsItem = Assert.IsType<ToolStripMenuItem>(items[4]);
            Assert.Equal(LocalizationService.Settings, settingsItem.Text);

            // Item 5: Separator
            Assert.IsType<ToolStripSeparator>(items[5]);

            // Item 6: Exit
            var exitItem = Assert.IsType<ToolStripMenuItem>(items[6]);
            Assert.Equal(LocalizationService.Exit, exitItem.Text);
        } finally {
            LocalizationService.CurrentLanguage = AppLanguage.System;
        }
    }

    [Fact]
    public void UpdateContextMenu_ReflectsNewState_HiddenAndPinnedAndOffline() {
        LocalizationService.CurrentLanguage = AppLanguage.English;

        try {
            using var tray = new TrayIconManager(
                onToggleMonitor: () => { },
                onOpenSettings: () => { },
                onExit: () => { },
                onTogglePin: () => { },
                onRestartCollector: () => { },
                initialPanelVisible: true,
                initialPinned: false,
                initialCollectorOnline: true
            );

            tray.UpdateContextMenu(isPanelVisible: false, isPinned: true, isCollectorOnline: false);

            Assert.False(tray.IsPanelVisible);
            Assert.True(tray.IsPinned);
            Assert.False(tray.IsCollectorOnline);

            var items = tray.ContextMenuStrip!.Items;

            // Item 0: Show Monitor (since panel is hidden)
            var showHideItem = Assert.IsType<ToolStripMenuItem>(items[0]);
            Assert.Equal(LocalizationService.ShowMonitor, showHideItem.Text);

            // Item 1: Pin (Checked = true)
            var pinItem = Assert.IsType<ToolStripMenuItem>(items[1]);
            Assert.Equal(LocalizationService.Pin, pinItem.Text);
            Assert.True(pinItem.Checked);

            // Item 3: Retry Collector (since collector is offline)
            var collectorItem = Assert.IsType<ToolStripMenuItem>(items[3]);
            Assert.Equal(LocalizationService.RetryCollector, collectorItem.Text);
        } finally {
            LocalizationService.CurrentLanguage = AppLanguage.System;
        }
    }

    [Fact]
    public void LocalizationService_LanguageChanged_UpdatesContextMenuTextsDynamically() {
        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;

        try {
            using var tray = new TrayIconManager(
                onToggleMonitor: () => { },
                onOpenSettings: () => { },
                onExit: () => { },
                onTogglePin: () => { },
                onRestartCollector: () => { },
                initialPanelVisible: true,
                initialPinned: false,
                initialCollectorOnline: true
            );

            // Verify Chinese
            var itemsZh = tray.ContextMenuStrip!.Items;
            Assert.Equal("隐藏监控", itemsZh[0].Text);
            Assert.Equal("固定显示", itemsZh[1].Text);
            Assert.Equal("重启采集", itemsZh[3].Text);
            Assert.Equal("设置", itemsZh[4].Text);
            Assert.Equal("退出", itemsZh[6].Text);

            // Switch to English
            LocalizationService.CurrentLanguage = AppLanguage.English;

            var itemsEn = tray.ContextMenuStrip!.Items;
            Assert.Equal("Hide Monitor", itemsEn[0].Text);
            Assert.Equal("Pin", itemsEn[1].Text);
            Assert.Equal("Restart Collector", itemsEn[3].Text);
            Assert.Equal("Settings", itemsEn[4].Text);
            Assert.Equal("Exit", itemsEn[6].Text);
        } finally {
            LocalizationService.CurrentLanguage = AppLanguage.System;
        }
    }

    [Fact]
    public void ContextMenu_ItemClicks_TriggerAssignedCallbacks() {
        var toggleMonitorCalled = 0;
        var openSettingsCalled = 0;
        var exitCalled = 0;
        var togglePinCalled = 0;
        var restartCollectorCalled = 0;

        using var tray = new TrayIconManager(
            onToggleMonitor: () => toggleMonitorCalled++,
            onOpenSettings: () => openSettingsCalled++,
            onExit: () => exitCalled++,
            onTogglePin: () => togglePinCalled++,
            onRestartCollector: () => restartCollectorCalled++
        );

        var items = tray.ContextMenuStrip!.Items;

        // Click Item 0 (Toggle Monitor)
        ((ToolStripMenuItem)items[0]).PerformClick();
        Assert.Equal(1, toggleMonitorCalled);

        // Click Item 1 (Toggle Pin)
        ((ToolStripMenuItem)items[1]).PerformClick();
        Assert.Equal(1, togglePinCalled);

        // Click Item 3 (Restart Collector)
        ((ToolStripMenuItem)items[3]).PerformClick();
        Assert.Equal(1, restartCollectorCalled);

        // Click Item 4 (Settings)
        ((ToolStripMenuItem)items[4]).PerformClick();
        Assert.Equal(1, openSettingsCalled);

        // Click Item 6 (Exit)
        ((ToolStripMenuItem)items[6]).PerformClick();
        Assert.Equal(1, exitCalled);
    }

    [Fact]
    public void Dispose_HidesAndDisposesNotifyIcon_UnsubscribesLanguageChanged() {
        var tray = new TrayIconManager(
            onToggleMonitor: () => { },
            onOpenSettings: () => { },
            onExit: () => { },
            onTogglePin: () => { },
            onRestartCollector: () => { }
        );

        Assert.True(tray.NotifyIcon.Visible);

        tray.Dispose();

        Assert.False(tray.NotifyIcon.Visible);

        // Changing language after dispose shouldn't throw
        LocalizationService.CurrentLanguage = AppLanguage.English;
        LocalizationService.CurrentLanguage = AppLanguage.SimplifiedChinese;
        LocalizationService.CurrentLanguage = AppLanguage.System;
    }
}
