#ifndef PLTS_H
#define PLTS_H

#include <QWidget>

namespace Ui {
class plts;
}

class plts : public QWidget
{
    Q_OBJECT

public:
    explicit plts(QWidget *parent = nullptr);
    ~plts();

private slots:
    void on_ksts_clicked(bool checked);
    void on_sctswj_2_clicked(bool checked);
    void on_sctswj_clicked(bool checked);
    void on_tzts_clicked(bool checked);

private:
    bool 保存();
    void 加载();
    Ui::plts *ui;
    QTimer *m_saveTimer;          // 定时保存定时器
};

#endif // PLTS_H
