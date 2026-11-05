#ifndef STATUSCHARTWIDGET_H
#define STATUSCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QRectF>

// 消息趋势柱状图（纯 QPainter 手绘，无 QtCharts 依赖）
//
// 排版约定：
//   ┌──────────────────────────────────────┐
//   │                        ● 接收  ● 发送 │  ← 图例右上角，16px
//   │  4 ┤┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄  │
//   │  2 ┤┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄  │
//   │  0 └──────────────────────────────────│  ← 零基线实线
//   │      0    2    4    6   …   22        │  ← 每 2 小时一个刻度
//   └──────────────────────────────────────┘
class StatusChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StatusChartWidget(QWidget *parent = nullptr);

    void setData(const QVector<int> &receiveData, const QVector<int> &sendData);

    QSize sizeHint() const override { return QSize(640, 200); }
    QSize minimumSizeHint() const override { return QSize(280, 120); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // 鼠标位置 → 小时下标（不在绘图区内返回 -1）；只在 paintEvent 更新过几何后才有效
    int hourAt(const QPointF &pos) const;

    QVector<int> m_receiveData;
    QVector<int> m_sendData;

    // 最近一次绘制算出的绘图区几何，供 hover 命中和重绘使用
    QRectF m_plotRect;
    double m_groupWidth = 1.0;
    int m_hoverHour = -1;
};

#endif // STATUSCHARTWIDGET_H
