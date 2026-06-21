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

#include "PlaceholderTextEdit.h"
#include <QPainter>

PlaceholderTextEdit::PlaceholderTextEdit(QWidget *parent)
    : QTextEdit(parent)
    , m_placeholderColor(175, 175, 175)
{
}

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
    update();
}

QColor PlaceholderTextEdit::placeholderColor() const
{
    return m_placeholderColor;
}

void PlaceholderTextEdit::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);

    if (!m_placeholderText.isEmpty() && toPlainText().isEmpty() && !hasFocus()) {
        QPainter painter(viewport());
        painter.setPen(m_placeholderColor);
        painter.setFont(font());

        QRect rect = viewport()->rect();
        int margin = 5;
        QRect textRect = rect.adjusted(margin, margin, -margin, -margin);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, m_placeholderText);
    }
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