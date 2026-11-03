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

#include "placeholdertextedit.h"
#include <QPainter>
#include <QMimeData>
#include <QTextDocument>

PlaceholderTextEdit::PlaceholderTextEdit(QWidget *parent)
    : QTextEdit(parent)
    , m_placeholderColor(175, 175, 175)
{
}

// 粘贴一律纯文本。
// QTextEdit 默认 acceptRichText = true，剪贴板里只要有 text/html 就走富文本分支：
// 从网页/Word/AI 回答里复制过来的内容会带一堆标签和样式（还可能是转义过的 html 源码），
// 粘进来就成了「一堆 html」。这里直接接管 paste/drop：
//   ① 图片（截图、图片文件）仍交给基类，聊天页靠它插本地图片占位符；
//   ② 有 text/plain 就只用 text/plain；
//   ③ 剪贴板只给了 html（没有 text/plain）时，先把 html 转成纯文本再插，
//      否则 Qt 会什么都不插（acceptRichText=false 时也不会回退到 html）。
void PlaceholderTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (!source) {
        QTextEdit::insertFromMimeData(source);
        return;
    }
    if (source->hasImage() || source->hasUrls()) {   // 图片 / 拖文件：保持原有行为
        QTextEdit::insertFromMimeData(source);
        return;
    }
    if (source->hasText()) {
        insertPlainText(source->text());
        return;
    }
    if (source->hasHtml()) {
        QTextDocument doc;
        doc.setHtml(source->html());
        insertPlainText(doc.toPlainText());
        return;
    }
    QTextEdit::insertFromMimeData(source);
}

// ⚠ 基类的 setPlaceholderText 不是 virtual，这里是同名隐藏。
// 故意**不转发**给基类：基类画占位符用的是 QPalette::PlaceholderText，
// 本工程挂了全局样式表（QStyleSheetStyle）之后这个角色被改成了正文色（黑色），
// 于是提示看起来和正文一模一样。所以这里只存文本，颜色/换行由 paintEvent 自绘。
void PlaceholderTextEdit::setPlaceholderText(const QString &text)
{
    m_placeholderText = text;
    update();
}

QString PlaceholderTextEdit::placeholderText() const
{
    return m_placeholderText;
}

void PlaceholderTextEdit::setPlaceholderColor(const QColor &color)
{
    m_placeholderColor = color;
    // 同时也写进调色板：万一别处把 setPlaceholderText 转发给了基类，颜色也不会是黑的
    QPalette pal = palette();
    pal.setColor(QPalette::PlaceholderText, color);
    setPalette(pal);
    update();
}

QColor PlaceholderTextEdit::placeholderColor() const
{
    return m_placeholderColor;
}

// 自绘占位符（灰色 + 自动换行）。
// 历史坑：原来这里在 QTextEdit::paintEvent 之后直接 `return ;`，下面的绘制永远执行不到，
// 而 setPlaceholderText 又被本类隐藏、没转发基类 → 被 `#define QTextEdit PlaceholderTextEdit`
// 换成本类的文本框（群管整页、聊天输入框、按钮编辑器、违禁词页…）一个提示都看不到。
void PlaceholderTextEdit::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);

    if (m_placeholderText.isEmpty()) return;
    if (!toPlainText().trimmed().isEmpty()) return;   // 有内容（哪怕只敲了空格）就不画

    QPainter painter(viewport());
    painter.setPen(m_placeholderColor);              // 默认 175,175,175 ≈ #AFAFAF
    painter.setFont(font());

    const int margin = 5;
    QRect rect = viewport()->rect().adjusted(margin, margin, -margin, -margin);
    // TextWordWrap：长提示按宽度折行（以前没有这个标志，长文本会被裁掉一行）
    painter.drawText(rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_placeholderText);
}

void PlaceholderTextEdit::focusInEvent(QFocusEvent *event)
{
    QTextEdit::focusInEvent(event);
    update();
}

void PlaceholderTextEdit::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);
    update();
}