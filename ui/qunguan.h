#ifndef QUNGUAN_H
#define QUNGUAN_H

#include <QWidget>

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

    void on_checkBox_2_checkStateChanged(const Qt::CheckState &arg1);

private:
    Ui::qunguan *ui;
};

#endif // QUNGUAN_H
