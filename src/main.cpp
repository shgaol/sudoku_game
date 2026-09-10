#include <QApplication>

#include "sudokuwidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    SudokuWidget widget;
    widget.show();

    return app.exec();
}
