#ifndef FTLABEL_H
#define FTLABEL_H

#include <QImage>
#include <QWidget>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_TYPES_H
#include FT_OUTLINE_H
#include FT_RENDER_H

class ftlabel : public QWidget {
    Q_OBJECT
public:
    explicit ftlabel(QString font, int size, QWidget *parent = 0);
    virtual ~ftlabel();

protected:
    void paintEvent(QPaintEvent *event);

private:
    // fields:
    FT_Library m_library;
    FT_Face m_face;
    // methods:
    QImage createLcdGlyphImage(FT_UInt glyphIndex);
    QImage createNormalGlyphImage(FT_UInt glyphIndex);

signals:

public slots:

};

#endif // FTLABEL_H
