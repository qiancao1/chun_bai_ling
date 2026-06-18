#ifndef AI_H
#define AI_H

#include <QWidget>

namespace Ui {
class Ai;
}

class Ai : public QWidget
{
    Q_OBJECT

public:
    explicit Ai(QWidget *parent = nullptr);
    ~Ai();

private:
    Ui::Ai *ui;
};

#endif // AI_H
