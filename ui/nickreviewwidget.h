#pragma once
#include <QWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <qpushbutton.h>
#include "botdb.h"

class NickReviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit NickReviewWidget(QWidget *parent = nullptr);
    ~NickReviewWidget();

    // 切换机器人：传入 appid，自动根据当前模式刷新
    void setAppId(uint32_t appid);

signals:
    // 申请模式：通过信号 (appId, userSeqId) 对 + 新昵称
    void approveRequested(const QList<QPair<uint32_t, uint32_t>>& idPairs,
                          const QStringList& newNicknames);

    // 批量模式：通过信号 (用户 seq_id 列表, 新昵称列表)
    void batchApproveRequested(const QList<uint32_t>& userSeqIds,
                               const QStringList& newNicknames);

    // 批量模式：拒绝信号 (用户 seq_id 列表)，外部将 name 置空
    void batchRejectRequested(const QList<uint32_t>& userSeqIds);

    // 批量模式：取消通过信号 (用户 seq_id 列表)，外部将 name 置空
    void batchCancelRequested(const QList<uint32_t>& userSeqIds);

private slots:
    void onModeChanged(int index);
    void onApprove();
    void onReject();
    void onCancel();
    void onSelectAll();
    void onSelectNone();
    void onInvert();
    void onEnableToggled(bool checked);
    void onLoadPending();
    void onLoadApproved();
    void prevPage();
    void nextPage();

private:
    void setupUI();
    void refresh();
    void loadFromApplicationDB();
    void loadFromBotDB();
    void removeReviews(const QList<QPair<uint32_t, uint32_t>>& idPairs);
    void updatePageLabel();
    void updateButtonsVisibility();

    enum class Mode { Application, BatchUser };
    Mode m_mode = Mode::Application;
    uint32_t m_appid = 0;
    BotDB* m_botDb = nullptr;

    QTableView* m_tableView;
    QStandardItemModel* m_model;
    QCheckBox* m_enableCheck;
    QComboBox* m_modeCombo;

    // 批量模式的分页
    int m_currentPage = 0;
    int m_pageSize = 50;
    int m_totalCount = 0;
    bool m_onlyNameEmpty = true;  // true: 显示 name 为空, false: 显示 name 非空

    // 按钮指针
    QPushButton *m_btnLoadPending, *m_btnLoadApproved;
    QPushButton *m_btnPrevPage, *m_btnNextPage;
    QLabel *m_pageLabel;
    QPushButton *m_btnLoadApp;      // 申请模式专用
    QPushButton *m_btnCancel;       // 取消通过
};