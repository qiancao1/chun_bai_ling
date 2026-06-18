#include "ai.h"
#include "ui_ai.h"

Ai::Ai(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Ai)
{
    ui->setupUi(this);
}

Ai::~Ai()
{
    delete ui;
}
