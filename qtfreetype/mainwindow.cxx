#include "ftlabel.h"
#include "mainwindow.h"

#include <QtGui/QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    QPalette pltt = palette();
    pltt.setColor(QPalette::Window, Qt::white);
    pltt.setColor(QPalette::WindowText, Qt::black);
    setPalette(pltt);

    QWidget* widget = new QWidget(this);
    setCentralWidget(widget);

    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);

    layout->addWidget(new ftlabel(this));
}

MainWindow::~MainWindow() {
}
