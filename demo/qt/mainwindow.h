#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtGui/QLabel>
#include <QtGui/QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    QLabel* createLabel(const QString& fontFamily, const int fontSize);
};

#endif // MAINWINDOW_H
