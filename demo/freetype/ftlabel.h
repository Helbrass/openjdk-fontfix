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

class RenderingProperties {
public:
    enum Weight {
        Weight_Regular,
        Weight_Bold
    };
    enum Slant {
        Slant_Roman,
        Slant_Italic
    };
    enum Subpixel {
        Subpixel_NONE,
        Subpixel_HRGB,
        Subpixel_HBGR,
        Subpixel_VRGB,
        Subpixel_VBGR
    };
    enum Hinting {
        Hinting_None,
        Hinting_Slight,
        Hinting_Medium,
        Hinting_Full
    };
    enum LCDFilter {
        LCDFilter_None,
        LCDFilter_Default,
        LCDFilter_Light,
        LCDFilter_Legacy
    };

    double pixelSize;
    Weight weight;
    Slant slant;
    QString fileName;
    bool antialias;
    Subpixel subpixel;
    Hinting hinting;
    bool autohint;
    LCDFilter lcdFilter;
    int dpi;
};

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
    RenderingProperties renderingProperties;
    // methods:
    QImage createLcdGlyphImage(FT_UInt glyphIndex);
    QImage createNormalGlyphImage(FT_UInt glyphIndex);

signals:

public slots:

};

#endif // FTLABEL_H
