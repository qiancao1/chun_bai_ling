#pragma once
#include <QWidget>
#include <QHash>
#include <QList>
#include <QSet>
#include <QJsonObject>
#include <QPixmap>
#include <QStringList>

#include <memory>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

// 「内置功能 → 素材」页：管理**当前机器人**的 Markdown 图片素材（平台 /v2/media-images）。
//
// 为什么按机器人分：这套素材在平台侧就是按 appid 隔离的（各自 100 张配额、
// 各自 qqbot-media:// 链接、各自 90 天滑动有效期），所以界面只认 setAppId()：
//   切账号 = 立刻清空列表/缩略图缓存 + 重新拉取；m_seq 递增作废所有在途回调，防串号。
//
// 上传走 QQBotClient::uploadMediaImage（prepare → PUT → complete 全异步），支持多选批量；
// 列表/删除走 listMediaImages / deleteMediaImage。所有网络回调都在线程池线程，
// 一律 QMetaObject::invokeMethod 投回主线程再碰控件。
//
// 图片本地缓存：tmp/素材/<appid>/<image_id>.<ext>（原图），下次进页面直接读盘，
// 不再每次下载 preview_url；预览也用这份原图。内存里只留缩小版（kMemThumbSide），
// 避免 100 张 10MB 图把界面拖死。
class MaterialWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MaterialWidget(QWidget *parent = nullptr);

    // 切换机器人（主窗口切左侧账号列表时调用）
    void setAppId(uint32_t appid);

protected:
    void showEvent(QShowEvent *event) override;   // 每次显示时跟 g_appid 对齐

private slots:
    void reload();
    void onUpload();
    void onDelete();
    void onCopyUrl();
    void onCopyMarkdown();
    void onPreview();
    void onSelectionChanged();
    void onViewModeChanged(int index);

private:
    // 批量上传队列（逐个传：并发 10 张 10MB 只会互相抢带宽，进度也看不清）
    struct UploadQueue {
        int total = 0;
        int index = 0;
        QStringList errors;
    };

    void loadPage(const QString &cursor);                  // 拉一页（首页 cursor 为空）
    void rebuildViews();                                   // m_images → 两个视图，并按 m_selection 还原选中
    void addIconItem(const QJsonObject &img);
    void addTreeItem(const QJsonObject &img);
    void ensureThumb(const QJsonObject &img);              // 内存 → 磁盘 → 下载
    void applyThumb(const QString &imageId, const QPixmap &pm);
    void syncSelectionFromActiveView();                    // 当前可见视图的选中 → m_selection
    void applySelectionToViews();

    QString cacheDirFor(uint32_t appid) const;             // tmp/素材/<appid>
    QString cacheFilePath(const QString &imageId, const QString &fileName) const;
    QString cacheTextOf(const QString &imageId, const QString &fileName) const;   // 「1.2 MB」/「未缓存」

    QStringList selectedIds() const;
    QJsonObject currentImage() const;
    void showItemMenu(const QPoint &globalPos);            // 右键菜单（两个视图共用）
    void uploadNext(const QStringList &paths, std::shared_ptr<UploadQueue> q);
    void setStatus(const QString &text, bool error = false);

    uint32_t m_appid = 0;
    int m_seq = 0;                              // 切号代数：回调里对不上就丢弃
    QList<QJsonObject> m_images;                // 已加载的素材（顺序 = 平台返回顺序）
    QHash<QString, QPixmap> m_thumbs;           // image_id -> 缩小版缩略图（内存）
    QSet<QString> m_pendingThumbs;              // 正在下载的 image_id（防重复请求）
    QSet<QString> m_selection;                  // 选中的 image_id（切换视图/重建时还原）
    QString m_nextCursor;                       // 非空 = 还有下一页
    bool m_loading = false;
    // 程序化改选中（重建视图 / 切视图）时置位：否则 clear()/setSelected() 会触发
    // itemSelectionChanged → onSelectionChanged → 反过来把 m_selection 抹掉
    bool m_updatingViews = false;

    QListWidget *m_iconView = nullptr;
    QTreeWidget *m_treeView = nullptr;
    QStackedWidget *m_views = nullptr;
    QComboBox *m_viewMode = nullptr;
    QLabel *m_title = nullptr;                  // 「素材管理 · 机器人名」——素材按机器人分开，标题写清楚
    QLabel *m_status = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_btnUpload = nullptr;
    QPushButton *m_btnReload = nullptr;
    QPushButton *m_btnMore = nullptr;
    QPushButton *m_btnDelete = nullptr;
    QPushButton *m_btnCopyUrl = nullptr;
    QPushButton *m_btnCopyMd = nullptr;
    // 没有「预览」按钮：两个视图都支持双击看大图
};
