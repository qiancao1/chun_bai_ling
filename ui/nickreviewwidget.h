#pragma once
#include <QWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QCheckBox>
#include "lmdbkv.h"

struct ReviewRecord {
    uint32_t appId;        // 应用ID
    uint32_t userSeqId;    // 用户序列号
    uint32_t timestamp;    // 申请时间（Unix 秒）
    char nickname[64];     // 新昵称（UTF-8）

    QByteArray serialize() const {
        QByteArray data;
        data.resize(sizeof(ReviewRecord));
        memcpy(data.data(), this, sizeof(ReviewRecord));
        return data;
    }

    static ReviewRecord deserialize(const QByteArray &data) {
        ReviewRecord rec;
        if (data.size() >= sizeof(ReviewRecord))
            memcpy(&rec, data.constData(), sizeof(ReviewRecord));
        else
            memset(&rec, 0, sizeof(ReviewRecord));
        return rec;
    }
};
class NickReviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit NickReviewWidget(LmdbKV *db, QWidget *parent = nullptr);
    ~NickReviewWidget();

    // 添加/更新审核记录（相同(appid,userSeqId)会覆盖）
    bool addReview(uint32_t appId, uint32_t userSeqId, const QString &nickname, uint32_t timestamp);

    // 刷新表格
    void loadReviews();

    // 是否启用审核（开关控制）
    bool isEnabled() const { return m_enableCheck->isChecked(); }

signals:
    // 通过审核：发射 (appId, userSeqId) 对列表和新昵称列表，外部执行更新后控件自动删除
    void approveRequested(const QList<QPair<uint32_t, uint32_t>> &idPairs, const QStringList &newNicknames);

private slots:
    void onApprove();
    void onReject();
    void onSelectAll();
    void onSelectNone();
    void onInvert();
    void onEnableToggled(bool checked);

private:
    void setupUI();
    void removeReviews(const QList<QPair<uint32_t, uint32_t>> &idPairs);
    static QByteArray makeKey(uint32_t appId, uint32_t userSeqId);
    static void parseKey(const QByteArray &key, uint32_t &appId, uint32_t &userSeqId);

    LmdbKV *m_db;
    QTableView *m_tableView;
    QStandardItemModel *m_model;
    QCheckBox *m_enableCheck;
};