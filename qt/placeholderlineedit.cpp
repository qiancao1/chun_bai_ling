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

#include "PlaceholderLineEdit.h"
#include <QPainter>

PlaceholderLineEdit::PlaceholderLineEdit(QWidget *parent)
    : QLineEdit(parent)
    , m_placeholderColor(175, 175, 175) // #AfAfAf
{
}

void PlaceholderLineEdit::setPlaceholderText(const QString &text)
{
    m_placeholderText = text;
    update();
}

QString PlaceholderLineEdit::placeholderText() const
{
    return m_placeholderText;
}

void PlaceholderLineEdit::setPlaceholderColor(const QColor &color)
{
    m_placeholderColor = color;
    update();
}

QColor PlaceholderLineEdit::placeholderColor() const
{
    return m_placeholderColor;
}

void PlaceholderLineEdit::paintEvent(QPaintEvent *event)
{
    QLineEdit::paintEvent(event);

    if (!m_placeholderText.isEmpty() && text().isEmpty() && !hasFocus()) {
        QPainter painter(this);
        painter.setPen(m_placeholderColor);
        painter.setFont(font());

        QRect r = rect();
        int left = 5; // 边距，与系统默认一致
        int top = (r.height() - painter.fontMetrics().height()) / 2;
        painter.drawText(left, top + painter.fontMetrics().ascent(), m_placeholderText);
    }
}