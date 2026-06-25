#ifndef STATUSCHARTWIDGET_H
#define STATUSCHARTWIDGET_H

#include <QWidget>
#include <QVector>

class StatusChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StatusChartWidget(QWidget *parent = nullptr);
    void setData(const QVector<int>& receiveData, const QVector<int>& sendData);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<int> m_receiveData;
    QVector<int> m_sendData;
    int m_maxValue;
};

#endif // STATUSCHARTWIDGET_H