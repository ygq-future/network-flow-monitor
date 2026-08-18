using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

[Collection("WpfTestFixtureGroup")]
public class WindowInteractionTests {
    [Fact]
    public void MonitorPanelWindow_ContextMenuOpen_DoesNotThrow() {
        WpfTestRunner.Run(() => {
            var dict = new ResourceDictionary {
                Source = new Uri("pack://application:,,,/NetworkFlowMonitor;component/Resources/FluentStyles.xaml", UriKind.Absolute)
            };
            Application.Current!.Resources.MergedDictionaries.Add(dict);

            var window = new MonitorPanelWindow();
            // Create HWND headlessly (SW_HIDE, zero visual popup on screen)
            var helper = new WindowInteropHelper(window);
            helper.EnsureHandle();

            // Raise right button up to open context menu
            var mouseDevice = Mouse.PrimaryDevice;
            if (mouseDevice != null) {
                var eventArgs = new MouseButtonEventArgs(mouseDevice, 0, MouseButton.Right) {
                    RoutedEvent = UIElement.MouseRightButtonUpEvent,
                    Source = window
                };
                window.RaiseEvent(eventArgs);
            }

            window.Close();
        });
    }

    [Fact]
    public void MonitorPanelWindow_MenuItemClick_DispatchesEventCorrectly() {
        WpfTestRunner.Run(() => {
            bool pinDispatched = false;

            var dict = new ResourceDictionary {
                Source = new Uri("pack://application:,,,/NetworkFlowMonitor;component/Resources/FluentStyles.xaml", UriKind.Absolute)
            };
            Application.Current!.Resources.MergedDictionaries.Add(dict);

            var window = new MonitorPanelWindow();
            window.Initialize(ev => {
                if (ev.Kind == MonitorEventKind.PinTogglePressed) {
                    pinDispatched = true;
                }
            });

            // Create HWND headlessly (SW_HIDE, zero visual popup on screen)
            var helper = new WindowInteropHelper(window);
            helper.EnsureHandle();

            var mouseDevice = Mouse.PrimaryDevice;
            if (mouseDevice != null) {
                var eventArgs = new MouseButtonEventArgs(mouseDevice, 0, MouseButton.Right) {
                    RoutedEvent = UIElement.MouseRightButtonUpEvent,
                    Source = window
                };
                window.RaiseEvent(eventArgs);
            }

            var menu = window.ContextMenu;
            Assert.NotNull(menu);

            // Find Pin item (index 1)
            var pinItem = menu.Items[1] as MenuItem;
            Assert.NotNull(pinItem);

            // Raise Click
            pinItem.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent, pinItem));

            window.Close();

            Assert.True(pinDispatched, "Clicking Pin menu item must dispatch PinTogglePressed event");
        });
    }
}
