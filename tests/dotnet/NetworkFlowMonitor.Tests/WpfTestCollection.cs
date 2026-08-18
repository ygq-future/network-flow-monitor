using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;
using Xunit;

[assembly: CollectionBehavior(DisableTestParallelization = true)]

namespace NetworkFlowMonitor.Tests;

[CollectionDefinition("WpfTestFixtureGroup", DisableParallelization = true)]
public class WpfTestFixtureGroup {
}

public static class WpfTestRunner {
    private static readonly Lazy<Dispatcher> _dispatcherLazy = new(() => {
        var tcs = new TaskCompletionSource<Dispatcher>();
        var thread = new Thread(() => {
            if (Application.Current == null) {
                _ = new Application {
                    ShutdownMode = ShutdownMode.OnExplicitShutdown
                };
            }
            tcs.SetResult(Dispatcher.CurrentDispatcher);
            Dispatcher.Run();
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Name = "WpfTestRunnerSTA";
        thread.Start();
        return tcs.Task.GetAwaiter().GetResult();
    });

    public static Dispatcher Dispatcher => _dispatcherLazy.Value;

    public static void Run(Action action) {
        Exception? error = null;
        Dispatcher.Invoke(() => {
            try {
                action();
            } catch (Exception ex) {
                error = ex;
            }
        });
        if (error != null) {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error).Throw();
        }
    }

    public static async Task RunAsync(Func<Task> asyncAction) {
        Exception? error = null;
        await Dispatcher.InvokeAsync(async () => {
            try {
                await asyncAction();
            } catch (Exception ex) {
                error = ex;
            }
        }).Task.Unwrap();
        if (error != null) {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error).Throw();
        }
    }
}
