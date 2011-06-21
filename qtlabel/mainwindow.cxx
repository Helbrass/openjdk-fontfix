#include "mainwindow.h"

#include <QtGui/QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    QPalette pltt = palette();
    pltt.setColor(QPalette::Window, Qt::white);
    setPalette(pltt);

    QWidget* widget = new QWidget(this);
    setCentralWidget(widget);

    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(createLabel("DejaVu Sans Mono", 10));
    layout->addWidget(createLabel("Consolas", 10));
    layout->addSpacing(20);
    layout->addWidget(createLabel("DejaVu Sans Mono", 12));
    layout->addWidget(createLabel("Consolas", 12));
    layout->addSpacing(20);
    layout->addWidget(createLabel("Monospace", 0));
    layout->addWidget(createLabel("SansSerif", 0));
    layout->addWidget(createLabel("Serif", 0));
}

MainWindow::~MainWindow() {
}

QLabel* MainWindow::createLabel(const QString &fontFamily, const int fontSize) {

    QString text("The quick brown fox jumps over the lazy dog (");
    text+=fontFamily;
    if (fontSize > 0) {
        text+=" ";
        text+=QString::number(fontSize);
    }
    text+=")";

    QLabel* label = new QLabel(text, this);
    QFont fnt = label->font();
    fnt.setFamily(fontFamily);
    if (fontSize > 0) {
        fnt.setPointSize(fontSize);
    }
    label->setFont(fnt);
    return label;
}
