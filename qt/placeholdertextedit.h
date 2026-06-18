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