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
    QComboBox* m_aimode; //ai模型列表
    bool addReview(uint32_t appId, uint32_t userSeqId, const QString &newNickname, uint32_t timestamp);


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
    void onAiApprove();
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
    //void onSearch();

protected:
    void showEvent(QShowEvent *event) override;   // 每次显示时重建插件下拉项（插件是异步加载的）

private:
    void setupUI();
    void refresh();
    void loadFromApplicationDB();
    void loadFromBotDB();
    void removeReviews(const QList<QPair<uint32_t, uint32_t>>& idPairs);
    void updatePageLabel();
    void updateButtonsVisibility();

    // ---- 插件审核模式 ----
    void refreshPluginItems();                        // 重建 m_modeCombo 里的插件项
    // 调插件 get_review_list(开始位置,数量,状态) 拉一页并填表；state: 1=待审核 2=已审核
    void loadFromPluginPage(int start, int count, int state);
    void submitPluginReview(bool approved);           // 投递 {"状态":同意/拒绝,"data":[{id,name,id2}]} 给 submit_review
    QString pluginCallInts(int pluginIndex, int start, int count, int state);     // 调插件 get_review_list（加载时已取好地址）
    QString pluginCallStr(int pluginIndex, const QString &argJson);              // 调插件 submit_review（加载时已取好地址）

    enum class Mode { Application, BatchUser, Plugin };
    Mode m_mode = Mode::Application;
    uint32_t m_appid = 0;
    BotDB* m_botDb = nullptr;
    int m_pluginIndex = -1;   // 插件模式下在 m_pluginList 里的下标

    QTableView* m_tableView;
    QStandardItemModel* m_model;
    QCheckBox* m_enableCheck,*m_qzsy;
    QComboBox* m_modeCombo;
    QLineEdit *m_ss;
    // 批量模式的分页
    int m_currentPage = 0;
    int m_pageSize = 500;
    int m_totalCount = 0;
    bool m_onlyNameEmpty = true;  // true: 显示 name 为空, false: 显示 name 非空

    // 按钮指针
    QPushButton *m_btnLoadPending, *m_btnLoadApproved;
    QPushButton *m_btnPrevPage, *m_btnNextPage;
    QLabel *m_pageLabel;
    QPushButton *m_btnLoadApp;      // 申请模式专用
    QPushButton *m_btnCancel;       // 取消通过
};