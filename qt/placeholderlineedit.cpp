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