#ifndef MENUPANELWIDGET_H
#define MENUPANELWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QStatusBar>
#include <QMap>
#include <qlistwidget.h>
#include <qradiobutton.h>
#include "placeholderlineedit.h"
#define QLineEdit PlaceholderLineEdit
class QQBotClient;
class MovableTableWidget : public QTableWidget
{
    Q_OBJECT
public:
    explicit MovableTableWidget(QWidget *parent = nullptr);
signals:
    void rowsSwapped(int fromRow, int toRow);
protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dropEvent(QDropEvent *event) override;
private:
    int dragStartRow = -1;
};
class MenuPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MenuPanelWidget(QWidget *parent = nullptr);
    QQBotClient *m_botClient = nullptr;

signals:
    void needRefreshTable();  // 异步刷新信号

public slots:
    void switchBot();   // 切换Bot，自动加载菜单和面板

private slots:
    // ---- 菜单 ----
    void onMenuItemClicked(QTreeWidgetItem *item, int col);
    void onAddTopLevel();
    void onAddChild();
    void onDeleteSelected();
    void onSaveMenuNode();
    void onUpdateMenu();
    void refreshTable();
    // ---- 面板 ----
    void onAddTableRow();
    void onRemoveTableRow();
    void onUpdatePanel();

    void onPanelListItemClicked(QTableWidgetItem *item);
    void onCreatePanel();
    void onDeleteSelectedPanel();
    void onEditGroups();
    void onEditFriends();
    void onScopeRadioClicked(int id);              // 场景单选按钮切换
    void onPanelRemarkChanged(QTableWidgetItem *item);   // 备注编辑后更新缓存
    void onTargetTypeChanged(int index);   // target_type 下拉框切换
    void onRowsSwapped(int fromRow, int toRow);
    void onTableItemChanged(QTableWidgetItem *item);
    void onComboBoxChanged(int row, QComboBox *combo);
    void onCheckBoxChanged(int row, QCheckBox *chk);



private:
    void setupUI();
    void loadPanelsForCurrentScope();
    void updateStatus(const QString &msg, bool isError = false);
    QString currentScope() const;
    void loadMenuItems(const QJsonArray &items);
    QJsonArray buildMenuJson();
    void applyMenuNodeToUI(QTreeWidgetItem *item);
    void clearMenuEdit();
    void onMenuTypeChanged(const QString &type);
    void loadPanelList(const QJsonObject &responseObj);
    void loadPanelData(const QJsonObject &panelObj);

    QJsonObject buildPanelJson();
     void onScopeChanged();   // 场景切换时加载面板
    void loadPanelTarget(const QJsonObject &record);   // 从记录中提取 target 列表
    void updatePanelTargetIfNeeded();                  // 对比并更新 target
    void fetchPanelDetail(const QString &panelId);     // 获取面板详情并加载
    void updateEditButtons();
    void applyTargetChanges(const QStringList &newGroups, const QStringList &newFriends);
    void updateCacheTarget(const QStringList &groups, const QStringList &friends);
    // ---- 菜单控件 ----
    QTreeWidget *m_menuTree;

    QLineEdit *m_menuTitleEdit;
    QComboBox *m_menuTypeCombo;
    QLineEdit *m_menuDataEdit;
    QCheckBox *m_menuSwitchDefault;
    QPushButton *m_saveMenuBtn;
    QPushButton *m_addTopBtn;
    QPushButton *m_addChildBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_updateMenuBtn;

    // ---- 面板控件 ----
    QList<QJsonObject> m_currentItems;   // 数据列表
                    // 刷新表格

    QComboBox *m_targetTypeCombo;   // 作用范围 all/specific
    QTableWidget *m_panelTable;
    QPushButton *m_addRowBtn;
    QPushButton *m_removeRowBtn;
    QPushButton *m_updatePanelBtn;
    QButtonGroup *m_scopeGroup;
    QRadioButton *m_scopeC2C;
    QRadioButton *m_scopeGroupChat;
    QRadioButton *m_scopeChannel;
    QRadioButton *m_scopeDM;
    QTableWidget *m_panelListTable;

    QPushButton *m_editGroupsBtn;
    QPushButton *m_editFriendsBtn;

    // 存储绑定的群ID列表和好友ID列表
    QStringList m_bindGroups;
    QStringList m_bindFriends;
    // 面板管理按钮
    QStatusBar *m_statusBar;
    QPushButton *m_createPanelBtn;

    QPushButton *m_deletePanelBtn;
    QString m_currentPanelId;   // 用于更新面板

    QMap<QString, QJsonObject> m_panelCache; // 缓存 panel_id -> PanelRecord 对象
};

#endif // MENUPANELWIDGET_H