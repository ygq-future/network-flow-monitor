using System;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using NetworkFlowMonitor.Presentation;
using NetworkFlowMonitor.Views;
using Xunit;

namespace NetworkFlowMonitor.Tests;

public class WindowInteractionTests
{
    [Fact]
    public void MonitorPanelWindow_ContextMenuOpen_DoesNotThrow()
    {
        Exception? error = null;
        var thread = new Thread(() =>
        {
            try
            {
                if (Application.Current == null)
                {
                    _ = new Application();
                }

                var dict = new ResourceDictionary
                {
                    Source = new Uri("pack://application:,,,/NetworkFlowMonitor;component/Resources/FluentStyles.xaml", UriKind.Absolute)
                };
                Application.Current.Resources.MergedDictionaries.Add(dict);

                var window = new MonitorPanelWindow();
                window.Show();

                var mouseDevice = Mouse.PrimaryDevice;
                var eventArgs = new MouseButtonEventArgs(mouseDevice, 0, MouseButton.Right)
                {
                    RoutedEvent = UIElement.MouseRightButtonUpEvent,
                    Source = window
                };
                window.RaiseEvent(eventArgs);

                window.Close();
            }
            catch (Exception ex)
            {
                error = ex;
            }
        });

        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        if (error != null)
        {
            throw new InvalidOperationException($"ContextMenu failed: {error.Message}", error);
        }
    }

    [Fact]
    public void MonitorPanelWindow_MenuItemClick_DispatchesEventCorrectly()
    {
        bool pinDispatched = false;
        Exception? error = null;

        var thread = new Thread(() =>
        {
            try
            {
                if (Application.Current == null)
                {
                    _ = new Application();
                }

                var dict = new ResourceDictionary
                {
                    Source = new Uri("pack://application:,,,/NetworkFlowMonitor;component/Resources/FluentStyles.xaml", UriKind.Absolute)
                };
                Application.Current.Resources.MergedDictionaries.Add(dict);

                var window = new MonitorPanelWindow();
                window.Initialize(ev =>
                {
                    if (ev.Kind == MonitorEventKind.PinTogglePressed)
                    {
                        pinDispatched = true;
                    }
                });

                window.Show();

                // Open context menu
                var eventArgs = new MouseButtonEventArgs(Mouse.PrimaryDevice, 0, MouseButton.Right)
                {
                    RoutedEvent = UIElement.MouseRightButtonUpEvent,
                    Source = window
                };
                window.RaiseEvent(eventArgs);

                var menu = window.ContextMenu;
                Assert.NotNull(menu);

                // Find Pin item (index 1)
                var pinItem = menu.Items[1] as MenuItem;
                Assert.NotNull(pinItem);

                // Raise Click
                pinItem.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent, pinItem));

                window.Close();
            }
            catch (Exception ex)
            {
                error = ex;
            }
        });

        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();

        if (error != null)
        {
            throw error;
        }

        Assert.True(pinDispatched, "Clicking Pin menu item must dispatch PinTogglePressed event");
    }
}
