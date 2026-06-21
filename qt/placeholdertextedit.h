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

private:
    QString m_placeholderText;
    QColor m_placeholderColor;
};

#endif // PLACEHOLDERTEXTEDIT_H