#ifndef BUTTONEDITOR_H
#define BUTTONEDITOR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMap>
#include <QJsonDocument>
#include <qcheckbox.h>
#include <qcombobox.h>
#include <qpushbutton.h>
#include <qscrollarea.h>
#include <qtextedit.h>

#include "placeholderlineedit.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换

// 全局替换宏
#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit
class DragButton;
class ButtonPropertyPanel;
struct ButtonData;

struct ButtonData {
    int id;
    QString buttonId;
    QString label;
    QString visitedLabel;
    int style=1;                   // 0=灰框,1=蓝框
    int actionType=2;              // 0=跳转,1=回调,2=指令
    int permissionType=2;          // 0=指定用户,1=仅管理者,2=所有人,3=指定身份组
    QStringList specifyUserIds;
    QStringList specifyRoleIds;
    QString actionData;
    bool reply=false;
    bool enter=true;
    int anchor=0;                  // 0/1 唤起选图器
    QString unsupportTips;

    // ========== 新增字段（模态框）==========
    QString modalContent;        // 弹窗内容，最多40字符
    QString modalConfirmText;    // 确认按钮文本，最多4字符
    QString modalCancelText;     // 取消按钮文本，最多4字符

    // ========== 新增字段（订阅数据）==========
    int subscribeTemplateId;     // 模板ID
    QString subscribeCustomId;   // 自定义订阅ID

    ButtonData();
    bool fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

class DragButton : public QPushButton {
    Q_OBJECT
public:
    explicit DragButton(int id, const QString &text, QWidget *parent = nullptr);
    int getId() const { return m_id; }
    void setButtonData(const ButtonData &data);
    ButtonData getButtonData() const { return m_data; }
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    int m_id;
    ButtonData m_data;
    QPoint m_dragStartPos;
};

class ButtonPropertyPanel : public QWidget {
    Q_OBJECT
public:
    explicit ButtonPropertyPanel(QWidget *parent = nullptr);
    void setCurrentButton(DragButton *button);
    void clearSelection();

signals:
    void buttonDataChanged(int buttonId, const ButtonData &newData);

private slots:
    void emitDataChanged();

private:
    void updateUiFromCurrentButton();
    DragButton *m_currentButton;

    // 基础控件
    QLineEdit *m_buttonIdEdit;
    QLineEdit *m_labelEdit;
    QLineEdit *m_visitedLabelEdit;
    QComboBox *m_styleCombo;
    QComboBox *m_actionTypeCombo;
    QLineEdit *m_dataEdit;
    QLineEdit *m_unsupportTipsEdit;
    QCheckBox *m_replyCheck;
    QCheckBox *m_enterCheck;
    QCheckBox *m_anchorCheck;

    // 权限
    QComboBox *m_permissionTypeCombo;
    QTextEdit *m_userIdListEdit;
    QTextEdit *m_roleIdListEdit;

    // 模态框
    QLineEdit *m_modalContentEdit;
    QLineEdit *m_modalConfirmEdit;
    QLineEdit *m_modalCancelEdit;

    // 订阅数据
    QLineEdit *m_subscribeTemplateIdEdit;
    QLineEdit *m_subscribeCustomIdEdit;
};


class ButtonEditor : public QWidget {
    Q_OBJECT
public:
    explicit ButtonEditor(QWidget *parent = nullptr);
    ~ButtonEditor();

private slots:
    void addNewRow();               // 新增一行
    void deleteSelectedButton();    // 删除当前选中按钮
    void clearAllButtons();         // 清空所有
    void generateJson();
    void loadFromJson();
    void copyToClipboard();
    void saveToFile();
    void onButtonClicked(DragButton *button);
    void onButtonDataChanged(int buttonId, const ButtonData &newData);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event)override;
    void dropEvent(QDropEvent *event)override;
private:
    void initUI();
    void rebuildAllRows();                      // 根据 m_buttonRows 完全重建 UI
    QWidget* createRowWidget(int rowIndex);     // 创建单行 UI（按钮 + ➕）
    void addButtonToRow(int rowIndex);          // 在指定行末尾添加按钮
    void refreshRow(int rowIndex);              // 刷新某行 UI
    void updateButtonWidths();                  // 自适应按钮宽度
    ButtonData getDefaultButtonData(int id) const;

    // 数据模型
    QList<QList<DragButton*>> m_buttonRows;   // 行 -> 按钮列表
    QMap<int, DragButton*> m_buttonMap;       // id -> 按钮指针
    int m_nextButtonId;
    DragButton *m_currentSelectedButton;      // 当前选中的按钮

    // UI 组件
    QScrollArea *m_scrollArea;
    QWidget *m_scrollContent;
    QVBoxLayout *m_rowsLayout;                // 垂直布局，每行一个 QWidget
    QList<QWidget*> m_rowWidgets;             // 每行的容器 Widget
    ButtonPropertyPanel *m_propertyPanel;
    QTextEdit *m_jsonPreview;
    static const int FIXED_BUTTON_WIDTH = 100;   // 固定按钮宽度（像素）
    static const int MAX_COLUMNS = 10;
    static const int MAX_ROWS = 5;
};

#endif // BUTTONEDITOR_H