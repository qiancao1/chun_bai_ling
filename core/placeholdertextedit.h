/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PLACEHOLDERTEXTEDIT_H
#define PLACEHOLDERTEXTEDIT_H

#include <QTextEdit>
#include <QColor>

class QMimeData;

// ===================== 带灰色占位符的 QTextEdit =====================
// 本工程所有多行输入框（群管整页、聊天输入框、AI 输入框、按钮编辑器、
// 违禁词页、沙箱页…）实际都是这个类，靠各页面头文件里的
// `#define QTextEdit PlaceholderTextEdit` 在 uic 生成的头里完成替换。
// 它身兼两职，两个都是必需品：
//
// ① 灰色占位符 —— 为什么不用 Qt 自带的 setPlaceholderText：
//    主窗口挂了一张全局样式表，其中有 `QWidget { color: #263241; }`。
//    QStyleSheetStyle 在 polish 时会把调色板里所有前景角色一起改写，
//    包括 QPalette::PlaceholderText，并且把原本 50% 的透明度抹成 100%，
//    于是原生占位符和正文同色，看起来像根本没设。
//    （`QTextEdit::placeholder { color: ... }` 不是 Qt 的合法选择器，写了无效。）
//    因此这里自己存文本、在 paintEvent 里自绘，颜色完全绕开调色板与样式表。
//
// ② 粘贴 / 拖放一律按纯文本插入，见 insertFromMimeData()。
//
// ⚠ 宏会被 include 传染：某个 TU 只要间接包含了带 #define 的头，里面写的
//   QTextEdit 就全变成这个类。想用原生 QTextEdit 得用 QTextBrowser 或
//   QPlainTextEdit 绕开（mainwindow.cpp、pluginpage 就是这么处理的）。
class PlaceholderTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit PlaceholderTextEdit(QWidget *parent = nullptr);

    void setPlaceholderText(const QString &text);
    QString placeholderText() const;

    void setPlaceholderColor(const QColor &color);
    QColor placeholderColor() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    // 粘贴/拖放一律按纯文本处理（详见 .cpp 里的说明）
    void insertFromMimeData(const QMimeData *source) override;

private:
    QString m_placeholderText;
    QColor m_placeholderColor;
};

#endif // PLACEHOLDERTEXTEDIT_H