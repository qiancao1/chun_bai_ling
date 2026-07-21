#include "PluginMarketWindow.h"
#include <QGridLayout>
#include <QFrame>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QTimer>
#include <QApplication>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qnetworkreply.h>
#include <qpainter.h>

// ======================== PluginCard 实现 ========================
PluginCard::PluginCard(const PluginInfo2 &info, QWidget *parent)
    : QWidget(parent), m_info(info) {
    //setFixedHeight(65);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // 图标
    m_iconLabel = new QLabel;
    m_iconLabel->setFixedSize(48, 48);
    m_iconLabel->setScaledContents(true);
    QPixmap pix(info.iconPath);
    if (pix.isNull()) {
        // 生成一个占位图标（用文字首字母）
        QPixmap fallback(48, 48);
        fallback.fill(Qt::lightGray);
        QPainter painter(&fallback);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 20));
        painter.drawText(fallback.rect(), Qt::AlignCenter, info.name.left(1));
        m_iconLabel->setPixmap(fallback);
    } else {
        m_iconLabel->setPixmap(pix);
    }

    // 名称+备注+标签（垂直布局）
    QVBoxLayout *infoLayout = new QVBoxLayout;
    m_nameLabel = new QLabel(info.name);
    m_nameLabel->setObjectName("tagLabel");   // 设置对象名
    m_remarkLabel = new QLabel(info.remark);
    m_remarkLabel->setObjectName("tagLabel");   // 设置对象名
    m_tagLabel = new QLabel(info.tags.join(" · "));
    m_tagLabel->setObjectName("tagLabel");   // 设置对象名
    infoLayout->addWidget(m_nameLabel);
    infoLayout->addWidget(m_remarkLabel);
    infoLayout->addWidget(m_tagLabel);
    infoLayout->setSpacing(2);

    // 操作按钮（右侧）
    QVBoxLayout *btnLayout = new QVBoxLayout;
    m_actionBtn = new QPushButton;
    m_actionBtn->setFixedSize(80, 28);
    updateStatus(info.isInstalled);

    m_detailBtn = new QPushButton("详情");

    btnLayout->addWidget(m_actionBtn);
    btnLayout->addWidget(m_detailBtn);
    btnLayout->setSpacing(6);

    layout->addWidget(m_iconLabel);
    layout->addLayout(infoLayout);
    layout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_actionBtn, &QPushButton::clicked, this, &PluginCard::onActionBtn);
    connect(m_detailBtn, &QPushButton::clicked, this, &PluginCard::onDetailBtn);
}

void PluginCard::updateStatus(bool installed) {
    m_info.isInstalled = installed;
    if (installed) {
        m_actionBtn->setText("已安装");
        m_actionBtn->setEnabled(false);
    } else {
        m_actionBtn->setText("安装");
        m_actionBtn->setEnabled(true);
    }
}

void PluginCard::onActionBtn() {
    emit installClicked(m_info.id);
}

void PluginCard::onDetailBtn() {
    emit detailClicked(m_info.detailUrl);
}

// ======================== PluginMarketWindow 实现 ========================
PluginMarketWindow::PluginMarketWindow(QWidget *parent)
    : QDialog(parent), m_currentTabIndex(0) {
    setWindowTitle("插件市场");
    resize(900, 650);
    setMinimumSize(800, 500);
    setupUI();
    refreshList();
}

PluginMarketWindow::~PluginMarketWindow() {}

void PluginMarketWindow::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    // ----- 顶部工具栏 -----
    QFrame *toolBar = new QFrame;
    toolBar->setFrameShape(QFrame::StyledPanel);
    toolBar->setFixedHeight(50);
    QHBoxLayout *toolLayout = new QHBoxLayout(toolBar);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("搜索插件...");
    m_searchEdit->setFixedWidth(200);

    m_categoryCombo = new QComboBox;
    m_categoryCombo->addItem("全部");
    m_categoryCombo->addItem("官方");
    m_categoryCombo->addItem("社区");
    m_categoryCombo->addItem("工具");
    m_categoryCombo->addItem("娱乐");
    m_categoryCombo->setFixedWidth(120);

    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setFixedWidth(80);

    QPushButton *closeBtn = new QPushButton("关闭");
    closeBtn->setFixedWidth(80);

    toolLayout->addWidget(m_searchEdit);
    toolLayout->addWidget(m_categoryCombo);
    toolLayout->addWidget(m_refreshBtn);
    toolLayout->addStretch();
    toolLayout->addWidget(closeBtn);

    mainLayout->addWidget(toolBar);

    // ----- Tab切换 -----
    QTabBar *tabBar = new QTabBar;
    tabBar->addTab("可用插件");
    tabBar->addTab("已安装");
    tabBar->addTab("可更新");
    tabBar->setExpanding(false);
    tabBar->setDrawBase(false);
    mainLayout->addWidget(tabBar);
    connect(tabBar, &QTabBar::currentChanged, this, &PluginMarketWindow::onTabChanged);

    // ----- 列表区域 -----
    m_listWidget = new QListWidget;
    m_listWidget->setSpacing(4);
    mainLayout->addWidget(m_listWidget);

    // ----- 底部状态栏 -----
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #888; padding: 4px;");
    mainLayout->addWidget(m_statusLabel);

    // 连接信号
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PluginMarketWindow::onSearchChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PluginMarketWindow::onCategoryChanged);
   // connect(m_tabWidget, &QTabWidget::currentChanged, this, &PluginMarketWindow::onTabChanged);
    connect(m_refreshBtn, &QPushButton::clicked, this, &PluginMarketWindow::refreshList);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    m_networkManager = new QNetworkAccessManager(this);

    // 连接 finished 信号到处理槽函数
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &PluginMarketWindow::onReplyFinished);
    fetchPluginsFromGitee();
}



void PluginMarketWindow::refreshList() {
    // 实际可重新从网络拉取
    filterAndDisplay();
}

void PluginMarketWindow::onSearchChanged(const QString &text) {
    m_currentKeyword = text;
    filterAndDisplay();
}

void PluginMarketWindow::onCategoryChanged(int index) {
    m_currentCategory = (index == 0) ? "" : m_categoryCombo->currentText();
    filterAndDisplay();
}

void PluginMarketWindow::onTabChanged(int index) {
    m_currentTabIndex = index;
    filterAndDisplay();
}

void PluginMarketWindow::filterAndDisplay() {
    m_listWidget->clear();
    int count = 0;
    for (const PluginInfo2 &info : m_allPlugins) {
        // 分类过滤
        if (!m_currentCategory.isEmpty() && !info.tags.contains(m_currentCategory))
            continue;
        // 搜索关键词
        if (!m_currentKeyword.isEmpty() && !info.name.contains(m_currentKeyword, Qt::CaseInsensitive))
            continue;
        // Tab过滤
        if (m_currentTabIndex == 1 && !info.isInstalled) continue;
        if (m_currentTabIndex == 2 && !info.hasUpdate) continue;
        // 可用tab显示未安装的
        if (m_currentTabIndex == 0 && info.isInstalled) continue;

        PluginCard *card = new PluginCard(info);
        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        item->setSizeHint(card->sizeHint());
        m_listWidget->setItemWidget(item, card);

        connect(card, &PluginCard::installClicked, this, &PluginMarketWindow::onInstallRequested);
        connect(card, &PluginCard::detailClicked, this, &PluginMarketWindow::onOpenDetail);

        count++;
    }
    //m_statusLabel->setText(QString("共 %1 个插件 (已安装 %2)")
    //                           .arg(count)
    //                           .arg(m_allPlugins.count([](const PluginInfo &p){ return p.isInstalled; })));
}

void PluginMarketWindow::onInstallRequested(const QString &id) {
    // 查找插件
    for (auto &info : m_allPlugins) {
        if (info.id == id) {
            // 模拟安装过程
            QProgressDialog progress("正在安装 " + info.name + " ...", "取消", 0, 100, this);
            progress.setWindowModality(Qt::WindowModal);
            progress.show();
            for (int i = 0; i <= 100; i += 10) {
                progress.setValue(i);
                QApplication::processEvents();
                if (progress.wasCanceled()) break;
                //QThread::msleep(50);
            }
            if (!progress.wasCanceled()) {
                info.isInstalled = true;
                info.hasUpdate = false;
                QMessageBox::information(this, "安装完成", info.name + " 已成功安装！");
                filterAndDisplay(); // 刷新列表
            }
            break;
        }
    }
}

void PluginMarketWindow::onOpenDetail(const QString &url) {
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void PluginMarketWindow::fetchPluginsFromGitee()
{

    QUrl url("https://gitee.com/linglan2/pure-white-bell--plugin-sdk/raw/master/PluginList.json");


    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Qt Plugin Market/1.0");

    // 3. 状态标记和界面反馈
    m_isLoading = true;
    m_statusLabel->setText("正在加载插件列表...");
    m_listWidget->clear(); // 清空旧列表，防止显示残留

    // 4. 发起 GET 请求（m_networkManager 是 QNetworkAccessManager* 成员）
    m_networkManager->get(request);
}
void PluginMarketWindow::onReplyFinished(QNetworkReply *reply)
{
    // 1. 清除加载状态
    m_isLoading = false;

    // 2. 检查网络错误
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::warning(this, "网络错误",
                             QString("获取插件列表失败：%1").arg(reply->errorString()));
        m_statusLabel->setText("加载失败，请检查网络");
        reply->deleteLater();
        return;
    }

    // 3. 读取数据
    QByteArray data = reply->readAll();
    reply->deleteLater(); // 释放 reply

    // 4. 解析 JSON
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, "数据错误", "插件列表格式无效");
        m_statusLabel->setText("数据解析失败");
        return;
    }

    QJsonObject root = doc.object();
    int code = root["code"].toInt();
    if (code != 0) {
        QMessageBox::warning(this, "接口错误", root["message"].toString());
        m_statusLabel->setText("接口返回错误");
        return;
    }

    // 5. 获取插件数组
    QJsonArray list = root["data"].toObject()["list"].toArray();
    m_allPlugins.clear();

    // 6. 遍历数组，填充 PluginInfo
    for (const QJsonValue &val : list) {
        QJsonObject item = val.toObject();
        PluginInfo2 info;

        info.id = item["id"].toString();
        info.name = item["name"].toString();
        info.iconPath = item["icon"].toString();
        info.remark = item["remark"].toString();
        info.author = item["author"].toString();
        info.versionCode = item["versionCode"].toInt();
        info.versionName = item["versionName"].toString();
        info.detailUrl = item["homepage"].toString();
        info.downloadUrl = item["downloadUrl"].toString();

        info.isInstalled = item["isInstalled"].toBool(false);
        info.hasUpdate = item["hasUpdate"].toBool(false);
        info.installedVersionCode = item["installedVersionCode"].toInt(0);
        info.installedVersionName = item["installedVersionName"].toString();

        // 解析 tags 数组
        QJsonArray tags = item["tags"].toArray();
        for (const QJsonValue &tag : tags) {
            info.tags << tag.toString();
        }

        // 额外字段：gitee（可选）
        info.gitee = item["gitee"].toString();

        m_allPlugins.append(info);
    }

    // 7. 刷新列表显示
    filterAndDisplay();

    // 8. 更新状态栏
    int installedCount = 0;
    for (const PluginInfo2 &info : m_allPlugins) {
        if (info.isInstalled) installedCount++;
    }
    m_statusLabel->setText(QString("加载成功，共 %1 个插件，已安装 %2 个")
                               .arg(m_allPlugins.size())
                               .arg(installedCount));
}