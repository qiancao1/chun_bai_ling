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