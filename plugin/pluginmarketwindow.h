#ifndef PLUGINMARKETWINDOW_H
#define PLUGINMARKETWINDOW_H

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QListWidgetItem>
#include <QUrl>
#include <QDesktopServices>
#include <qnetworkaccessmanager.h>
#include <qprogressdialog.h>

// 插件信息结构
struct PluginInfo2 {
    QString id;
    QString name;
    QString iconPath;
    QString remark;
    QString author;
    int versionCode;              // 整数版本号（用于比较更新）
    QString versionName;          // 显示版本号
    QString detailUrl;
    QString downloadUrl;          // 通用包 / Windows 包地址（JSON 的 downloadUrl）
    QString downloadUrlLinux;     // 仅原生库（DLL / DLL32）：Linux 包地址（JSON 的 downloadUrl_linux，空 = 还没发 Linux 版）
    QString index;                // 仅原生库（DLL / DLL32）：入口文件基名（不含平台后缀，如「纯白世界」）；Python / JS 不写

    bool isInstalled=false;
    bool hasUpdate=false;
    QString type;
    QString installedVersionName;
    QStringList tags;
};

// ---------------------------------------------------------------------------
// 插件市场的平台差异（入口文件 / 下载地址 / 平台标签）
// ---------------------------------------------------------------------------
// 市场 JSON 的平台约定（**index 与 downloadUrl_linux 只有原生库 DLL / DLL32 才有**，
// Python / JS 的包本身通用，这两个字段都不写）：
//    "index"              原生库专用：入口文件的**基名**（不带后缀），装机时按当前平台补后缀：
//                           Windows → 纯白世界.dll
//                           Linux   → 纯白世界.so
//                           macOS   → 纯白世界.dylib
//                         也容忍写成带后缀的老数据（"纯白世界.dll"）：一律剥掉已知后缀再按平台补。
//    "downloadUrl"        包地址（原生库 = Windows 包；Python / JS = 通用包）
//    "downloadUrl_linux"  原生库专用：Linux 包地址（可能为空 = 发布者还没出 Linux 版）
//
// Python / JS 跨平台、包本身通用：既没有也不需要 downloadUrl_linux，直接复用 downloadUrl，
// 并自动补上 windows / linux 两个标签（方便 Python 侧按标签判断"默认支持跨平台"）。
// 原生库不同平台二进制不通用，缺哪边就是真的缺，**绝不回退** ——
// 这正是「Linux 上装 DLL 插件却拉到 Windows 包」的根因。
namespace PluginMarketMeta {

// 平台标签（写进 tags，供标签筛选与 Python 侧读取）
inline QString tagWindows() { return QStringLiteral("windows"); }
inline QString tagLinux()   { return QStringLiteral("linux"); }

// 当前平台名（提示文案用）
inline QString platformName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Windows");
#elif defined(Q_OS_MAC)
    return QStringLiteral("macOS");
#else
    return QStringLiteral("Linux");
#endif
}

// 当前平台原生插件库的后缀（含点）
inline QString nativeSuffix()
{
#ifdef Q_OS_WIN
    return QStringLiteral(".dll");
#elif defined(Q_OS_MAC)
    return QStringLiteral(".dylib");
#else
    return QStringLiteral(".so");
#endif
}

// 是否原生库类型（DLL / DLL32 —— 需要入口文件，且二进制分平台）
inline bool isNativeType(const QString &type)
{
    return type.compare(QLatin1String("DLL"), Qt::CaseInsensitive) == 0
        || type.compare(QLatin1String("DLL32"), Qt::CaseInsensitive) == 0;
}

// 当前平台该用市场 JSON 的哪个包地址字段（错误提示文案用，省得写死字段名）
inline QString downloadUrlFieldName(const QString &type)
{
#ifdef Q_OS_WIN
    Q_UNUSED(type);                                  // Windows 一律用 downloadUrl
    return QStringLiteral("downloadUrl");
#else
    // Linux / macOS：原生库用 downloadUrl_linux，Python / JS 复用通用包 downloadUrl
    return isNativeType(type) ? QStringLiteral("downloadUrl_linux")
                              : QStringLiteral("downloadUrl");
#endif
}

// index（仅原生库有）→ 当前平台的入口文件名；index 为空返回空串
inline QString entryFileName(const QString &index)
{
    QString s = index.trimmed();
    if (s.isEmpty()) return QString();
    s.replace('\\', '/');
    const QStringList known{ QStringLiteral(".dll"),
                             QStringLiteral(".dylib"),
                             QStringLiteral(".so") };
    for (const QString &ext : known) {
        if (s.endsWith(ext, Qt::CaseInsensitive)) { s.chop(ext.size()); break; }
    }
    if (s.isEmpty()) return QString();
    return s + nativeSuffix();
}

// 按当前平台挑下载地址；返回空串 = 本平台没有可用包
inline QString activeDownloadUrl(const PluginInfo2 &info)
{
    const QString win   = info.downloadUrl.trimmed();
    const QString linux = info.downloadUrlLinux.trimmed();
#ifdef Q_OS_WIN
    Q_UNUSED(linux);
    return win;
#else
    if (!linux.isEmpty()) return linux;              // 有专用包优先用
    if (isNativeType(info.type)) return QString();   // 原生库不跨平台，缺就是缺
    return win;                                      // Python / JS 包通用
#endif
}

// 本平台是否可安装
inline bool availableOnThisPlatform(const PluginInfo2 &info)
{
    return !activeDownloadUrl(info).isEmpty();
}

// 自动补平台标签（tags 里的 windows / linux 由包地址推出，作者不必手写）：
//   原生库（DLL / DLL32）：二进制分平台 —— 有哪个平台的包就加哪个平台标签
//                          （downloadUrl → windows，downloadUrl_linux → linux）；
//   Python / JS：JSON 里只有一个通用 downloadUrl，默认跨平台 ——
//                只要有包，windows + linux 直接都补上。
//   已存在的标签不重复添加（大小写不敏感）。
inline void applyPlatformTags(PluginInfo2 &info)
{
    QStringList want;
    if (isNativeType(info.type)) {
        if (!info.downloadUrl.trimmed().isEmpty())      want << tagWindows();
        if (!info.downloadUrlLinux.trimmed().isEmpty()) want << tagLinux();
    } else if (!info.downloadUrl.trimmed().isEmpty()
               || !info.downloadUrlLinux.trimmed().isEmpty()) {
        want << tagWindows() << tagLinux();
    }

    for (const QString &t : want) {
        if (!info.tags.contains(t, Qt::CaseInsensitive)) info.tags << t;
    }
}

} // namespace PluginMarketMeta

// 插件卡片（内嵌在窗口类中）
class PluginCard : public QWidget {
    Q_OBJECT
public:
    explicit PluginCard(const PluginInfo2 &info, QWidget *parent = nullptr);
    void updateStatus(bool installed);  // 更新安装状态
    QString pluginId() const { return m_info.id; }

signals:
    void installClicked(const QString &id);
    void detailClicked(const QString &url);

private slots:
    void onActionBtn();
    void onDetailBtn();


private:
    PluginInfo2 m_info;
    QLabel *m_iconLabel;
    QLabel *m_nameLabel;
    QLabel *m_remarkLabel;
    QLabel *m_tagLabel;
    QPushButton *m_actionBtn;
    QPushButton *m_detailBtn;
    QLabel *m_versionLabel;
};

// 插件市场主窗口
class PluginMarketWindow : public QDialog {
    Q_OBJECT
public:
    explicit PluginMarketWindow(QWidget *parent = nullptr);
    ~PluginMarketWindow();

private slots:
    void refreshList();
    void onSearchChanged(const QString &text);
    void onCategoryChanged(int index);
    void onCategoryChanged2(int index);
    void onTabChanged(int index);
    void onInstallRequested(const QString &id);
    void onOpenDetail(const QString &url);
    void onReplyFinished(QNetworkReply *reply);

private:
    void setupUI();
    void applyStyleSheet();
    void filterAndDisplay();
    void startDownload(PluginInfo2 *info, const QUrl &url, int redirectDepth);
    void finishInstall(PluginInfo2 *info, const QString &zipPath, QProgressDialog *progress);
    // 原生库（DLL / DLL32）自动安装：按 index 定位当前平台入口文件 → LoadPlugin。
    // 返回空串 = 已加载成功；entryOut 传出实际使用的入口文件绝对路径。
    QString installNativePlugin(const PluginInfo2 *info, const QString &targetDir,
                                QString *entryOut = nullptr);
    QTabWidget *m_tabWidget;
    QListWidget *m_listWidget;  // 当前显示的列表（每个tab共用一个）
    QLineEdit *m_searchEdit;
    QComboBox *m_categoryCombo;
    QComboBox *m_plugin_type;
    QPushButton *m_refreshBtn;
    QLabel *m_statusLabel;


    QString m_currentCategory;
    QString m_plugin_type_str;
    QString m_currentKeyword;
    int m_currentTabIndex;      // 0=可用,1=已安装,2=可更新
    int Installed_type=0;
    QNetworkAccessManager *m_networkManager;
    bool m_isLoading;
    void fetchPluginsFromGitee();
};

#endif // PLUGINMARKETWINDOW_H