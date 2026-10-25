using System.Globalization;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.AppLifecycle;
using Microsoft.Windows.AppNotifications;
using Microsoft.Windows.AppNotifications.Builder;
using SCSKiller.App.Design;
using SCSKiller.App.Pages;
using SCSKiller.Core;
using SCSKiller.Core.App;

namespace SCSKiller.App;

public partial class App : Application
{
    public static IScsKiller Core { get; private set; } = null!;
    public static MainWindow Main { get; private set; } = null!;
    /// <summary>Sign in with Patreon: <see cref="Account.SignInAsync"/> is the entry point for Settings and the welcome dialog.</summary>
    public static Account Account { get; private set; } = null!;
    /// <summary>The data folder's store: the real one, or the design data's temporary folder.</summary>
    public static AppStore Store { get; private set; } = null!;
    /// <summary>The signed news.json in use (the bell's notifications).</summary>
    public static News News => Core is ScsKiller k ? k.News : FakeScsKiller.SampleNews;

    static readonly TimeSpan SessionEndWait = TimeSpan.FromSeconds(10);

    static Tray? tray;
    static bool notifications, quitting, checkingDriver, checkAgain;
    static string? toldDriver;
    static Dictionary<string, string>? told;
    static readonly HashSet<string> autoQueued = [];
    static readonly DateTimeOffset session = DateTimeOffset.UtcNow;
    static Coalesced? newShadersCheck;
    static TaskCompletionSource? quitNow;
    static readonly CancellationTokenSource stopWatching = new();
    static Task watcher = Task.CompletedTask;

    public App()
    {
        UnhandledException += (_, e) => Program.LogCrash(e.Exception);
        InitializeComponent();
    }

    public static bool Quitting => quitting;
    public static bool HidesOnClose => tray is { Added: true };

    protected override void OnLaunched(LaunchActivatedEventArgs e)
    {
        var culture = new CultureInfo("tr-TR");
        CultureInfo.CurrentCulture = culture;
        CultureInfo.CurrentUICulture = culture;

        try { Launch(); }
        catch (Exception ex)
        {
            Program.StartFailed(ex, Environment.GetCommandLineArgs());
            throw;
        }
    }

    // Args: --fake ...
    void Launch()
    {
        var args = Environment.GetCommandLineArgs();
        int shots = Array.IndexOf(args, "--screenshots");
        bool driverUpdated = args.Contains("--driver-updated");
        Core = shots >= 0 || args.Contains("--fake") ? new FakeScsKiller() : ScsKiller.CreateDefault();
        var fakeDir = Path.Combine(Path.GetTempPath(), $"SCSKiller-fake-{Environment.ProcessId}");
        Account = shots >= 0 ? FakeAccount.Create(fakeDir) : new Account(Core is FakeScsKiller ? fakeDir : AppStore.DefaultDir);
        Store = Core is ScsKiller ks ? ks.Store : new AppStore(fakeDir);

        if (Core is ScsKiller real)
        {
            real.Log = new AppLog(real.Store.DataDir);
            Codecs.Log = real.Log;
            real.Log.Report($"SCSKiller {AppVersion.Current} started on Windows {Environment.OSVersion.Version}, {real.Vendor.Gpu.Name}, driver {real.Vendor.Gpu.DriverVersion}");
            real.Community = new Community(AppStore.DefaultDir, Account.GetDbTokenAsync) { TokenWith = Account.GetTokenAsync };
            real.DbEntitled = () => Account.SignedIn && (Account.Status == null || Account.HasDb);
            real.Sharing = new Sharing(AppStore.DefaultDir, () => real.Settings.ShareRecordings);
            real.Diagnostics = new Diagnostics(() => real.Settings.ShareDiagnostics);
            real.ContentRoutes = RouteFailover.Default;
            real.ChannelAccess = async () => (await Account.GetAccessTokenAsync(), Account.Status?.Ent);
            real.UserFetch = async () => { await Account.RefreshAsync(); await Updater.CheckAsync(); };
            real.ActiveCheck = new ActiveCheck(AppStore.DefaultDir, () => ActiveCheck.Allowed(real.Settings), real.Vendor.Vendor);
            real.ManageRecorders = !driverUpdated;
            real.CleanupHelper = Environment.ProcessPath;
            real.CheckPlans = true;
            var hadDb = false;
            Account.Changed += () => { var db = Account.Status?.Ent.Contains("db") == true; if (db && !hadDb) real.StartCommunitySync(); hadDb = db; };
            void Channels() { if (!Account.SignedIn || Account.Status != null) real.UseChannels(Account.Status?.Ent); }
            Account.Changed += Channels;
            Channels();
            watcher = real.WatchGames(stopWatching.Token);
        }

        Main = new MainWindow();
        Main.ApplyTheme(Core.Settings.Theme);
        if (shots < 0) Main.RestorePlacement(Core is ScsKiller own ? own.Store : new AppStore(fakeDir));
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(Main);
        if (Core is ScsKiller seen) seen.Unseen = () => !IsWindowVisible(hwnd) || IsIconic(hwnd);

        string? toastError = null;
        try
        {
            AppNotificationManager.Default.NotificationInvoked += (_, a) => Main.DispatcherQueue.TryEnqueue(() => OnToast(a.Arguments));
            AppNotificationManager.Default.Register();
        }
        catch (Exception ex) { toastError = ex.Message; }
        notifications = toastError == null;
        var activation = AppInstance.GetCurrent().GetActivatedEventArgs();

        if (shots >= 0)
        {
            string dir = shots + 1 < args.Length ? args[shots + 1] : "screenshots";
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "toast.xml"), toastError ?? DriverToast(Core.Games.Take(1).ToList()).Payload);
            if (toastError == null)
            {
                File.WriteAllText(Path.Combine(dir, "toast-new-shaders.xml"), NewShadersToast([Core.Games[0] with { RecordedSinceWarm = 42 }]).Payload);
                File.WriteAllText(Path.Combine(dir, "toast-new-shaders-games.xml"), NewShadersToast(Core.Games.Take(3).ToList()).Payload);
                File.WriteAllText(Path.Combine(dir, "toast-new-shaders-compiling.xml"), NewShadersToast([Core.Games[0] with { RecordedSinceWarm = 42 }], compiling: true).Payload);
            }
            _ = Main.TakeScreenshotsAsync(dir, (FakeScsKiller)Core);
            return;
        }

        BeforeAfterUi.Watch();

        if (!Core.Settings.WelcomeSeen)
        {
            WhatsNew.AtStart(Store, firstRun: true, AppVersion.Current);
            WhenShown(root => ShowWelcomeAsync(root, Core is FakeScsKiller ? Task.FromResult<WelcomeContent?>(null) : WelcomeContent.FetchAsync()));
        }
        else WhenShown(async root =>
        {
            if (ActiveCheck.Asks(Core.Settings)) await AskActiveCheckAsync(root);
            if (Diagnostics.Asks(Core.Settings)) await AskDiagnosticsAsync(root);
            if (WhatsNew.AtStart(Store, firstRun: false, AppVersion.Current) is { } news) await ShowWhatsNewAsync(root, news);
        });

        tray = CreateTray();
        UpdateTip();
        if (Core is not FakeScsKiller)
        {
            AppInstance.GetCurrent().Activated += (_, a) => Main.DispatcherQueue.TryEnqueue(() => OnActivated(a));
            Main.Activated += (_, a) =>
            {
                if (a.WindowActivationState == WindowActivationState.Deactivated || Core is not ScsKiller { ManageRecorders: false } r) return;
                r.ManageRecorders = true;
                Task.Run(() => r.ReconcileRecorders());
            };
            var lastSizes = DateTime.MinValue;
            Main.Activated += (_, a) =>
            {
                if (a.WindowActivationState == WindowActivationState.Deactivated || DateTime.UtcNow - lastSizes < TimeSpan.FromSeconds(10)) return;
                lastSizes = DateTime.UtcNow;
                Task.Run(Core.RefreshCacheSizes);
            };
            if (Core is ScsKiller gpu) gpu.GpuChanged += () => Main.DispatcherQueue.TryEnqueue(OnGpuChanged);
            ApplyStartWithWindows(byUser: false);
            Updater.Start();
            if (activation.Kind == ExtendedActivationKind.Launch) _ = Updater.ApplyAtStartAsync(args[1..]);
            if (Core is ScsKiller k && !driverUpdated)
            {
                var check = newShadersCheck = new Coalesced(Main.DispatcherQueue, () => NotifyNewShaders(k.Store));
                Core.GameChanged += _ => check.Request();
                Core.QueueChanged += q => { if (q.Stage is QueueStage.Indexing or QueueStage.Done or QueueStage.Failed or QueueStage.Stopped) check.Request(); };
            }
            if (Updater.HasResume) _ = Core.ScanAsync(CancellationToken.None).ContinueWith(_ => Main.DispatcherQueue.TryEnqueue(Updater.ResumeQueue));
            else if (args.Contains(WindowsStartup.TrayArg)) _ = Core.ScanAsync(CancellationToken.None);
        }

        if (activation.Kind == ExtendedActivationKind.AppNotification)
        {
            var toastArgs = ((AppNotificationActivatedEventArgs)activation.Data).Arguments;
            OnToast(toastArgs, relaunched: true);
            return;
        }

        if (driverUpdated)
        {
            NotifyDriverUpdate(exitIfNothing: true);
            return;
        }

        if (!args.Contains(WindowsStartup.TrayArg)) Main.Activate();
        ShowDriverToast();
    }

    static void HideToTray()
    {
        Main.AppWindow.Hide();
        if (Core.Settings.TrayNoticeShown || !notifications) return;
        Core.Settings = Core.Settings with { TrayNoticeShown = true };
        AppNotificationManager.Default.Show(new AppNotificationBuilder()
            .AddArgument("action", "open")
            .AddText("SCSKiller çalışmaya devam ediyor")
            .AddText("Bildirim alanında çalışıyor; çıkmak için simgenin üzerine sağ tıklayın.")
            .BuildNotification());
    }

    public static void Hint(string title, string text)
    {
        if (!notifications) return;
        var toast = new AppNotificationBuilder().AddText(title).AddText(text).BuildNotification();
        toast.Expiration = DateTimeOffset.Now.AddMinutes(5);
        AppNotificationManager.Default.Show(toast);
    }

    public static ContentDialog ActiveCheckDialog(XamlRoot root) => new()
    {
        XamlRoot = root, Title = "Günlük anonim kontrol",
        Content = new StackPanel
        {
            Spacing = 8,
            Children =
            {
                new TextBlock
                {
                    Text = "Günde bir kez SCSKiller, uygulama sürümünü ve GPU üreticisini içeren anonim bir günlük kontrol gönderebilir. Bu, aktif kurulumları saymamıza yardımcı olur. Bunu açana kadar kapalıdır.",
                    TextWrapping = TextWrapping.Wrap,
                },
                new TextBlock { Text = "İsterseniz Ayarlar bölümünden değiştirin.", TextWrapping = TextWrapping.Wrap, Style = (Style)Current.Resources["Caption"] },
            },
        },
        PrimaryButtonText = "Aç", CloseButtonText = "Kapalı tut", DefaultButton = ContentDialogButton.None,
    };

    static async Task AskDiagnosticsAsync(XamlRoot root)
    {
        var dialog = new ContentDialog
        {
            XamlRoot = root, Title = "Sorunları çözmeye yardımcı olmak için tanı verilerini paylaş?",
            Content = new TextBlock
            {
                Text = "Bir derleme başarısız olduğunda, her derleme için bir kez hata kodlarını gönderin. Ayarlar bölümünden bunu değiştirebilirsiniz.",
                TextWrapping = TextWrapping.Wrap,
            },
            PrimaryButtonText = "Tanı verilerini paylaş", CloseButtonText = "Hayır, teşekkürler", DefaultButton = ContentDialogButton.Close,
        };
        ContentDialogResult result;
        try { result = await Themed(dialog).ShowAsync(); }
        catch (COMException) { return; }
        Core.Settings = Diagnostics.Answered(Core.Settings, result == ContentDialogResult.Primary);
    }

    public static ContentDialog WhatsNewDialog(XamlRoot root, WhatsNewSection section)
    {
        var panel = new StackPanel { Spacing = 10 };
        foreach (var b in section.Bullets)
        {
            var text = new TextBlock { TextWrapping = TextWrapping.Wrap };
            text.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = "• " });
            foreach (var s in WhatsNew.Spans(b))
            {
                Microsoft.UI.Xaml.Documents.Inline inline = s.Kind switch
                {
                    SpanKind.Bold => new Microsoft.UI.Xaml.Documents.Bold { Inlines = { new Microsoft.UI.Xaml.Documents.Run { Text = s.Text } } },
                    SpanKind.Code => new Microsoft.UI.Xaml.Documents.Run { Text = s.Text, FontFamily = new("Consolas") },
                    SpanKind.Link => new Microsoft.UI.Xaml.Documents.Hyperlink { NavigateUri = new(s.Url!), Inlines = { new Microsoft.UI.Xaml.Documents.Run { Text = s.Text } } },
                    _ => new Microsoft.UI.Xaml.Documents.Run { Text = s.Text },
                };
                text.Inlines.Add(inline);
            }
            panel.Children.Add(text);
        }
        return new ContentDialog { XamlRoot = root, Title = $"{section.Version} sürümünde yenilikler", Content = new ScrollViewer { Content = panel }, CloseButtonText = "Kapat" };
    }

    public static ContentDialog Confirm(Page page, string title, object content, string primaryText, ContentDialogButton defaultButton = ContentDialogButton.Close) => new()
    {
        XamlRoot = page.XamlRoot, Title = title, Content = content, PrimaryButtonText = primaryText, CloseButtonText = "İptal", DefaultButton = defaultButton,
    };

    static AppNotification DriverToast(IReadOnlyList<GameState> stale)
    {
        var time = TimeSpan.FromTicks(stale.Sum(g => (g.EstimatedWarmTime ?? TimeSpan.Zero).Ticks));
        string games = stale.Count == 1 ? "1 oyun gerekiyor" : $"{stale.Count} oyun gerekiyor";
        return new AppNotificationBuilder()
            .AddArgument("action", "open")
            .AddText($"{Fmt.Vendor(Core.Vendor.Vendor)} sürücüsü güncellendi")
            .AddText($"Sürücü {Core.Vendor.Gpu.DriverVersion} shader önbelleğini temizledi. {games} yeniden oluşturuluyor: " +
                     $"{string.Join(", ", stale.Select(g => g.Game.Name))}" + (time > TimeSpan.Zero ? $", yaklaşık {Format.Duration(time)}." : "."))
            .AddButton(new AppNotificationButton("Şimdi derle").AddArgument("action", "now"))
            .AddButton(new AppNotificationButton("Boşta olduğunda").AddArgument("action", "idle"))
            .AddButton(new AppNotificationButton("Atla").AddArgument("action", "skip"))
            .BuildNotification();
    }

    static AppNotification NewShadersToast(IReadOnlyList<GameState> games, bool compiling = false)
    {
        var ids = string.Join('|', games.Select(g => g.Game.Id));
        var n = NewShaders.Count(games[0]);
        var b = new AppNotificationBuilder()
            .AddArgument("action", "shaders-show").AddArgument("games", ids);
        if (compiling)
            b.AddText("Yeni shader'lar derleniyor")
                .AddText(games.Count == 1 ? $"{games[0].Game.Name} adlı oyunda {n:N0} yeni pipeline var. Bilgisayar boşta olduğunda derleniyor."
                    : $"{games.Count} oyunda yeni shader var. Bilgisayar boşta olduğunda derleniyor.");
        else
            b.AddText("Derlenecek yeni shader'lar")
                .AddText(games.Count == 1 ? $"{games[0].Game.Name} adlı oyunda {n:N0} yeni pipeline var. Bunları derleyip stutter'ı önleyin."
                    : $"{games.Count} oyunda derlenecek yeni shader var.");
        return b.AddButton(new AppNotificationButton("Şimdi derle").AddArgument("action", "shaders-compile").AddArgument("games", ids))
            .AddButton(new AppNotificationButton("Göster").AddArgument("action", "shaders-show").AddArgument("games", ids))
            .BuildNotification();
    }
}
