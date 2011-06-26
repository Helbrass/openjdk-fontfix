#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtGui/QApplication>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QLabel>
#include <QtGui/QMainWindow>
#include <QtGui/QVBoxLayout>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    QString file("/usr/share/fonts/truetype/DejaVuSansMono.ttf");
    int size = 10;

    QTextStream out(stdout);
    out << "Qt font viewer demo.\n"
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

    int fontId = QFontDatabase::addApplicationFont(file);
    QString family = QFontDatabase::applicationFontFamilies(fontId).at(0);

    QFont font(family, size);
    QLabel* label = new QLabel("The quick brown fox jumps over the lazy dog");
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, Qt::black);
    label->setPalette(p);
    label->setFont(font);
    label->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(label, 0, Qt::AlignCenter);

    QWidget* centralWidget = new QWidget();
    centralWidget->setContentsMargins(20, 20, 20, 20);
    centralWidget->setLayout(layout);
    centralWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    centralWidget->adjustSize();
    centralWidget->updateGeometry();

    QMainWindow* w = new QMainWindow();
    p = w->palette();
    p.setColor(QPalette::Window, Qt::white);
    w->setPalette(p);
    w->setWindowTitle("fontview qt");
    w->setCentralWidget(centralWidget);
    w->move(0, 0);
    w->resize(centralWidget->size());
    w->show();

    return a.exec();
}
