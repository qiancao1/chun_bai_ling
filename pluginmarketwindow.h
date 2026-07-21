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
    QString downloadUrl;
    QString gitee;                // 可选 Gitee 地址
    bool isInstalled;
    bool hasUpdate;
    int installedVersionCode;
    QString installedVersionName;
    QStringList tags;
};

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
    void onTabChanged(int index);
    void onInstallRequested(const QString &id);
    void onOpenDetail(const QString &url);
    void onReplyFinished(QNetworkReply *reply);

private:
    void setupUI();

    void filterAndDisplay();

    QTabWidget *m_tabWidget;
    QListWidget *m_listWidget;  // 当前显示的列表（每个tab共用一个）
    QLineEdit *m_searchEdit;
    QComboBox *m_categoryCombo;
    QPushButton *m_refreshBtn;
    QLabel *m_statusLabel;

    QList<PluginInfo2> m_allPlugins;
    QString m_currentCategory;
    QString m_currentKeyword;
    int m_currentTabIndex;      // 0=可用,1=已安装,2=可更新

    QNetworkAccessManager *m_networkManager;
    bool m_isLoading;
    void fetchPluginsFromGitee();
};

#endif // PLUGINMARKETWINDOW_H