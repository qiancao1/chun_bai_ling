#include "materialwidget.h"
#include "global.h"            // g_botClients / g_appid / m_accounts / accinfo
#include "qqbotclient.h"
#include "netmanager.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// 平台配额上限（文档：单机器人默认 100 张，删除接口可释放）
constexpr int kMaterialLimit = 100;
// 图标模式下缩略图的显示边长
constexpr int kIconSide = 132;
// 内存里只留这么大的缩小版（原图在磁盘），100 张也不会吃掉几百 MB
constexpr int kMemThumbSide = 320;
// 下载上限（预览图是 COS 预签名原图，正常不会大，这里只是防呆）
constexpr qint64 kThumbMaxBytes = 10LL * 1024 * 1024;

// 本地缓存根目录（相对程序当前目录）：tmp/素材/<appid>/<image_id>.<ext>
const char kCacheRoot[] = "tmp/素材";

// 占位图（还没下到缩略图时显示），不依赖任何资源文件
QPixmap placeholderThumb(const QString &text)
{
    QPixmap pm(kIconSide, kIconSide);
    pm.fill(QColor(0xF2, 0xEC, 0xF6));
    QPainter p(&pm);
    p.setPen(QColor(0x9B, 0x8E, 0xA8));
    p.drawRect(0, 0, kIconSide - 1, kIconSide - 1);
    p.drawText(pm.rect(), Qt::AlignCenter, text);
    p.end();
    return pm;
}

QString humanTime(qint64 secs)
{
    if (secs <= 0) return QStringLiteral("—");
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("—");
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
}

// 宽松取整数：返回体里可能是数字、数字字符串，甚至是毫秒。
// （踩过：接口给的时间字段名/类型跟文档不完全一致，直接 toDouble() 会静默变 0 → 界面全是「—」）
qint64 jsonInt64Loose(const QJsonValue &v)
{
    qint64 n = 0;
    if (v.isDouble())      n = qint64(v.toDouble());
    else if (v.isString()) {
        bool ok = false;
        n = v.toString().trimmed().toLongLong(&ok);
        if (!ok) return 0;
    } else {
        return 0;
    }
    if (n > 100000000000LL) n /= 1000;   // 13 位 = 毫秒
    return n;
}

qint64 pickInt64(const QJsonObject &o, const QStringList &keys)
{
    for (const QString &k : keys) {
        const qint64 n = jsonInt64Loose(o.value(k));
        if (n > 0) return n;
    }
    return 0;
}

// 上传 / 过期时刻（秒）。平台是滑动过期：最后一次被消息引用起 90 天。
// 文档写 created_at / expires_at，但真机上字段名可能不同、也可能只给 ttl，
// 所以这里多试几个键 + 用 ttl 反推，尽量把时间显示出来。
struct ImgTimes { qint64 created = 0; qint64 expires = 0; };

ImgTimes timesOf(const QJsonObject &img)
{
    ImgTimes t;
    t.created = pickInt64(img, {QStringLiteral("created_at"), QStringLiteral("create_time"),
                                QStringLiteral("created_time"), QStringLiteral("createTime"),
                                QStringLiteral("upload_time"), QStringLiteral("uploadTime"),
                                QStringLiteral("ctime"), QStringLiteral("time")});
    t.expires = pickInt64(img, {QStringLiteral("expires_at"), QStringLiteral("expire_time"),
                                QStringLiteral("expires_time"), QStringLiteral("expireTime"),
                                QStringLiteral("expire_at"), QStringLiteral("etime")});
    const qint64 ttl = pickInt64(img, {QStringLiteral("ttl"), QStringLiteral("expire_in"),
                                       QStringLiteral("expires_in")});
    if (t.expires <= 0 && t.created > 0 && ttl > 0 && ttl < 100000000LL)
        t.expires = t.created + ttl;
    if (t.created <= 0 && t.expires > 0 && ttl > 0 && ttl < 100000000LL)
        t.created = t.expires - ttl;
    return t;
}

// 剩余天数；expires 未知时返回 -1（界面显示「—」）
qint64 leftDaysOf(const QJsonObject &img)
{
    const qint64 expires = timesOf(img).expires;
    if (expires <= 0) return -1;
    return (expires - QDateTime::currentSecsSinceEpoch()) / 86400;
}

// 缩略图取原图缩小版：别把 10MB 的原图塞进 iconMode 的 icon 里
QPixmap shrink(const QPixmap &src)
{
    if (src.isNull()) return src;
    if (src.width() <= kMemThumbSide && src.height() <= kMemThumbSide) return src;
    return src.scaled(kMemThumbSide, kMemThumbSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

MaterialWidget::MaterialWidget(QWidget *parent) : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);

    // -------- 顶部：标题 + 上传/刷新 + 视图切换 + 张数 --------
    auto *topLayout = new QHBoxLayout;
    m_title = new QLabel(QStringLiteral("素材管理"));
    m_title->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold;"));

    auto *scopeTip = new QLabel(QStringLiteral("每机器人独立 · 上限 100 张 · 引用有效期 90 天"));
    scopeTip->setStyleSheet(QStringLiteral("color:#8A7A96;"));

    m_btnUpload = new QPushButton(QStringLiteral("上传图片"));
    m_btnUpload->setToolTip(QStringLiteral("可一次选多张，批量上传"));
    m_btnReload = new QPushButton(QStringLiteral("刷新"));
    m_btnMore = new QPushButton(QStringLiteral("加载更多"));
    m_btnMore->setVisible(false);

    m_viewMode = new QComboBox;
    m_viewMode->addItem(QStringLiteral("图标"));
    m_viewMode->addItem(QStringLiteral("列表"));

    m_count = new QLabel(QStringLiteral("—"));

    connect(m_btnUpload, &QPushButton::clicked, this, &MaterialWidget::onUpload);
    connect(m_btnReload, &QPushButton::clicked, this, &MaterialWidget::reload);
    connect(m_btnMore, &QPushButton::clicked, this, [this]() { loadPage(m_nextCursor); });
    connect(m_viewMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaterialWidget::onViewModeChanged);

    topLayout->addWidget(m_title);
    topLayout->addWidget(scopeTip);
    topLayout->addSpacing(10);
    topLayout->addWidget(m_btnUpload);
    topLayout->addWidget(m_btnReload);
    topLayout->addWidget(m_btnMore);
    topLayout->addStretch();
    topLayout->addWidget(m_viewMode);
    topLayout->addWidget(m_count);
    mainLayout->addLayout(topLayout);

    // -------- 中间：图标网格 / 详情列表 两种视图，共用 m_images --------
    m_iconView = new QListWidget;
    m_iconView->setViewMode(QListView::IconMode);
    m_iconView->setResizeMode(QListView::Adjust);
    m_iconView->setMovement(QListView::Static);
    m_iconView->setIconSize(QSize(kIconSide, kIconSide));
    m_iconView->setGridSize(QSize(kIconSide + 28, kIconSide + 52));
    m_iconView->setSpacing(6);
    m_iconView->setWordWrap(true);
    m_iconView->setTextElideMode(Qt::ElideMiddle);
    m_iconView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_iconView, &QListWidget::itemSelectionChanged, this, &MaterialWidget::onSelectionChanged);
    connect(m_iconView, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { onPreview(); });

    m_treeView = new QTreeWidget;
    m_treeView->setRootIsDecorated(false);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setIconSize(QSize(48, 48));
    // 列：image_id 对用户没意义（内部还留着当 key），换成真正有用的引用链接和本地缓存
    m_treeView->setColumnCount(6);
    m_treeView->setHeaderLabels({QStringLiteral("文件名"), QStringLiteral("引用链接"),
                                 QStringLiteral("上传时间"), QStringLiteral("过期时间"),
                                 QStringLiteral("剩余"), QStringLiteral("本地缓存")});
    QHeaderView *th = m_treeView->header();
    th->setSectionResizeMode(QHeaderView::ResizeToContents);
    th->setSectionResizeMode(0, QHeaderView::Interactive);
    th->setSectionResizeMode(1, QHeaderView::Stretch);   // 链接是长串，吃剩余宽度
    th->setStretchLastSection(false);
    m_treeView->setColumnWidth(0, 220);
    connect(m_treeView, &QTreeWidget::itemSelectionChanged, this, &MaterialWidget::onSelectionChanged);
    connect(m_treeView, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { onPreview(); });
    // 右键：复制链接 / 复制 Markdown / 预览 / 复制原始 JSON（时间字段对不上时方便排查）
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *hit = m_treeView->itemAt(pos);
        if (!hit) return;
        if (!hit->isSelected()) { m_treeView->clearSelection(); hit->setSelected(true); }
        showItemMenu(m_treeView->viewport()->mapToGlobal(pos));
    });
    m_iconView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_iconView, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *hit = m_iconView->itemAt(pos);
        if (!hit) return;
        if (!hit->isSelected()) { m_iconView->clearSelection(); hit->setSelected(true); }
        showItemMenu(m_iconView->viewport()->mapToGlobal(pos));
    });

    m_views = new QStackedWidget;
    m_views->addWidget(m_iconView);
    m_views->addWidget(m_treeView);
    mainLayout->addWidget(m_views, 1);

    // -------- 底部：操作按钮 + 固定提示 + 状态 --------
    // 没有「预览」按钮：两个视图都支持双击看大图（列表里右键也有「预览」），按钮纯属占地方
    auto *bottomLayout = new QHBoxLayout;
    m_btnCopyUrl = new QPushButton(QStringLiteral("复制引用链接"));
    m_btnCopyMd = new QPushButton(QStringLiteral("复制 Markdown"));
    m_btnDelete = new QPushButton(QStringLiteral("删除选中"));
    m_btnDelete->setMinimumWidth(120);   // 多选时文字会变成「删除选中 (3)」，留足宽度免得底部按钮跳动

    connect(m_btnCopyUrl, &QPushButton::clicked, this, &MaterialWidget::onCopyUrl);
    connect(m_btnCopyMd, &QPushButton::clicked, this, &MaterialWidget::onCopyMarkdown);
    connect(m_btnDelete, &QPushButton::clicked, this, &MaterialWidget::onDelete);

    auto *multiTip = new QLabel(QStringLiteral("双击看大图 · Ctrl / Shift + 单击可多选 → 批量删除"));
    multiTip->setStyleSheet(QStringLiteral("color:#B08A3E;"));

    m_status = new QLabel(QStringLiteral("未选择机器人"));
    m_status->setWordWrap(false);

    bottomLayout->addWidget(m_btnCopyUrl);
    bottomLayout->addWidget(m_btnCopyMd);
    bottomLayout->addWidget(m_btnDelete);
    bottomLayout->addSpacing(12);
    bottomLayout->addWidget(multiTip);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_status);
    mainLayout->addLayout(bottomLayout);

    onSelectionChanged();
}

// ---------- 切号 ----------

void MaterialWidget::setAppId(uint32_t appid)
{
    m_appid = appid;

    // 标题写清是哪个机器人 —— 素材按 appid 隔离，切号时必须一眼看出来再看下面的列表
    QString botName;
    const int idx = accinfo(int(appid));
    if (appid != 0 && idx >= 0 && idx < m_accounts.size())
        botName = m_accounts[idx]->Ai_nickname;
    if (appid == 0)
        m_title->setText(QStringLiteral(""));
    else if (botName.isEmpty())
        m_title->setText(QStringLiteral("%1").arg(appid));
    else
        m_title->setText(QStringLiteral("%1（%2）").arg(botName).arg(appid));

    ++m_seq;                    // 作废所有在途回调（列表 / 缩略图 / 上传 / 删除）
    m_images.clear();
    m_thumbs.clear();
    m_pendingThumbs.clear();
    m_selection.clear();
    m_nextCursor.clear();
    m_loading = false;
    m_iconView->clear();
    m_treeView->clear();
    m_btnUpload->setEnabled(true);
    m_btnMore->setVisible(false);
    m_count->setText(QStringLiteral("—"));
    onSelectionChanged();

    if (appid == 0) {
        setStatus(QStringLiteral("未选择机器人"), true);
        return;
    }
    reload();
}

void MaterialWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 显示时跟当前账号对齐：① 切过账号但没收到信号；② 列表是空的（首次进来/上次失败）
    const uint32_t cur = uint32_t(g_appid > 0 ? g_appid : 0);
    if (m_appid != cur) {
        setAppId(cur);
    } else if (m_appid != 0 && m_images.isEmpty() && !m_loading) {
        reload();
    }
}

// ---------- 拉列表 ----------

void MaterialWidget::reload()
{
    m_images.clear();
    m_thumbs.clear();
    m_pendingThumbs.clear();
    m_selection.clear();
    m_nextCursor.clear();
    m_loading = false;
    m_iconView->clear();
    m_treeView->clear();
    m_btnMore->setVisible(false);
    m_count->setText(QStringLiteral("—"));
    onSelectionChanged();

    if (m_appid == 0) {
        setStatus(QStringLiteral("未选择机器人"), true);
        return;
    }
    QQBotClient *bot = m_botClients.value(int(m_appid), nullptr);
    if (!bot) {
        setStatus(QStringLiteral("这个账号还没启动，素材需要机器人上线后才能管理"), true);
        return;
    }
    // 不硬性要求在线：token 没过期时离线也能查；真没 token 平台会直接回鉴权错误，显示出来更有用
    if (!bot->isOnline())
        setStatus(QStringLiteral("机器人当前离线，可能取不到素材…"));
    loadPage(QString());
}

void MaterialWidget::loadPage(const QString &cursor)
{
    QQBotClient *bot = m_botClients.value(int(m_appid), nullptr);
    if (!bot || m_loading) return;

    m_loading = true;
    m_btnMore->setEnabled(false);
    if (cursor.isEmpty()) setStatus(QStringLiteral("正在加载素材…"));

    const int seq = m_seq;
    bot->listMediaImages(cursor, [this, seq, cursor](const QString &resp, QNetworkReply::NetworkError err) {
        // 回调在线程池 → 投回主线程

        QMetaObject::invokeMethod(this, [this, seq, cursor, resp, err]() {
            if (seq != m_seq) return;          // 期间切了账号：这页直接丢
            m_loading = false;
            m_btnMore->setEnabled(true);

            if (err != QNetworkReply::NoError) {
                setStatus(QStringLiteral("加载失败：") + resp, true);
                return;
            }
            const QJsonObject root = QJsonDocument::fromJson(resp.toUtf8()).object();
            const QJsonObject body = root.contains(QStringLiteral("images"))
                                         ? root
                                         : root.value(QStringLiteral("data")).toObject();
            const QJsonArray images = body.value(QStringLiteral("images")).toArray();

            if (cursor.isEmpty()) m_images.clear();     // 首页 = 重来一遍
            for (const QJsonValue &v : images) {
                const QJsonObject o = v.toObject();
                if (!o.value(QStringLiteral("image_id")).toString().isEmpty()) m_images.append(o);
            }

            m_nextCursor = body.value(QStringLiteral("next_cursor")).toString();
            m_btnMore->setVisible(!m_nextCursor.isEmpty());

            const int total = body.value(QStringLiteral("total")).toInt(m_images.size());
            m_count->setText(QStringLiteral("共 %1 / %2 张").arg(total).arg(kMaterialLimit));

            rebuildViews();

            if (m_images.isEmpty()) {
                setStatus(QStringLiteral("还没有素材：点「上传图片」可一次选多张 png/jpg/gif/webp（单张 ≤ 10MB）"));
            } else {
                // 时间字段是「平台给多少显示多少」：一个都没有时提示一下，
                // 右键「复制原始 JSON」能把真实返回贴出来，方便按真实键名再对一次
                bool anyTime = false;
                for (const QJsonObject &img : m_images) {
                    if (timesOf(img).expires > 0) { anyTime = true; break; }
                }
                if (anyTime)
                    setStatus(QStringLiteral("已缓存到 %1").arg(cacheDirFor(m_appid)));
                else
                    setStatus(QStringLiteral("接口这次没返回时间字段（右键可复制原始 JSON）"));
            }
            onSelectionChanged();
        });
    });
}

// ---------- 视图重建 / 缩略图 ----------

void MaterialWidget::rebuildViews()
{
    m_updatingViews = true;
    m_iconView->clear();
    m_treeView->clear();
    for (const QJsonObject &img : m_images) {
        addIconItem(img);
        addTreeItem(img);
    }
    applySelectionToViews();
    m_updatingViews = false;
    onSelectionChanged();
}

void MaterialWidget::addIconItem(const QJsonObject &img)
{
    const QString imageId = img.value(QStringLiteral("image_id")).toString();
    if (imageId.isEmpty()) return;

    auto *item = new QListWidgetItem(m_iconView);
    item->setData(Qt::UserRole, imageId);
    item->setData(Qt::UserRole + 1, img);

    const QString name = img.value(QStringLiteral("file_name")).toString();
    const ImgTimes tm = timesOf(img);
    const qint64 leftDays = leftDaysOf(img);
    item->setText(leftDays >= 0 ? QStringLiteral("%1\n剩余 %2 天").arg(name).arg(leftDays) : name);
    item->setToolTip(QStringLiteral("%1\n%2\n上传：%3\n过期：%4")
                         .arg(name, img.value(QStringLiteral("url")).toString(),
                              humanTime(tm.created), humanTime(tm.expires)));

    const QPixmap cached = m_thumbs.value(imageId);
    if (!cached.isNull()) item->setIcon(QIcon(cached));
    else                  item->setIcon(QIcon(placeholderThumb(QStringLiteral("加载中…"))));

    ensureThumb(img);
}

void MaterialWidget::addTreeItem(const QJsonObject &img)
{
    const QString imageId = img.value(QStringLiteral("image_id")).toString();
    if (imageId.isEmpty()) return;

    auto *item = new QTreeWidgetItem(m_treeView);
    item->setData(0, Qt::UserRole, imageId);          // image_id 只当内部 key / 删除用，不再占一列
    item->setData(0, Qt::UserRole + 1, img);

    const QString name = img.value(QStringLiteral("file_name")).toString();
    const ImgTimes tm = timesOf(img);
    const qint64 leftDays = leftDaysOf(img);
    item->setText(0, name);
    item->setText(1, img.value(QStringLiteral("url")).toString());
    item->setText(2, humanTime(tm.created));
    item->setText(3, humanTime(tm.expires));
    item->setText(4, leftDays >= 0 ? QStringLiteral("%1 天").arg(leftDays) : QStringLiteral("—"));
    item->setText(5, cacheTextOf(imageId, name));
    item->setToolTip(1, img.value(QStringLiteral("url")).toString());

    const QPixmap cached = m_thumbs.value(imageId);
    if (!cached.isNull()) item->setIcon(0, QIcon(cached));

    ensureThumb(img);
}

// 缩略图三级取：内存 → 磁盘缓存（tmp/素材/<appid>/<id>.<ext>）→ 下载并落盘
void MaterialWidget::ensureThumb(const QJsonObject &img)
{
    const QString imageId = img.value(QStringLiteral("image_id")).toString();
    if (imageId.isEmpty() || m_thumbs.contains(imageId) || m_pendingThumbs.contains(imageId)) return;

    const QString path = cacheFilePath(imageId, img.value(QStringLiteral("file_name")).toString());
    if (QFileInfo::exists(path)) {
        QPixmap pm;
        if (pm.load(path) && !pm.isNull()) {
            m_thumbs.insert(imageId, shrink(pm));
            applyThumb(imageId, m_thumbs.value(imageId));
            return;
        }
        QFile::remove(path);      // 坏文件：删了重下
    }

    const QString previewUrl = img.value(QStringLiteral("preview_url")).toString();
    if (previewUrl.isEmpty()) return;   // 预签名链接缺失就只能先空着

    m_pendingThumbs.insert(imageId);
    const int seq = m_seq;
    const QString dir = cacheDirFor(m_appid);
    NetManager::instance()->downloadAsync(
        previewUrl, kThumbMaxBytes, 20000,
        [this, seq, imageId, path, dir](const QByteArray &data, bool tooBig, QNetworkReply::NetworkError err) {
            QPixmap pm;
            if (!tooBig && err == QNetworkReply::NoError && !data.isEmpty()) pm.loadFromData(data);
            // 落盘：原图留着，下次进来不再下载（池线程写文件，不碰 UI）
            if (!pm.isNull()) {
                QDir().mkpath(dir);
                QFile f(path);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(data);
                    f.close();
                }
            }
            QMetaObject::invokeMethod(this, [this, seq, imageId, pm]() {
                m_pendingThumbs.remove(imageId);
                if (seq != m_seq || pm.isNull()) return;   // 切号了，缓存作废
                m_thumbs.insert(imageId, shrink(pm));
                applyThumb(imageId, m_thumbs.value(imageId));
            });
        });
}

void MaterialWidget::applyThumb(const QString &imageId, const QPixmap &pm)
{
    for (int i = 0; i < m_iconView->count(); ++i) {
        QListWidgetItem *it = m_iconView->item(i);
        if (it->data(Qt::UserRole).toString() == imageId) {
            it->setIcon(QIcon(pm));
            break;
        }
    }
    for (int i = 0; i < m_treeView->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_treeView->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == imageId) {
            it->setIcon(0, QIcon(pm));
            // 刚写入磁盘，把「本地缓存」那列的大小同步刷新
            const QJsonObject o = it->data(0, Qt::UserRole + 1).toJsonObject();
            it->setText(5, cacheTextOf(imageId, o.value(QStringLiteral("file_name")).toString()));
            break;
        }
    }
}

QString MaterialWidget::cacheDirFor(uint32_t appid) const
{
    return QStringLiteral("%1/%2").arg(QString::fromUtf8(kCacheRoot)).arg(appid);
}

QString MaterialWidget::cacheFilePath(const QString &imageId, const QString &fileName) const
{
    QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext.isEmpty()) ext = QStringLiteral("jpg");
    return cacheDirFor(m_appid) + QLatin1Char('/') + imageId + QLatin1Char('.') + ext;
}

QString MaterialWidget::cacheTextOf(const QString &imageId, const QString &fileName) const
{
    const QFileInfo fi(cacheFilePath(imageId, fileName));
    if (!fi.exists() || fi.size() <= 0) return QStringLiteral("未缓存");
    return humanSize(fi.size());
}

// ---------- 选择 ----------

void MaterialWidget::syncSelectionFromActiveView()
{
    m_selection.clear();
    if (m_views->currentIndex() == 0) {
        const QList<QListWidgetItem *> items = m_iconView->selectedItems();
        for (QListWidgetItem *it : items) m_selection.insert(it->data(Qt::UserRole).toString());
    } else {
        const QList<QTreeWidgetItem *> items = m_treeView->selectedItems();
        for (QTreeWidgetItem *it : items) m_selection.insert(it->data(0, Qt::UserRole).toString());
    }
}

void MaterialWidget::applySelectionToViews()
{
    const bool wasUpdating = m_updatingViews;
    m_updatingViews = true;          // 抑制 itemSelectionChanged 把 m_selection 反冲掉
    m_iconView->clearSelection();
    for (int i = 0; i < m_iconView->count(); ++i) {
        QListWidgetItem *it = m_iconView->item(i);
        if (m_selection.contains(it->data(Qt::UserRole).toString())) it->setSelected(true);
    }
    m_treeView->clearSelection();
    for (int i = 0; i < m_treeView->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_treeView->topLevelItem(i);
        if (m_selection.contains(it->data(0, Qt::UserRole).toString())) it->setSelected(true);
    }
    m_updatingViews = wasUpdating;
}

void MaterialWidget::onSelectionChanged()
{
    if (m_updatingViews) return;     // 程序化选中，不覆盖 m_selection
    syncSelectionFromActiveView();
    const bool has = !m_selection.isEmpty();
    m_btnCopyUrl->setEnabled(has);
    m_btnCopyMd->setEnabled(has);
    m_btnDelete->setEnabled(has);
    if (has && m_selection.size() > 1)
        m_btnDelete->setText(QStringLiteral("删除选中 (%1)").arg(m_selection.size()));
    else
        m_btnDelete->setText(QStringLiteral("删除选中"));
}

void MaterialWidget::onViewModeChanged(int index)
{
    syncSelectionFromActiveView();       // 切换前先记住当前视图选了啥
    m_views->setCurrentIndex(index == 0 ? 0 : 1);
    applySelectionToViews();
}

QStringList MaterialWidget::selectedIds() const
{
    QStringList ids;
    if (m_views->currentIndex() == 0) {
        const QList<QListWidgetItem *> items = m_iconView->selectedItems();
        for (QListWidgetItem *it : items) ids << it->data(Qt::UserRole).toString();
    } else {
        const QList<QTreeWidgetItem *> items = m_treeView->selectedItems();
        for (QTreeWidgetItem *it : items) ids << it->data(0, Qt::UserRole).toString();
    }
    ids.removeAll(QString());
    return ids;
}

QJsonObject MaterialWidget::currentImage() const
{
    if (m_views->currentIndex() == 0) {
        const QList<QListWidgetItem *> items = m_iconView->selectedItems();
        if (!items.isEmpty()) return items.first()->data(Qt::UserRole + 1).toJsonObject();
    } else {
        const QList<QTreeWidgetItem *> items = m_treeView->selectedItems();
        if (!items.isEmpty()) return items.first()->data(0, Qt::UserRole + 1).toJsonObject();
    }
    return QJsonObject();
}

void MaterialWidget::showItemMenu(const QPoint &globalPos)
{
    if (currentImage().isEmpty()) return;
    QMenu menu(this);
    QAction *aUrl = menu.addAction(QStringLiteral("复制引用链接"));
    QAction *aMd = menu.addAction(QStringLiteral("复制 Markdown"));
    QAction *aView = menu.addAction(QStringLiteral("预览"));
    menu.addSeparator();
    QAction *aJson = menu.addAction(QStringLiteral("复制原始 JSON"));
    QAction *act = menu.exec(globalPos);
    if (act == aUrl)       onCopyUrl();
    else if (act == aMd)   onCopyMarkdown();
    else if (act == aView) onPreview();
    else if (act == aJson)
        QApplication::clipboard()->setText(
            QString::fromUtf8(QJsonDocument(currentImage()).toJson(QJsonDocument::Indented)));
}

// ---------- 批量上传 ----------

void MaterialWidget::onUpload()
{
    if (m_appid == 0) {
        setStatus(QStringLiteral("先选一个机器人"), true);
        return;
    }
    QQBotClient *bot = m_botClients.value(int(m_appid), nullptr);
    if (!bot) {
        setStatus(QStringLiteral("这个账号还没启动"), true);
        return;
    }

    // 多选：一次挑一批，逐个上传
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择要上传的素材图片（可多选）"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.gif *.webp)"));
    if (paths.isEmpty()) return;

    m_btnUpload->setEnabled(false);
    auto q = std::make_shared<UploadQueue>();
    q->total = paths.size();
    uploadNext(paths, q);
}

void MaterialWidget::uploadNext(const QStringList &paths, std::shared_ptr<UploadQueue> q)
{
    if (q->index >= paths.size()) {
        // 收尾：全成功只报一句；有失败就把原因一次性列出来
        m_btnUpload->setEnabled(true);
        if (q->errors.isEmpty()) {
            setStatus(QStringLiteral("已上传 %1 张").arg(paths.size()));
        } else {
            setStatus(QStringLiteral("%1 张成功，%2 张失败").arg(paths.size() - q->errors.size()).arg(q->errors.size()), true);
            QMessageBox::warning(this, QStringLiteral("部分图片上传失败"), q->errors.join(QStringLiteral("\n")));
        }
        reload();      // 新图按上传时间倒序排在最前，直接重拉一页最省事
        return;
    }

    QQBotClient *bot = m_botClients.value(int(m_appid), nullptr);
    if (!bot) {
        m_btnUpload->setEnabled(true);
        setStatus(QStringLiteral("这个账号还没启动"), true);
        return;
    }

    const QString path = paths.at(q->index);
    const QString name = QFileInfo(path).fileName();
    setStatus(QStringLiteral("正在上传 %1/%2：%3").arg(q->index + 1).arg(paths.size()).arg(name));

    const int seq = m_seq;
    bot->uploadMediaImage(path, [this, paths, q, seq, name](bool ok, const QJsonObject &, const QString &error) {
        QMetaObject::invokeMethod(this, [this, paths, q, seq, ok, error, name]() {
            if (seq != m_seq) return;        // 期间切了账号：这条链就此打住（按钮已在 setAppId 里恢复）
            if (!ok) q->errors << QStringLiteral("%1：%2").arg(name, error);
            ++q->index;
            uploadNext(paths, q);
        });
    });
}

// ---------- 批量删除 ----------

void MaterialWidget::onDelete()
{
    QQBotClient *bot = m_botClients.value(int(m_appid), nullptr);
    if (!bot) {
        setStatus(QStringLiteral("这个账号还没启动"), true);
        return;
    }
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) {
        setStatus(QStringLiteral("先选中要删除的素材（Ctrl / Shift + 单击可多选）"), true);
        return;
    }

    // 名字与缓存路径都先备好：删完要顺手清掉 tmp 里的图
    QStringList names;
    QHash<QString, QString> idToCache;
    for (const QJsonObject &img : m_images) {
        const QString id = img.value(QStringLiteral("image_id")).toString();
        if (!ids.contains(id)) continue;
        const QString fileName = img.value(QStringLiteral("file_name")).toString();
        names << fileName;
        idToCache.insert(id, cacheFilePath(id, fileName));
    }

    const auto ret = QMessageBox::question(
        this, QStringLiteral("删除素材"),
        QStringLiteral("确定删除选中的 %1 张素材？\n\n%2\n\n删除后引用链接立即失效，已发出的消息不受影响。")
            .arg(ids.size()).arg(names.join(QStringLiteral("、"))));
    if (ret != QMessageBox::Yes) return;

    setStatus(QStringLiteral("正在删除 %1 张素材…").arg(ids.size()));

    // 并发删，全部回齐再刷新；失败原因攒起来一起报
    auto left = std::make_shared<int>(ids.size());
    auto errors = std::make_shared<QStringList>();
    const int seq = m_seq;
    for (const QString &id : ids) {
        const QString cachePath = idToCache.value(id);
        bot->deleteMediaImage(id, [this, seq, left, errors, cachePath](const QString &resp, QNetworkReply::NetworkError err) {
            QMetaObject::invokeMethod(this, [this, seq, left, errors, cachePath, resp, err]() {
                const QJsonObject o = QJsonDocument::fromJson(resp.toUtf8()).object();
                const int code = o.value(QStringLiteral("code")).toInt();
                const bool failed = (err != QNetworkReply::NoError || code != 0);
                if (failed) {
                    QString msg = o.value(QStringLiteral("message")).toString();
                    if (msg.isEmpty()) msg = resp.trimmed();
                    if (msg.isEmpty()) msg = QStringLiteral("网络错误");
                    errors->append(msg);
                } else {
                    // 平台删掉了，本地缓存也一起清（避免 tmp 里越攒越多）
                    if (!cachePath.isEmpty() && QFileInfo::exists(cachePath)) QFile::remove(cachePath);
                }
                if (--(*left) > 0) return;
                if (seq != m_seq) return;      // 期间切号：别再动界面
                m_selection.clear();
                reload();
                if (!errors->isEmpty())
                    setStatus(QStringLiteral("部分删除失败：") + errors->join(QStringLiteral("；")), true);
            });
        });
    }
}

// ---------- 选中项操作 ----------

void MaterialWidget::onCopyUrl()
{
    const QString url = currentImage().value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        setStatus(QStringLiteral("先选中一张素材"), true);
        return;
    }
    QApplication::clipboard()->setText(url);
    setStatus(QStringLiteral("引用链接已复制（写进 Markdown 即可）"));
}

void MaterialWidget::onCopyMarkdown()
{
    const QJsonObject img = currentImage();
    const QString url = img.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        setStatus(QStringLiteral("先选中一张素材"), true);
        return;
    }
    // 与公网图片语法一致，宽高可选（#800px #400px）
    QString name = img.value(QStringLiteral("file_name")).toString();
    name = QFileInfo(name).completeBaseName();
    QApplication::clipboard()->setText(QStringLiteral("![%1](%2)").arg(name, url));
    setStatus(QStringLiteral("Markdown 已复制"));
}

void MaterialWidget::onPreview()
{
    const QJsonObject img = currentImage();
    if (img.isEmpty()) {
        setStatus(QStringLiteral("先选中一张素材"), true);
        return;
    }

    const QString imageId = img.value(QStringLiteral("image_id")).toString();
    const QString fileName = img.value(QStringLiteral("file_name")).toString();
    const QString localPath = cacheFilePath(imageId, fileName);
    const QString previewUrl = img.value(QStringLiteral("preview_url")).toString();
    const ImgTimes tm = timesOf(img);
    const qint64 leftDays = leftDaysOf(img);

    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(fileName);
    auto *lay = new QVBoxLayout(dlg);
    auto *view = new QLabel(dlg);
    view->setAlignment(Qt::AlignCenter);
    view->setMinimumSize(320, 240);
    lay->addWidget(view, 1);
    auto *info = new QLabel(QStringLiteral("%1\n上传：%2\n过期：%3（剩余 %4）\n本地：%5")
                                .arg(img.value(QStringLiteral("url")).toString(),
                                     humanTime(tm.created), humanTime(tm.expires),
                                     leftDays >= 0 ? QStringLiteral("%1 天").arg(leftDays) : QStringLiteral("未知"),
                                     QFileInfo::exists(localPath) ? localPath : QStringLiteral("（未缓存）")),
                            dlg);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->setWordWrap(true);
    lay->addWidget(info);

    // 预览永远优先用磁盘上的原图（缓存过就不吃网络）
    QPixmap local;
    if (QFileInfo::exists(localPath)) local.load(localPath);
    if (!local.isNull()) {
        view->setPixmap(local.scaled(720, 560, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        const QPixmap mem = m_thumbs.value(imageId);
        view->setPixmap(mem.isNull() ? placeholderThumb(QStringLiteral("暂无预览")) : mem);
    }

    dlg->resize(760, 640);
    dlg->show();

    if (local.isNull() && !previewUrl.isEmpty()) {
        const QString dir = cacheDirFor(m_appid);
        NetManager::instance()->downloadAsync(
            previewUrl, kThumbMaxBytes, 20000,
            [view, localPath, dir](const QByteArray &data, bool tooBig, QNetworkReply::NetworkError err) {
                if (tooBig || err != QNetworkReply::NoError || data.isEmpty()) return;
                QPixmap pm;
                if (!pm.loadFromData(data)) return;
                QDir().mkpath(dir);
                QFile f(localPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(data);
                    f.close();
                }
                // view 是上下文对象：对话框关掉后 invokeMethod 自动丢弃，不会碰野指针
                QMetaObject::invokeMethod(view, [view, pm]() {
                    view->setPixmap(pm.scaled(720, 560, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                });
            });
    }
}

void MaterialWidget::setStatus(const QString &text, bool error)
{
    m_status->setStyleSheet(error ? QStringLiteral("color:#C0392B;") : QStringLiteral("color:#7A6A88;"));
    m_status->setText(text);
}
