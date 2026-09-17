#ifndef THEMECOLORWIDGET_H
#define THEMECOLORWIDGET_H

// 界面配色设置控件（嵌在 高级设置 → 基础设置 里）
//
// 一行一个色块，点一下弹系统调色板；改完立刻生效（主窗口重套样式表）并落库。
// 具体有哪些可调项由 ui/themecolors.h 的 roles() 决定，这里不写死。

#include <QWidget>
#include <QHash>
#include <QString>

class QGridLayout;
class QPushButton;

class ThemeColorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ThemeColorWidget(QWidget *parent = nullptr);

private:
    void setupUI();
    void refreshSwatches();                 // 按当前配色重画所有色块
    void pickColor(const QString &key);     // 弹调色板并应用

    QGridLayout *m_grid = nullptr;
    QHash<QString, QPushButton *> m_swatches;   // key -> 色块按钮
};

#endif // THEMECOLORWIDGET_H
