#ifndef PLACEHOLDERLINEEDIT_H
#define PLACEHOLDERLINEEDIT_H

#include <QLineEdit>
#include <QColor>

class PlaceholderLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit PlaceholderLineEdit(QWidget *parent = nullptr);

    void setPlaceholderText(const QString &text);
    QString placeholderText() const;

    void setPlaceholderColor(const QColor &color);
    QColor placeholderColor() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_placeholderText;
    QColor m_placeholderColor;
};

#endif // PLACEHOLDERLINEEDIT_H