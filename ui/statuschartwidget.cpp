#include "StatusChartWidget.h"
#include <QPainter>


StatusChartWidget::StatusChartWidget(QWidget *parent)
    : QWidget(parent), m_maxValue(120)
{
    m_receiveData.resize(24);
    m_sendData.resize(24);
    for (int i = 0; i < 24; ++i) {
        m_receiveData[i] = rand() % 100 + 10;
        m_sendData[i] = rand() % 80 + 5;
    }
}

void StatusChartWidget::setData(const QVector<int>& receiveData, const QVector<int>& sendData)
{
    if (receiveData.size() == 24 && sendData.size() == 24) {
        m_receiveData = receiveData;
        m_sendData = sendData;
        int maxVal = 1;
        for (int i = 0; i < 24; ++i) {
            if (m_receiveData[i] > maxVal) maxVal = m_receiveData[i];
            if (m_sendData[i] > maxVal) maxVal = m_sendData[i];
        }
        m_maxValue = static_cast<int>(maxVal * 1.2) + 1;
        update();
    }
}

void StatusChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int margin = 40;
    int left = margin;
    int right = width() - margin;
    int top = margin;
    int bottom = height() - margin;
    int chartWidth = right - left;
    int chartHeight = bottom - top;

    int groupWidth = chartWidth / 24;
    int barWidth = groupWidth * 0.35;
    int gapBetweenBars = groupWidth * 0.1;
    int gapBetweenGroups = groupWidth * 0.15;

    // 网格线
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    for (int i = 0; i <= 5; ++i) {
        int y = top + chartHeight - (i * chartHeight / 5);
        painter.drawLine(left, y, right, y);
        painter.setPen(Qt::black);
        painter.drawText(5, y + 5, QString::number(i * m_maxValue / 5));
    }

    // 柱子
    for (int hour = 0; hour < 24; ++hour) {
        int groupX = left + hour * groupWidth + gapBetweenGroups / 2;

        int receiveHeight = (m_receiveData[hour] * chartHeight) / m_maxValue;
        int receiveX = groupX;
        int receiveY = bottom - receiveHeight;
        painter.setBrush(QBrush(Qt::blue));
        painter.setPen(Qt::NoPen);
        painter.drawRect(receiveX, receiveY, barWidth, receiveHeight);

        int sendHeight = (m_sendData[hour] * chartHeight) / m_maxValue;
        int sendX = groupX + barWidth + gapBetweenBars;
        int sendY = bottom - sendHeight;
        painter.setBrush(QBrush(Qt::green));
        painter.drawRect(sendX, sendY, barWidth, sendHeight);

        painter.setPen(Qt::black);
        painter.drawText(groupX, bottom + 20, groupWidth, 20,
                         Qt::AlignHCenter | Qt::AlignTop, QString::number(hour));
    }

    // 图例
    painter.setBrush(Qt::blue);
    painter.drawRect(width()/2 - 45, 0, 20, 20);
    painter.drawText(width()/2 - 20, 15, "接收");
    painter.setBrush(Qt::green);
    painter.drawRect(width()/2 + 20, 0, 20, 20);
    painter.drawText(width()/2 + 45 , 15, "发送");
}