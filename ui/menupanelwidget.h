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
#include "PlaceholderLineEdit.h"
#define QLineEdit PlaceholderLineEdit
class QQBotClient;

class MenuPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MenuPanelWidget(QWidget *parent = nullptr);
    QQBotClient *m_botClient = nullptr;

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

    // ---- 面板 ----
    void onAddTableRow();
    void onRemoveTableRow();
    void onUpdatePanel();

private:
    void setupUI();
    void updateStatus(const QString &msg, bool isError = false);

    void loadMenuItems(const QJsonArray &items);
    QJsonArray buildMenuJson();
    void applyMenuNodeToUI(QTreeWidgetItem *item);
    void clearMenuEdit();
    void onMenuTypeChanged(const QString &type);

    void loadPanelData(const QJsonObject &panelObj);
    QJsonObject buildPanelJson();
     void onScopeChanged();   // 场景切换时加载面板


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
    QComboBox *m_scopeCombo;
    QComboBox *m_targetTypeCombo;   // 作用范围 all/specific
    QTableWidget *m_panelTable;
    QPushButton *m_addRowBtn;
    QPushButton *m_removeRowBtn;
    QPushButton *m_updatePanelBtn;

    QStatusBar *m_statusBar;

    QString m_currentPanelId;   // 用于更新面板
};

#endif // MENUPANELWIDGET_H