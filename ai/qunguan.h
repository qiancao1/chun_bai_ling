#ifndef QUNGUAN_H
#define QUNGUAN_H

#include <QWidget>
#include <qlistwidget.h>

namespace Ui {
class qunguan;
}

class qunguan : public QWidget
{
    Q_OBJECT

public:
    explicit qunguan(QWidget *parent = nullptr);
    ~qunguan();
    void 列表行被单击();

private slots:
    void on_pushButton_clicked();
    void onItemClicked(QListWidgetItem *item);



private:
    void refreshList();
    // 「全部机器人」模式下点「保存页面」走这里：弹窗勾选要应用的机器人，
    // 只把左侧当前页面的文本写过去（上方输入框 / 复选框属于单个账号，不参与批量）。
    void batchSaveCurrentField();
    QString currentPageName() const;      // 左侧当前选中项的文字（如「签到」）
    QString m_currentField;
    Ui::qunguan *ui;
};

#endif // QUNGUAN_H
