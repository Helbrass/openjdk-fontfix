#include <QtCore/QDebug>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtGui/QApplication>
#include <QtGui/QMainWindow>
#include <QtGui/QVBoxLayout>
#include "ftlabel.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QString file("/usr/share/fonts/truetype/DejaVuSansMono.ttf");
    int size = 8;

    QTextStream out(stdout);
    out << "freetype2 font viewer demo.\n"
        << "By default shown font is "
        << file
        << " size "
        << size
        << ".\n"
        << "usage: --file <path to TTF file to read font from> --size <font size>\n";
    out.flush();

    QStringList args = a.arguments();
    // starting i is 1 because app name at position 0
    for (int i = 1; i < args.size(); i++) {
        if (args.at(i) == "--file") {
            i++;
            file = args.at(i);
            continue;
        }
        if (args.at(i) == "--size") {
            i++;
            size = args.at(i).toInt();
            continue;
        }
    }

    QMainWindow* mainWindow = new QMainWindow();

    QPalette pltt = mainWindow->palette();
    pltt.setColor(QPalette::Window, Qt::white);
    pltt.setColor(QPalette::WindowText, Qt::black);
    mainWindow->setPalette(pltt);

    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setMargin(0);
    layout->setSpacing(0);

    ftlabel* label = new ftlabel(file, size);
    label->setContentsMargins(0, 0, 0, 0);
    //layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addWidget(label);

    QWidget* widget = new QWidget(mainWindow);
    widget->setLayout(layout);
    mainWindow->setCentralWidget(widget);
    widget->setContentsMargins(20, 20, 20, 20);
    widget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    widget->adjustSize();
    widget->updateGeometry();

    pltt.setColor(QPalette::Window, Qt::gray);
    widget->setPalette(pltt);
    widget->setAutoFillBackground(true);

    mainWindow->setWindowTitle("fontview freetype2");
    mainWindow->move(0, 0);
    mainWindow->resize(widget->size());
    mainWindow->show();

    qDebug() << "window size: " << mainWindow->size();

    return a.exec();
}
