#include "statuschartwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace {

const QColor kReceiveColor (0x4C, 0x8D, 0xFF);   // 接收：蓝
const QColor kSendColor    (0x46, 0xC9, 0x8B);   // 发送：绿
const QColor kGridColor    (0xF0, 0xE6, 0xDA);   // 网格虚线（米色系）
const QColor kBaseColor    (0xE2, 0xD3, 0xC1);   // 零基线实线
const QColor kAxisText     (0x99, 0xA2, 0xB0);   // 刻度文字
const QColor kHintText     (0xB2, 0xBA, 0xC6);   // 无数据提示
const QColor kHoverBand    (255, 127, 50, 16);   // hover 列高亮
const QColor kTipBg        (0x33, 0x3D, 0x4A);   // 浮标底色

// 柱子只在顶部做圆角（底部贴着基线，圆了就成悬空了）
void addBarPath(QPainterPath &path, const QRectF &r)
{
    const double rad = qMin(3.0, qMin(r.width() / 2.0, r.height()));
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + rad);
    path.quadTo(r.left(), r.top(), r.left() + rad, r.top());
    path.lineTo(r.right() - rad, r.top());
    path.quadTo(r.right(), r.top(), r.right(), r.top() + rad);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
}

}  // namespace

StatusChartWidget::StatusChartWidget(QWidget *parent)
    : QWidget(parent)
{
    // ⚠ 这里以前是 rand() 填假数据：chartPanel 构造时会先画一帧，切到首页能看见
    //   一秒钟的假柱子。改成全 0，真实数据由 setData() 灌进来。
    m_receiveData.fill(0, 24);
    m_sendData.fill(0, 24);
    setMouseTracking(true);          // hover 需要（不开只有按住鼠标才收 mousemove）
    setMinimumHeight(130);
}

void StatusChartWidget::setData(const QVector<int> &receiveData, const QVector<int> &sendData)
{
    if (receiveData.size() != 24 || sendData.size() != 24)
        return;

    m_receiveData = receiveData;
    m_sendData = sendData;
    update();
}

void StatusChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int hours = 24;
    if (m_receiveData.isEmpty() || m_sendData.isEmpty())
        return;

    // ---------- 1. Y 轴刻度 ----------
    // 旧写法是 i * max / 5 再取整，最大值只有 2 时会得到「2 1 1 0 0」这种重复刻度。
    // 改成取 1/2/5×10^n 的「整」步长，且计数型数据步长不小于 1。
    int rawMax = 0;
    for (int i = 0; i < hours; ++i) {
        rawMax = std::max(rawMax, m_receiveData.at(i));
        rawMax = std::max(rawMax, m_sendData.at(i));
    }

    double step = 1.0;
    int gridCount = 4;
    if (rawMax > 4) {
        const double rough = rawMax / 4.0;
        const double mag = std::pow(10.0, std::floor(std::log10(rough)));
        const double norm = rough / mag;
        const double mult = (norm <= 1.0) ? 1.0 : (norm <= 2.0 ? 2.0 : (norm <= 5.0 ? 5.0 : 10.0));
        step = std::max(1.0, mult * mag);
        gridCount = static_cast<int>(std::ceil(rawMax / step));
        if (gridCount < 4) gridCount = 4;
    }
    const double axisMax = step * gridCount;

    // ---------- 2. 排版尺寸 ----------
    QFont small = font();
    small.setPixelSize(11);
    const QFontMetrics fm(small);

    const double legendH = 16.0;      // 顶部图例条
    const double axisLabelH = 18.0;   // 底部小时刻度
    const double axisLabelW = fm.horizontalAdvance(QString::number(static_cast<int>(axisMax))) + 10.0;

    m_plotRect = QRectF(axisLabelW + 6.0,
                        legendH + 6.0,
                        width() - (axisLabelW + 6.0) - 8.0,
                        height() - (legendH + 6.0) - axisLabelH - 4.0);
    if (m_plotRect.width() < 40.0 || m_plotRect.height() < 30.0) {
        m_groupWidth = 1.0;           // 太窄了，硬画只会糊成一团
        return;
    }

    m_groupWidth = m_plotRect.width() / hours;
    const double barW = qBound(2.0, m_groupWidth * 0.34, 12.0);
    const double barGap = qMax(1.0, m_groupWidth * 0.08);
    const double barsW = barW * 2.0 + barGap;
    const double barOffset = qMax(0.0, (m_groupWidth - barsW) / 2.0);

    // 小时刻度密度：格子够宽就每 2 小时一个，窄了退到每 4 / 6 小时
    const int xStep = (m_groupWidth >= 24.0) ? 2 : ((m_groupWidth >= 12.0) ? 4 : 6);

    // ---------- 3. 网格 + Y 刻度 ----------
    p.setFont(small);
    for (int i = 0; i <= gridCount; ++i) {
        const double value = step * i;
        const double y = m_plotRect.bottom() - (value / axisMax) * m_plotRect.height();
        const bool zero = (i == 0);

        p.setPen(QPen(zero ? kBaseColor : kGridColor, 1.0,
                      zero ? Qt::SolidLine : Qt::DashLine));
        p.drawLine(QPointF(m_plotRect.left(), y), QPointF(m_plotRect.right(), y));

        p.setPen(kAxisText);
        p.drawText(QRectF(0.0, y - 8.0, m_plotRect.left() - 8.0, 16.0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(static_cast<int>(value)));
    }

    // ---------- 4. hover 列高亮（画在柱子下面） ----------
    if (m_hoverHour >= 0 && m_hoverHour < hours) {
        const QRectF band(m_plotRect.left() + m_hoverHour * m_groupWidth,
                          m_plotRect.top(), m_groupWidth, m_plotRect.height());
        p.fillRect(band, kHoverBand);
    }

    // ---------- 5. 柱子 ----------
    auto paintBar = [&](double x, int value, const QColor &color) {
        if (value <= 0)
            return;
        double h = (value / axisMax) * m_plotRect.height();
        h = std::max(h, 1.5);         // 1 条消息也要看得见
        QPainterPath path;
        addBarPath(path, QRectF(x, m_plotRect.bottom() - h, barW, h));
        p.fillPath(path, color);
    };

    for (int hour = 0; hour < hours; ++hour) {
        const double gx = m_plotRect.left() + hour * m_groupWidth + barOffset;
        paintBar(gx, m_receiveData.at(hour), kReceiveColor);
        paintBar(gx + barW + barGap, m_sendData.at(hour), kSendColor);
    }

    // ---------- 6. 底部小时刻度 ----------
    p.setPen(kAxisText);
    for (int hour = 0; hour < hours; hour += xStep) {
        const double cx = m_plotRect.left() + (hour + 0.5) * m_groupWidth;
        p.drawText(QRectF(cx - 20.0, m_plotRect.bottom() + 2.0, 40.0, axisLabelH),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(hour));
    }

    // ---------- 7. 图例（右上角，旧版画在 width()/2 处会和标题打架） ----------
    const int recvTextW = fm.horizontalAdvance(QStringLiteral("接收"));
    const int sendTextW = fm.horizontalAdvance(QStringLiteral("发送"));
    const double dot = 8.0, dotGap = 5.0, blockGap = 14.0;
    const double legendW = dot + dotGap + recvTextW + blockGap + dot + dotGap + sendTextW;

    double lx = m_plotRect.right() - legendW;
    if (lx < m_plotRect.left()) lx = m_plotRect.left();
    const double ly = 1.0;

    auto paintLegend = [&](double x, const QColor &color, const QString &text, int textW) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(x, ly + 2.5, dot, dot), 2.5, 2.5);
        p.setPen(kAxisText);
        p.drawText(QRectF(x + dot + dotGap, ly, textW, legendH),
                   Qt::AlignLeft | Qt::AlignVCenter, text);
    };
    paintLegend(lx, kReceiveColor, QStringLiteral("接收"), recvTextW);
    paintLegend(lx + dot + dotGap + recvTextW + blockGap,
                kSendColor, QStringLiteral("发送"), sendTextW);

    // ---------- 8. 无数据提示 ----------
    if (rawMax == 0) {
        QFont hint = small;
        hint.setPixelSize(12);
        p.setFont(hint);
        p.setPen(kHintText);
        p.drawText(m_plotRect, Qt::AlignCenter, QStringLiteral("今日暂无消息记录"));
        p.setFont(small);
    }

    // ---------- 9. hover 浮标 ----------
    if (m_hoverHour >= 0 && m_hoverHour < hours && m_plotRect.height() > 40.0) {
        const int recv = m_receiveData.at(m_hoverHour);
        const int send = m_sendData.at(m_hoverHour);
        const QString text = QStringLiteral("%1 时   收 %2 · 发 %3")
                                 .arg(m_hoverHour, 2, 10, QLatin1Char('0'))
                                 .arg(recv)
                                 .arg(send);

        const double tipW = fm.horizontalAdvance(text) + 18.0;
        const double tipH = 22.0;
        double tx = m_plotRect.left() + (m_hoverHour + 0.5) * m_groupWidth - tipW / 2.0;
        tx = qBound(m_plotRect.left(), tx, m_plotRect.right() - tipW);
        const double ty = m_plotRect.top() + 2.0;

        p.setPen(Qt::NoPen);
        p.setBrush(kTipBg);
        p.drawRoundedRect(QRectF(tx, ty, tipW, tipH), 6.0, 6.0);
        p.setPen(Qt::white);
        p.drawText(QRectF(tx, ty, tipW, tipH), Qt::AlignCenter, text);
    }
}

int StatusChartWidget::hourAt(const QPointF &pos) const
{
    if (m_plotRect.isEmpty() || m_groupWidth <= 0.0 || !m_plotRect.contains(pos))
        return -1;
    const int hour = static_cast<int>((pos.x() - m_plotRect.left()) / m_groupWidth);
    return qBound(0, hour, 23);
}

void StatusChartWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int hour = hourAt(event->pos());
    if (hour != m_hoverHour) {
        m_hoverHour = hour;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void StatusChartWidget::leaveEvent(QEvent *event)
{
    if (m_hoverHour != -1) {
        m_hoverHour = -1;
        update();
    }
    QWidget::leaveEvent(event);
}
