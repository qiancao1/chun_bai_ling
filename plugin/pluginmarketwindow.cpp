#include "pluginmarketwindow.h"
#include "global.h"
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
#include <QSettings>

// ======================== PluginCard 实现 ========================
PluginCard::PluginCard(const PluginInfo2 &info, QWidget *parent)
    : QWidget(parent), m_info(info) {
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

    QVBoxLayout *infoLayout = new QVBoxLayout;
    infoLayout->setSpacing(2);

    // 名称行
    QString nameText = info.name;
    if (info.isInstalled) {
        nameText += " [已安装]";
    }
    m_nameLabel = new QLabel(nameText);
    m_nameLabel->setStyleSheet("background: transparent; font-weight: bold; font-size: 14px; color: black;");

    // 备注行
    m_remarkLabel = new QLabel(info.remark);
    m_remarkLabel->setStyleSheet("background: transparent; color: #666; font-size: 12px;");

    // ----- 标签 + 版本信息 同一行（水平布局）-----
    QHBoxLayout *tagVersionLayout = new QHBoxLayout;
    tagVersionLayout->setSpacing(8);

    // 标签（tags）
    m_tagLabel = new QLabel(info.tags.join(" · "));
    m_tagLabel->setStyleSheet("background: transparent; color: #1E90FF; font-size: 11px;");

    // 版本信息
    QString versionText;
    if (info.isInstalled) {
        versionText = QString("已安装: %1").arg(info.installedVersionName.isEmpty() ? "未知" : info.installedVersionName);
        if (!info.versionName.isEmpty()) {
            versionText += QString(" | 最新: %1").arg(info.versionName);
        }
        if (info.hasUpdate) {
            versionText += " ⚡ 有更新";
        }
    } else {
        if (!info.versionName.isEmpty()) {
            versionText = QString("最新: %1").arg(info.versionName);
        }
    }
    m_versionLabel = new QLabel(versionText);
    m_versionLabel->setStyleSheet("background: transparent; color: #888; font-size: 11px;");
    if (info.hasUpdate && info.isInstalled) {
        m_versionLabel->setStyleSheet("background: transparent; color: #FF6B00; font-size: 11px; font-weight: bold;");
    }

    // 将标签和版本信息加入水平布局，版本信息靠右（添加伸缩）

    QString color;
    if (info.type == "Python") color = "#2EE89F";
    else if (info.type == "DLL") color = "#FFA500";
    else if (info.type == "DLL32") color = "#FF6347";
    else if (info.type == "JS") color = "#97CEEB";
    else color = "#CCCCCC"; // 默认灰色

    QLabel *bq_type = new QLabel(" " + info.type + " ");
    bq_type->setStyleSheet(QString("background-color: %1;").arg(color));




    QLabel *author = new QLabel(" " + info.author + " ");
    author->setStyleSheet("background-color: #DFA9F1;");



    tagVersionLayout->addWidget(bq_type);
    tagVersionLayout->addWidget(author);
    tagVersionLayout->addWidget(m_tagLabel);


    tagVersionLayout->addWidget(m_versionLabel);

    tagVersionLayout->addStretch();
    // 将各行加入 infoLayout
    infoLayout->addWidget(m_nameLabel);
    infoLayout->addWidget(m_remarkLabel);
    infoLayout->addLayout(tagVersionLayout);   // 这里替换原来的单独标签行

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
    applyStyleSheet();
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

    m_categoryCombo->addItem("视频");
    m_categoryCombo->addItem("音频");
    m_categoryCombo->addItem("图片");

    m_categoryCombo->addItem("游戏");
    m_categoryCombo->addItem("小游戏");

    m_categoryCombo->setFixedWidth(120);




    m_plugin_type = new QComboBox;
    m_plugin_type->addItem("全部");
    m_plugin_type->addItem("Python");
    m_plugin_type->addItem("DLL");
    m_plugin_type->addItem("DLL32");
    m_plugin_type->addItem("JS");
    m_plugin_type->setFixedWidth(120);



    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setFixedWidth(80);

    QPushButton *closeBtn = new QPushButton("关闭");
    closeBtn->setFixedWidth(80);

    toolLayout->addWidget(m_searchEdit);
    toolLayout->addWidget(m_categoryCombo);
    toolLayout->addWidget(m_plugin_type);
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
    connect(m_plugin_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PluginMarketWindow::onCategoryChanged2);
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
void PluginMarketWindow::onCategoryChanged2(int index) {

    m_plugin_type_str = (index == 0) ? "" : m_plugin_type->currentText();
    filterAndDisplay();
}
void PluginMarketWindow::onTabChanged(int index) {
    m_currentTabIndex = index;
    filterAndDisplay();
}
void PluginMarketWindow::filterAndDisplay() {
    m_listWidget->clear();
    int count = 0;

    // 1. 先计算已安装总数（从 m_pluginList 读取）
    int installedTotal = m_pluginList.size(); // 如果 m_pluginList 就是已安装列表的话

    for (PluginInfo2 &info : m_allPlugins) {

        if (!m_currentCategory.isEmpty() && !info.tags.contains(m_currentCategory))
            continue;
        if(m_plugin_type_str !="全部" && !m_plugin_type_str.isEmpty())
        {
            if(m_plugin_type_str != info.type) continue;
        }
        if (!m_currentKeyword.isEmpty() && !info.name.contains(m_currentKeyword, Qt::CaseInsensitive))
            continue;

        // 从 m_pluginList 中匹配是否已安装
        bool isInstalled = false;
        int installedVer = 0;
        for (const auto &p : std::as_const(m_pluginList)) {
            if (info.id == p.id) {
                isInstalled = true;
                installedVer = p.version_int; // 假设你的已安装列表有这个字段
                info.installedVersionName =p.version;
                break;
            }
        }
        info.isInstalled = isInstalled;
        if (isInstalled) {
            info.hasUpdate = (info.versionCode > installedVer);
        } else {
            info.hasUpdate = false;
        }

        // Tab 过滤
        if (m_currentTabIndex == 1 && !info.isInstalled) continue;
        if (m_currentTabIndex == 2 && !info.hasUpdate) continue;

        // 创建卡片
        PluginCard *card = new PluginCard(info);
        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        item->setSizeHint(card->sizeHint());
        m_listWidget->setItemWidget(item, card);

        connect(card, &PluginCard::installClicked, this, &PluginMarketWindow::onInstallRequested);
        connect(card, &PluginCard::detailClicked, this, &PluginMarketWindow::onOpenDetail);

        count++;
    }

    // 2. 更新状态栏
    m_statusLabel->setText(QString("共 %1 个插件 (已安装 %2)")
                               .arg(count)
                               .arg(installedTotal));
}


void PluginMarketWindow::onInstallRequested(const QString &id) {
    // 查找插件
    PluginInfo2 *targetInfo = nullptr;
    for (auto &info : m_allPlugins) {
        if (info.id == id) {
            targetInfo = &info;
            break;
        }
    }
    if (!targetInfo) {
        QMessageBox::warning(this, "错误", "未找到插件信息");
        return;
    }

    // 检查下载链接
    if (targetInfo->downloadUrl.isEmpty()) {
        QMessageBox::warning(this, "错误", "该插件没有提供下载链接");
        return;
    }

    // 检查 7za.exe 是否存在

    #ifdef _WIN32
        QString appDir = QCoreApplication::applicationDirPath();
        QString sevenZipPath = appDir + "/7za.exe";
        if (!QFile::exists(sevenZipPath)) {
            QMessageBox::warning(this, "错误", "解压工具 7za.exe 未找到，请将 7za.exe 放置在程序目录下");
            return;
        }
    #else
        // Linux 下可以用 QStandardPaths::findExecutable 提前检查
        QString sevenZipPath = QStandardPaths::findExecutable("7zz");
        if (sevenZipPath.isEmpty()) {
            sevenZipPath = QStandardPaths::findExecutable("7z");
        }
        if (sevenZipPath.isEmpty()) {
            QMessageBox::warning(this, "错误", "未找到 7z/7zz，请安装 p7zip-full (sudo apt install p7zip-full)");
            return;
        }
    #endif
    Installed_type=-1;
    if(targetInfo->type=="Python")
    {
        Installed_type=0;
    }
    if(targetInfo->type=="DLL")
    {
        Installed_type=1;
    }
    if(targetInfo->type=="JS")
    {
        Installed_type=3;
    }
    // 准备临时文件路径
    QString tempZipPath = "tmp/market" + targetInfo->id + ".zip";
    QFile *tempFile = new QFile(tempZipPath, this);
    if (tempFile->exists()) {
        tempFile->remove();
    }
    if (!tempFile->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "错误", "无法创建临时文件");
        delete tempFile;
        return;
    }
    tempFile->close(); // 先关闭，等下载完成再打开写入
    startDownload(targetInfo, QUrl(targetInfo->downloadUrl), 0);
}


void PluginMarketWindow::startDownload(PluginInfo2 *info, const QUrl &url, int redirectDepth) {
    // 防止无限重定向
    const int MAX_REDIRECT = 5;
    if (redirectDepth > MAX_REDIRECT) {
        QMessageBox::warning(this, "错误", "下载重定向次数过多，请检查链接");
        return;
    }

    // 创建临时文件路径
    QString tempZipPath = "tmp/market" + info->id + ".zip";
    QFile tempFile(tempZipPath);
    if (tempFile.exists()) {
        tempFile.remove();
    }

    // 创建进度对话框
    QProgressDialog *progress = new QProgressDialog("正在下载 " + info->name + " ...", "取消", 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();

    // 网络管理器
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Qt Plugin Market/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    QPointer<QNetworkReply> reply = manager->get(request);

    // 下载进度
    connect(reply, &QNetworkReply::downloadProgress, this, [progress](qint64 received, qint64 total) {
        if (!progress) return;
        if (total > 0) {
            progress->setValue(static_cast<int>((received * 100) / total));
        } else {
            progress->setValue(0);
        }
        QApplication::processEvents();
    });

    // 下载完成
    connect(reply, &QNetworkReply::finished, this, [=]() mutable {
        if (reply.isNull()) {
            progress->deleteLater();
            manager->deleteLater();
            return;
        }

        // 处理 HTTP 重定向
        QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (!redirect.isNull()) {
            QUrl newUrl = reply->url().resolved(redirect.toUrl());
            reply->deleteLater();
            manager->deleteLater();
            progress->deleteLater();
            // 递归重试
            startDownload(info, newUrl, redirectDepth + 1);
            return;
        }

        // 检查是否有错误
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "下载失败", QString("下载出错：%1").arg(reply->errorString()));
            reply->deleteLater();
            manager->deleteLater();
            progress->deleteLater();
            return;
        }

        // 读取数据
        QByteArray data = reply->readAll();
        reply->deleteLater();
        manager->deleteLater();

        // 检查是否为 HTML 重定向页面（Gitee 常见）
        QString content = QString::fromUtf8(data);
        if (content.contains("redirected") && content.contains("<a href=")) {
            QRegularExpression rx("<a\\s+href\\s*=\\s*\"([^\"]+)\"");
            QRegularExpressionMatch match = rx.match(content);
            if (match.hasMatch()) {
                QString newUrl = match.captured(1);
                progress->deleteLater();
                // 递归重试
                startDownload(info, QUrl(newUrl), redirectDepth + 1);
                return;
            }
        }

        // 如果用户取消了下载
        if (progress->wasCanceled()) {
            progress->deleteLater();
            return;
        }

        // 写入文件
        QFile outFile(tempZipPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "错误", "无法写入临时文件");
            progress->deleteLater();
            return;
        }
        outFile.write(data);
        outFile.close();

        // 下载成功，进入解压阶段
        finishInstall(info, tempZipPath, progress);
    });

    // 取消下载
    connect(progress, &QProgressDialog::canceled, this, [=]() {
        if (!reply.isNull() && reply->isRunning()) {
            reply->abort();
        }
        // 清理会在 finished 中处理
    });
}

void PluginMarketWindow::finishInstall(PluginInfo2 *info, const QString &zipPath, QProgressDialog *progress)
{

    QString sevenZipPath;
#ifdef _WIN32
    // Windows 保持原样
    sevenZipPath = QCoreApplication::applicationDirPath() + "/7za.exe";

#else
    // Linux：按优先级依次探测
    QStringList candidates;
    candidates << "7zz"    // 官方新版 7-Zip (推荐)
               << "7z"     // p7zip-full 或 官方旧版符号链接
               << "7za";   // p7zip 独立版 (部分旧系统)

    for (const QString &cmd : candidates) {
        QString path = QStandardPaths::findExecutable(cmd);
        if (!path.isEmpty()) {
            sevenZipPath = path;
            break;
        }
    }

    // 如果还没找到，再检查常见固定路径（兜底）
    if (sevenZipPath.isEmpty()) {
        QFileInfo possible("/usr/bin/7zz");
        if (possible.exists() && possible.isExecutable())
            sevenZipPath = possible.absoluteFilePath();
        else {
            QFileInfo possible2("/usr/bin/7z");
            if (possible2.exists() && possible2.isExecutable())
                sevenZipPath = possible2.absoluteFilePath();
        }
    }

#endif

    // 最终检查是否真的找到了
    if (sevenZipPath.isEmpty() || !QFileInfo::exists(sevenZipPath)) {
        QString errMsg = QSysInfo::productType() == "windows" ?
                             "解压工具 7za.exe 未找到" :
                             "未找到 7z/7zz/7za 命令，请安装 p7zip-full 或 7zip (sudo apt install p7zip-full)";
        QMessageBox::warning(this, "错误", errMsg);
        progress->deleteLater();
        return;
    }
    // 2. 其余逻辑与原代码完全一致
    progress->setLabelText("正在解压 " + info->name + " ...");
    progress->setValue(0);

    QString targetDir = QCoreApplication::applicationDirPath() + "/plugins/" + info->name + "/";
    QDir().mkpath(targetDir);

    QStringList args;
    args << "x" << zipPath << "-o" + targetDir << "-y" << "-aoa";

    QProcess *process = new QProcess(this);
    process->setProgram(sevenZipPath);
    process->setArguments(args);
    process->start();

    // 进度读取（与原代码相同）
    connect(process, &QProcess::readyReadStandardOutput, this, [progress, this]() {
        if (!progress) return;
        QProcess *p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString log = QString::fromLocal8Bit(p->readAllStandardOutput());
        QRegularExpression rx("(\\d+)%");
        QRegularExpressionMatch match = rx.match(log);
        if (match.hasMatch()) {
            int percent = match.captured(1).toInt();
            progress->setValue(percent);
            QApplication::processEvents();
        }
    });

    // 完成处理（与原代码基本相同，仅需注意 progress 可能为空）
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int exitCode, QProcess::ExitStatus status) {
                process->deleteLater();

                if (progress->wasCanceled()) {
                    QDir targetDirObj(targetDir);
                    targetDirObj.removeRecursively();
                    QFile::remove(zipPath);
                    progress->deleteLater();
                    return;
                }

                if (exitCode != 0 || status != QProcess::NormalExit) {
                    QByteArray err = process->readAllStandardError();
                    QString errMsg = QString::fromLocal8Bit(err);
                    QMessageBox::warning(this, "解压失败", QString("7z 解压出错：%1").arg(errMsg));
                    QDir targetDirObj(targetDir);
                    targetDirObj.removeRecursively();
                    QFile::remove(zipPath);
                    progress->deleteLater();
                    return;
                }

                // 后续安装逻辑（保持不变）
                if (Installed_type == 0) {
                    pluginPage->LoadPlugin_Python_pip("plugins/" + info->name);
                } else if (Installed_type == 3) {
                    pluginPage->npmJSpk("plugins/" + info->name);
                } else {
                    QMessageBox::information(this, "下载完成", info->name + "\n此类插件需要手动安装（dll 未知入口）");
                }
                progress->close();
                progress->deleteLater();
            });

    // 取消响应（不变）
    connect(progress, &QProgressDialog::canceled, this, [=]() {
        if (process->state() == QProcess::Running) {
            process->kill();
        }
    });
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


    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    request.setRawHeader("Referer", "https://gitee.com/");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");

    m_isLoading = true;
    m_statusLabel->setText("正在加载插件列表...");
    m_listWidget->clear();

    m_networkManager->get(request);
}

void PluginMarketWindow::onReplyFinished(QNetworkReply *reply)
{
    m_isLoading = false;

    // 检查是否有重定向（状态码 301/302/307）
    QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!redirect.isNull()) {
        QUrl newUrl = reply->url().resolved(redirect.toUrl());
        qDebug() << "Redirecting to:" << newUrl;

        QNetworkRequest newRequest(newUrl);
        // 同样设置请求头
        newRequest.setHeader(QNetworkRequest::UserAgentHeader,
                             "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        newRequest.setRawHeader("Referer", "https://gitee.com/");
        newRequest.setRawHeader("Accept", "application/json, text/plain, */*");

        // 重新发起请求
        m_networkManager->get(newRequest);
        reply->deleteLater();
        return;
    }

    // 检查网络错误
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::warning(this, "网络错误",
                             QString("获取插件列表失败：%1").arg(reply->errorString()));
        m_statusLabel->setText("加载失败，请检查网络");
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    // 移除 UTF-8 BOM
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove(0, 3);
    }


    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (doc.isNull() || !doc.isObject()) {
        QString errMsg = QString("JSON 解析失败，偏移 %1：%2")
                             .arg(error.offset)
                             .arg(error.errorString());
        QMessageBox::warning(this, "数据错误", errMsg);
        // 打印错误附近的内容
        int start = qMax(0, error.offset - 30);
        int len = qMin(60, data.size() - start);
        qDebug() << "Error context:" << data.mid(start, len);
        m_statusLabel->setText("数据解析失败");
        return;
    }
    // 正常解析
    QJsonObject root = doc.object();
    int code = root["code"].toInt();
    if (code != 0) {
        QMessageBox::warning(this, "接口错误", root["message"].toString());
        m_statusLabel->setText("接口返回错误");
        return;
    }

    const QJsonArray list = root["data"].toObject()["list"].toArray();
    m_allPlugins.clear();

    for (const QJsonValue &val : list) {
        QJsonObject item = val.toObject();
        PluginInfo2 info;
        info.id = item["id"].toString();
        info.name = item["name"].toString();
        info.iconPath = item["icon"].toString();
        info.remark = item["remark"].toString();
        info.detailUrl = item["homepage"].toString();
        info.author = item["author"].toString();
        info.versionCode = item["versionCode"].toInt();
        info.versionName = item["versionName"].toString();
        info.downloadUrl = item["downloadUrl"].toString();
        info.type = item["type"].toString();
        if(info.id.isEmpty()) info.id=info.name;
        if(info.type.isEmpty()) info.type = "未知";
        const QJsonArray tags = item["tags"].toArray();
        for (const QJsonValue &tag : tags) {
            info.tags << tag.toString();
        }
        m_allPlugins.append(info);
    }

    filterAndDisplay();
    m_statusLabel->setText(QString("加载成功，共 %1 个插件").arg(m_allPlugins.size()));
}
void PluginMarketWindow::applyStyleSheet()
{
    setStyleSheet(R"(
        QMainWindow {
            background: transparent;
        }
        QWidget#centralRoot {
            background: #FFF8EF;
            border-radius: 10px;
        }
        QWidget {
            color: #263241;
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            font-size: 13px;
        }
        QWidget#titleBar {
            background: #FFF8EF;
            border: none;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
        }
        QWidget#botStatusWidget {
            background: transparent;
            border: 1px solid #F2E8DE;
            border-radius: 10px;
        }
        QWidget#botStatusWidget:hover {
            background: #FFF7EA;
            border: 1px solid #FFCF9F;
        }
        QLabel#titleAvatar {
            background: #8A94A6;
            border-radius: 10px;
            color: white;
            font-size: 16px;
            font-weight: bold;
        }
        QLabel#titleUserName {
            color: #263241;
            font-weight: 700;
            font-size: 12px;
            background: transparent;
        }
        QLabel#titleOnline {
            color: #65B85A;
            font-size: 11px;
            background: transparent;
        }
        QWidget#contentWidget {
            background: #F7EFE5;
            border-bottom-left-radius: 10px;
            border-bottom-right-radius: 10px;
        }
        QWidget#sideBar {
            background: #FEFEFC;
            border-right: 1px solid #F4E8DA;
            border-top-right-radius: 10px;
            border-bottom-left-radius: 10px;
        }
        QLabel#brandLogoLabel {
            background: transparent;
            border: none;
        }
        QPushButton#navBtn {
            background: transparent;
            border: none;
            border-radius: 6px;
            color: #687589;
            font-size: 14px;
            font-weight: 600;
            padding: 0px 16px;
            text-align: left;
        }
        QPushButton#navBtn:hover {
            background: #FFF6EA;
            color: #FF914D;
        }
        QPushButton#navBtn:checked {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QLabel#mascotImage {
            background: transparent;
            border: none;
        }
        QStackedWidget#contentStack {
            background: #F7EFE5;
            border: none;
            border-bottom-right-radius: 5px;
        }
        QFrame, QGroupBox {
            background: #FFFFFF;
            border: none; /* 移除残余硬边框 */
            border-radius: 5px;
        }
        QGroupBox {
            margin-top: 14px;
            padding-top: 18px;
            font-weight: 700;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 14px;
            padding: 0 8px;
            color: #263241;
        }
        QListWidget, QListView, QScrollArea {
            background: #FFFFFF;
            border: 1px solid #e0e0e0;   /* 宽度1px，实线，黑色 */
            border-radius: 4px;
            outline: none;
        }
        QListWidget::item, QListView::item {
            border: none;
            color: #596579;
        }
        QListWidget::item:selected, QListView::item:selected {
            background: #FFF0DE;
            color: #FF7F32;
            border-radius: 4px;
        }
        QListWidget::item:hover, QListView::item:hover {
            background: #FFF7EA;
            border-radius: 6px;
            /*background: transparent;*/
        }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox {
            background: #FeFeFe;

            border-radius: 4px;
            padding: 6px 10px;
            border: 1px solid #E0E0E0;   /* 宽度1px，实线，黑色 */
            selection-background-color: #FFB066;
            selection-color: #FFFFFF;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus {
            background: #FFFFFF;
            border: 1px solid #FFB066;
        }
        /* 新增 placeholder 颜色 */
        QLineEdit::placeholder, QTextEdit::placeholder, QPlainTextEdit::placeholder {
            color: #AfAfAf;
        }
        QPushButton {
            border: none;
            border-radius: 6px;
            padding: 6px 12px;
            background: #FFF0DE;
            color: #FF7F32;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #FFE5C8;
        }
        QPushButton:pressed {
            background: #FFD7A8;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 4px 2px 4px 2px;
        }
        QScrollBar::handle:vertical {
            background: #E7D9C8;
            border-radius: 4px;
            min-height: 40px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QComboBox {
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 5px 30px 5px 10px; /* 为按钮腾出右侧空间 */
            background: white;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 25px;
            border-left: 1px solid #ccc;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            background: #f0f0f0;
        }
        QComboBox::down-arrow {
            image: url(:/arrow_down.png); /* 或自定义 */
            width: 12px;
            height: 12px;
        }
        /* 悬停效果 */
        QComboBox::drop-down:hover {
            background: #e0e0e0;
        }
#tagLabel {
    background: transparent !important;
    color: #1E90FF;
    font-size: 11px;
    padding: 0px;
    border: none;
}
        QTableWidget { border: 1px solid #AE8AB1; gridline-color: #d0d0d0; }
        QHeaderView::section { background-color: #f5f5f5; border: 1px solid #d0d0d0; }
    )");
}
