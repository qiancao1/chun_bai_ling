#ifndef SCROLLTEXTDIALOG_H
#define SCROLLTEXTDIALOG_H

/*
 * 可滚动的只读文本弹窗 —— 内容多的场景替代 QMessageBox
 *
 * 背景：QMessageBox 的尺寸会被屏幕"挤"住，内容一多就被截断，既不能滚动也不能
 * 全选复制。典型例子就是插件页的「指令」按钮：把所有插件的注册指令拼成一大段文本，
 * 插件一多就显示不全。这里给一个统一的替代品：QDialog + 只读 QPlainTextEdit
 * （自带横竖滚动条），带「复制全部」，且可自由拉伸窗口。
 *
 * 用法：
 *     ScrollTextDialog::show(this, "插件注册指令", text);
 *     // 需要别的初始尺寸时（会按屏幕可用区域自动收窄）：
 *     ScrollTextDialog::show(this, "标题", text, QSize(900, 700));
 *
 * 注意：刻意用 QPlainTextEdit 而不是 QTextEdit 有两个原因 ——
 *   1. 纯文本展示用 QPlainTextEdit 性能更好、更轻；
 *   2. 项目里 ui/buttoneditor.h、ui/set.h、ui/sandboxwindow.h 等头文件写着
 *      `#define QTextEdit PlaceholderTextEdit`，一旦被间接包含进来，写 QTextEdit
 *      就会被宏替换成 PlaceholderTextEdit（还得额外 include core/placeholdertextedit.h），
 *      QPlainTextEdit 没有任何宏替换它，完全绕开这个坑。
 *
 * 本文件是 header-only，只有 inline 函数，不需要加进 CMakeLists。
 */

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

namespace ScrollTextDialog {

// 弹窗里按 qMin/qMax 用
inline constexpr int kMinWidth  = 460;
inline constexpr int kMinHeight = 320;

// 显示一个可滚动的只读文本窗口（模态，exec() 返回即销毁，不用管生命周期）。
//   parent 可为空；title 为空时用"内容"；text 为空时显示占位提示。
inline void show(QWidget *parent, const QString &title, const QString &text,
                 const QSize &size = QSize(780, 600))
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title.trimmed().isEmpty() ? QStringLiteral("内容") : title);
    dialog.setModal(true);

    // 期望尺寸：先保底，再按屏幕可用区域收窄（小屏 / 高分屏缩放都不会顶出屏幕）
    QSize want(qMax(size.width(), kMinWidth), qMax(size.height(), kMinHeight));
    if (const QScreen *scr = QGuiApplication::primaryScreen()) {
        const QSize avail = scr->availableGeometry().size();
        want.setWidth(qMin(want.width(), qMax(kMinWidth, avail.width() - 80)));
        want.setHeight(qMin(want.height(), qMax(kMinHeight, avail.height() - 100)));
    }
    dialog.setMinimumSize(kMinWidth, kMinHeight);
    dialog.resize(want);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    QPlainTextEdit *edit = new QPlainTextEdit(&dialog);
    edit->setReadOnly(true);                              // 只读，但可选中 / 全选复制
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);        // 不折行：长行横向滚动，保住每行的结构
    edit->setPlainText(text.isEmpty() ? QStringLiteral("（没有可显示的内容）") : text);
    QFont font(QStringLiteral("Consolas"), 10);
    font.setStyleHint(QFont::Monospace);
    edit->setFont(font);
    edit->moveCursor(QTextCursor::Start);                 // 打开时停在顶部
    mainLayout->addWidget(edit, 1);                       // 拉伸占满整个窗口

    // 底部：内容量提示 + 复制全部 + 关闭
    QHBoxLayout *btnLayout = new QHBoxLayout;
    const int lines = text.isEmpty() ? 0 : text.count(QLatin1Char('\n')) + 1;
    QLabel *infoLabel = new QLabel(
        QStringLiteral("共 %1 行 / %2 字符").arg(lines).arg(text.size()));
    infoLabel->setStyleSheet(QStringLiteral("color:#888;"));
    btnLayout->addWidget(infoLabel);
    btnLayout->addStretch();

    QPushButton *copyBtn = new QPushButton(QStringLiteral("复制全部"), &dialog);
    // 以 copyBtn 作为 context：按钮随弹窗销毁后连接自动断开，不会回调到野指针
    QObject::connect(copyBtn, &QPushButton::clicked, copyBtn, [copyBtn, text]() {
        QApplication::clipboard()->setText(text);
        copyBtn->setText(QStringLiteral("✓ 已复制"));
        QTimer::singleShot(1200, copyBtn, [copyBtn]() {
            copyBtn->setText(QStringLiteral("复制全部"));
        });
    });
    btnLayout->addWidget(copyBtn);

    QPushButton *closeBtn = new QPushButton(QStringLiteral("关闭"), &dialog);
    closeBtn->setDefault(true);
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);

    (void)dialog.exec();
}

} // namespace ScrollTextDialog

#endif // SCROLLTEXTDIALOG_H
