using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml.Navigation;
using SCSKiller.App.Design;
using SCSKiller.App.Pages;
using SCSKiller.Core;
using SCSKiller.Core.App;
using SCSKiller.Core.Vendors;
using Windows.Graphics;
using Windows.Graphics.Imaging;

namespace SCSKiller.App;

public sealed partial class MainWindow : Window
{
    const int MinWidth = 1072, MinHeight = 640;

    bool syncingNav;
    AppStore? placement;
    bool maximizeOnShow;
    readonly (NavigationViewItem Item, Type Page)[] pages;

    public MainWindow()
    {
        InitializeComponent();
        pages = [(QueueItem, typeof(QueuePage)), (CacheItem, typeof(CachePage)), (SettingsItem, typeof(SettingsPage)), (AboutItem, typeof(AboutPage))];
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico"));
        Resize(1280);
        SetMinimumSize();
        Root.Loaded += (_, _) => Root.XamlRoot.Changed += (_, _) => SetMinimumSize();
        ShowGpu();
        Nav.SelectedItem = LibraryItem;
        Updater.Changed += () => DispatcherQueue.TryEnqueue(ShowUpdate);
        ShowUpdate();
        var bell = new Coalesced(DispatcherQueue, ShowBell);
        App.Core.GameChanged += _ => bell.Request();
        App.Account.Changed += bell.Request;
        if (App.Core is ScsKiller k) k.NewsChanged += bell.Request;
        ShowBell();
        AppWindow.Closing += (w, e) =>
        {
            SavePlacement();
            switch (WindowClose.Of(App.Core.Settings, App.Quitting, App.HidesOnClose))
            {
                case CloseAction.Quit:
                    e.Cancel = true;
                    w.Hide();
                    _ = App.QuitAsync();
                    break;
                case CloseAction.Hide:
                    e.Cancel = true;
                    App.HideToTray();
                    break;
            }
        };
        AppWindow.Changed += (w, e) =>
        {
            if (!e.DidVisibilityChange || !w.IsVisible || !maximizeOnShow) return;
            maximizeOnShow = false;
            ((OverlappedPresenter)w.Presenter).Maximize();
        };
    }

    public void ShowGpu()
    {
        GpuName.Text = App.Core.Vendor.Gpu.Name;
        GpuDriver.Text = (App.Core as ScsKiller)?.GpuRestartNote ?? "Sürücü " + App.Core.Vendor.Gpu.DriverVersion;
    }

    public void Navigate(Type page, object? arg = null) => ContentFrame.Navigate(page, arg);

    public void NavigateToPatreon()
    {
        Navigate(typeof(SettingsPage));
        if (ContentFrame.Content is not SettingsPage settings) return;
        settings.Loaded += Scroll;
        void Scroll(object _, RoutedEventArgs __) { settings.Loaded -= Scroll; settings.ScrollToPatreon(); }
    }

    void ShowUpdate()
    {
        var ready = Updater.Ready;
        UpdateButton.Visibility = ready != null ? Visibility.Visible : Visibility.Collapsed;
        ToolTipService.SetToolTip(UpdateButton, Updater.Problem ?? $"SCSKiller {ready} hazır. Yeniden başlatma, çalışan derlemeyi güvenli şekilde durdurur " +
            "(sürücü önbelleğini önce kaydeder) ve ardından devam ettirir." +
            (Updater.InstallsAutomatically ? " Aksi halde sonraki SCSKiller başlatma veya kapanma sırasında kurulur." : ""));
    }

    static (IReadOnlyList<NewsItem> Shown, IReadOnlySet<string> Unread) Notifications() =>
        NewsState.Of(App.Store, App.News.For(new(AppVersion.Current, App.Account.Status?.Ent ?? [], App.Core.Vendor.Vendor, id => App.Core.Games.Any(g => g.Game.Id == id)),
            DateOnly.FromDateTime(DateTime.Now)));

    static HashSet<string> Feed() => App.News.Items.Select(i => i.Id).ToHashSet();

    void ShowBell()
    {
        var unread = Notifications().Unread.Count;
        BellBadge.Value = unread;
        BellBadge.Visibility = unread > 0 ? Visibility.Visible : Visibility.Collapsed;
        AutomationProperties.SetName(BellButton, unread > 0 ? $"Bildirimler, {unread} okunmamış" : "Bildirimler");
    }

    void OnBellOpening(object _, object __)
    {
        var (shown, unread) = Notifications();
        FillBell(shown, unread);
        NewsState.MarkRead(App.Store, shown.Select(i => i.Id), Feed());
        ShowBell();
    }

    void FillBell(IReadOnlyList<NewsItem> shown, IReadOnlySet<string> unread)
    {
        var list = new StackPanel { Spacing = 8 };
        foreach (var item in shown) list.Children.Add(NewsCard(item, unread.Contains(item.Id)));
        BellFlyout.Content = new StackPanel
        {
            Width = 360, Spacing = 12,
            Children =
            {
                new TextBlock { Text = "Bildirimler", Style = Res<Style>("Section") },
                shown.Count == 0 ? new TextBlock { Text = "Yeni bir şey yok.", Style = Res<Style>("Secondary") } : new ScrollViewer { MaxHeight = 480, Content = list },
            },
        };
    }

    UIElement NewsCard(NewsItem item, bool unread)
    {
        var date = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        if (unread) date.Children.Add(new Microsoft.UI.Xaml.Shapes.Ellipse { Width = 8, Height = 8, VerticalAlignment = VerticalAlignment.Center, Fill = Res<Brush>("AccentFillColorDefaultBrush") });
        date.Children.Add(new TextBlock { Text = News.Day(item.Date)!.Value.ToString("d"), Style = Res<Style>("Caption") });
        var text = new StackPanel { Spacing = 4, Children = { date } };
        text.Children.Add(new TextBlock { Text = item.Title, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap });
        text.Children.Add(new TextBlock { Text = item.Body, Style = Res<Style>("Secondary"), TextWrapping = TextWrapping.Wrap });
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(0, 4, 0, 0) };
        if (item.Link != null) actions.Children.Add(new HyperlinkButton { Content = "Aç", NavigateUri = new Uri(item.Link) });
        if (item.Game != null)
        {
            var go = new Button { Content = "Oyuna git" };
            go.Click += (_, _) => { BellFlyout.Hide(); Navigate(typeof(DetailPage), item.Game); };
            actions.Children.Add(go);
        }
        if (actions.Children.Count > 0) text.Children.Add(actions);

        var dismiss = new Button
        {
            Content = new FontIcon { Glyph = "\uE711", FontSize = 10 }, Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent), BorderThickness = new Thickness(0),
            Padding = new Thickness(6), VerticalAlignment = VerticalAlignment.Top,
        };
        ToolTipService.SetToolTip(dismiss, "Kapat");
        AutomationProperties.SetName(dismiss, "Kapat");
        dismiss.Click += (_, _) =>
        {
            NewsState.Dismiss(App.Store, item.Id, Feed());
            var (shown, _) = Notifications();
            FillBell(shown, new HashSet<string>());
            ShowBell();
        };
        var row = new Grid { ColumnSpacing = 8, ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } }, Children = { text, dismiss } };
        Grid.SetColumn(dismiss, 1);
        return new Border { Style = Res<Style>("Card"), Padding = new Thickness(12), Child = row };
    }

    static T Res<T>(string key) => (T)Application.Current.Resources[key];

    async void OnRestartToUpdate(object _, RoutedEventArgs __)
    {
        UpdateButton.IsEnabled = false;
        UpdateText.Text = "Derleme tamamlanıyor…";
        SavePlacement();
        if (await Updater.RestartAsync()) return;
        (UpdateButton.IsEnabled, UpdateText.Text) = (true, "Güncellemeyi yeniden başlat");
        ShowUpdate();
    }

    void OnNavSelection(NavigationView _, NavigationViewSelectionChangedEventArgs e)
    {
        if (syncingNav) return;
        var page = pages.FirstOrDefault(p => p.Item == e.SelectedItemContainer).Page ?? typeof(LibraryPage);
        if (ContentFrame.SourcePageType != page && !(page == typeof(LibraryPage) && ContentFrame.SourcePageType == typeof(DetailPage)))
            ContentFrame.Navigate(page);
    }

    void OnNavInvoked(NavigationView _, NavigationViewItemInvokedEventArgs e)
    {
        if (e.InvokedItemContainer == LibraryItem && ContentFrame.SourcePageType == typeof(DetailPage)) ShowLibrary();
    }

    public void ShowLibrary()
    {
        if (ContentFrame.CanGoBack && ContentFrame.BackStack[^1].SourcePageType == typeof(LibraryPage)) ContentFrame.GoBack();
        else Navigate(typeof(LibraryPage));
    }

    void OnNavigated(object _, NavigationEventArgs e)
    {
        syncingNav = true;
        Nav.SelectedItem = pages.FirstOrDefault(p => p.Page == e.SourcePageType).Item ?? LibraryItem;
        syncingNav = false;
    }

    void OnPaneToggle(TitleBar _, object __) => Nav.IsPaneOpen = !Nav.IsPaneOpen;

    double Scale => GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this)) / 96.0;

    void Resize(int width) => AppWindow.Resize(new SizeInt32((int)(width * Scale), (int)(880 * Scale)));

    void SetMinimumSize()
    {
        var presenter = (OverlappedPresenter)AppWindow.Presenter;
        (presenter.PreferredMinimumWidth, presenter.PreferredMinimumHeight) = ((int)(MinWidth * Scale), (int)(MinHeight * Scale));
    }

    public void RestorePlacement(AppStore store)
    {
        placement = store;
        if (store.LoadWindow() is not { } saved) return;
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var wp = new WindowPlacement { Length = Marshal.SizeOf<WindowPlacement>(), ShowCmd = SwHide,
                                       Normal = new(saved.Left, saved.Top, saved.Left + saved.Width, saved.Top + saved.Height) };
        var dpi = GetDpiForWindow(hwnd);
        SetWindowPlacement(hwnd, ref wp);
        if (GetDpiForWindow(hwnd) != dpi)
        {
            SetMinimumSize();
            SetWindowPlacement(hwnd, ref wp);
        }
        maximizeOnShow = saved.Maximized;
    }

    public void SavePlacement()
    {
        if (placement == null || !AppWindow.IsVisible) return;
        var wp = new WindowPlacement { Length = Marshal.SizeOf<WindowPlacement>() };
        if (!GetWindowPlacement(WinRT.Interop.WindowNative.GetWindowHandle(this), ref wp)) return;
        var r = wp.Normal;
        try { placement.SaveWindow(new(r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top, WindowBounds.WasMaximized(wp.ShowCmd, wp.Flags))); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
    }

    public void ApplyTheme(AppTheme? theme)
    {
        Root.RequestedTheme = theme switch { AppTheme.Light => ElementTheme.Light, AppTheme.Dark => ElementTheme.Dark, _ => ElementTheme.Default };
        AppWindow.TitleBar.PreferredTheme = theme switch { AppTheme.Light => TitleBarTheme.Light, AppTheme.Dark => TitleBarTheme.Dark, _ => TitleBarTheme.UseDefaultAppMode };
    }

    public ElementTheme Theme => Root.RequestedTheme;

    public void ShowOffscreen()
    {
        AppWindow.IsShownInSwitchers = false;
        AppWindow.Move(new PointInt32(-20000, -20000));
        AppWindow.Show(false);
        Bindings.Update();
    }

    static async Task SavePngAsync(UIElement element, string path)
    {
        var rtb = new RenderTargetBitmap();
        await rtb.RenderAsync(element);
        var pixels = await rtb.GetPixelsAsync();
        using var file = File.Create(path);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, file.AsRandomAccessStream());
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)rtb.PixelWidth, (uint)rtb.PixelHeight, 96, 96, pixels.ToArray());
        await encoder.FlushAsync();
    }

    [DllImport("user32.dll")] static extern uint GetDpiForWindow(nint hwnd);
    [DllImport("user32.dll")] static extern bool GetWindowPlacement(nint hwnd, ref WindowPlacement wp);
    [DllImport("user32.dll")] static extern bool SetWindowPlacement(nint hwnd, ref WindowPlacement wp);

    const int SwHide = 0;

    [StructLayout(LayoutKind.Sequential)] record struct NativeRect(int Left, int Top, int Right, int Bottom);
    [StructLayout(LayoutKind.Sequential)] struct WindowPlacement { public int Length, Flags, ShowCmd; public PointInt32 Min, Max; public NativeRect Normal; }
}
