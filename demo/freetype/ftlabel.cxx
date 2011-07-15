#include "ftlabel.h"

#include FT_LCD_FILTER_H

#include <QtCore/QDebug>
#include <QtGui/QPainter>

static bool LCD_RENDERING = true;

void ftlabel::paintEvent(QPaintEvent *event) {

    qDebug() << (LCD_RENDERING ? "LCD Rendering" : "Default Rendering");

    QWidget::paintEvent(event);

    QPainter painter(this);

    QString text = "The quick brown fox jumps over the lazy dog";
    // coordinates where we will start painting our text string:
    int x = 0;
    //int y = m_face->ascender / 64;
    int y = 10;

    // one letter at a time:
    for (int i = 0; i < text.size(); i++) {

        FT_UInt glyph_index = FT_Get_Char_Index( m_face, text[i].toAscii() );

        QImage glyphImage = LCD_RENDERING ? createLcdGlyphImage(glyph_index) : createNormalGlyphImage(glyph_index);

        /*
          Fonts and glyphs as I udnerstood them:
          - There is a font and there is a glyph. Font represents full font family, has it's size, it's
            imaginary borders. Glyph is image of concrete character from font. There is a padding
            between glyph boundary box and font borders. That padding is number of pixels from "current
            pen position" to glyph boundary bor. We can get its value from m_face->glyph->bitmap_left.
          - Step from one font character to next is "advance". In freetype2 tutorials it's used with
            shifting: m_face->glyph->advance.x >> 6.
          - We will draw image on coordinates X + padding, then we will increment X with font advance value,
            and it will be prepared to draw next image.
         */
        int left = m_face->glyph->bitmap_left;
        int top = m_face->glyph->metrics.horiBearingY / 64;
        painter.drawImage(x + left, y - top, glyphImage);
        int inc = ((m_face->glyph->advance.x + 32) & -64) >> 6;
        x += inc;

    }
}

QImage ftlabel::createLcdGlyphImage(FT_UInt glyphIndex) {
    // reusable freetype error variable:
    int error = 0;

    // DejaVu font is visually similar with autohinter and without, but Consolas is crippled with autohinter,
    // so we will load without autohinting:
    error = FT_Load_Glyph( m_face, glyphIndex, FT_LOAD_TARGET_LCD);
    if (error) {
        qDebug() << "FT_Load_Glyph error: " << error;
        return QImage();
    }

    // in LCD mode additional filtering is required:
    error = FT_Library_SetLcdFilter(m_library, FT_LCD_FILTER_DEFAULT);
    if (error) {
        qDebug() << "FT_Library_SetLcdFilter error: " << error;
        return QImage();
    }

    error = FT_Render_Glyph( m_face->glyph, FT_RENDER_MODE_LCD );
    if (error) {
        qDebug() << "FT_Render_Glyph error: " << error;
        return QImage();
    }

    // height and width of bitmap:
    int _h = m_face->glyph->bitmap.rows;
    // in LCD mode bitmap is 3 times wider, because of R, G and B channels,
    // dividing /3 to get target image width:
    int _w = m_face->glyph->bitmap.width/3;

    // bytes per bitmap row:
    int _p = m_face->glyph->bitmap.pitch;

    // I have absolutely no idea what's the difference between ARGB32 and ARGB32_Premultiplied:
    QImage glyphImage(_w, _h, QImage::Format_ARGB32_Premultiplied);
    // filling image will absolutely transparent black:
    glyphImage.fill(qRgba(0, 0, 0, 0xff));

    /*
      NOTE: this is not optimized version of painting. It was simplified for demo purposes.
            1) Optimized version should create full image buffer in memory and just pass it
               to QImage constructor.
            2) Pointer to bitmap buffer may be incremented instead of keeping separate "bit"
               positioning integer.
            3) Difference between m_face->glyph->bitmap.width and m_face->glyph->bitmap.pitch
               is constant, it can be calculated once and used as increment to src pointer after
               each row is processed.
            4) Color value calculation can be skipped and background color value used in case when
               bitmap value is 0.
     */
    uchar *src = m_face->glyph->bitmap.buffer;
    // position inside src[] buffer:
    int bit = 0;
    for (int y = 0; y < _h; y++) {

        for (int x = 0; x < _w; x++) {

            uchar r = src[bit++];
            uchar g = src[bit++];
            uchar b = src[bit++];

            /*
              formula for every color goes like this:
              color = background + ( ( foreground - background ) * source / 255 )
              where:
              color        final value of color for image.
              background   color of panel or surface for text painting.
              foreground   color of text.
              source       transparency level from freetype2 rendered bitmap. It's
                           value divided by 255 to simulate transparency double.

              In our current case background is constant 0xff, foreground is constant 0,
              so we can hardcode required values for this demo.

              We have to perform this operation for all 3 colors we are getting from bitmap.
             */
            r = 0xff + ( -0xff * r / 255 );
            g = 0xff + ( -0xff * g / 255 );
            b = 0xff + ( -0xff * b / 255 );

            glyphImage.setPixel(x, y, qRgb(r, g, b));

        }

        // move bitmap pointer to next line:
        bit = _p * (y + 1);
    }
    return glyphImage;
}

QImage ftlabel::createNormalGlyphImage(FT_UInt glyphIndex) {
    // reusable freetype error variable:
    int error = 0;

    error = FT_Load_Glyph( m_face, glyphIndex, FT_LOAD_DEFAULT);
    if (error) {
        qDebug() << "FT_Load_Glyph error: " << error;
        return QImage();
    }

    error = FT_Render_Glyph( m_face->glyph, FT_RENDER_MODE_NORMAL );
    if (error) {
        qDebug() << "FT_Render_Glyph error: " << error;
        return QImage();
    }

    // direct values from freetype2 may be used in case of monochrome rendering:
    QImage glyphImage(m_face->glyph->bitmap.buffer, // uchar buffer of bits
                      m_face->glyph->bitmap.width, // width of image
                      m_face->glyph->bitmap.rows, // height of image
                      m_face->glyph->bitmap.pitch, // bytes per line
                      QImage::Format_Indexed8); // image format

    // feature of QImage, setting foreground to black for any level of transparency:
    QVector<QRgb> colorTable;
    for (int i = 0; i < 256; ++i) {
        colorTable << qRgba(0, 0, 0, i);
    }
    glyphImage.setColorTable(colorTable);
    return glyphImage;
}

ftlabel::ftlabel(QString font, int size, QWidget *parent) : QWidget(parent) {
    int error = FT_Init_FreeType( &m_library );
    if (error) {
        qDebug() << "FT_Init error: " << error;
        return;
    }
    //error = FT_New_Face( m_library, "/usr/share/fonts/truetype/DejaVuSansMono.ttf", 0, &m_face);
    //error = FT_New_Face( m_library, "/usr/local/share/fonts/c/CONSOLA.ttf", 0, &m_face);
    error = FT_New_Face( m_library, font.toLatin1(), 0, &m_face);
    if (error) {
        qDebug() << "FT_New_Face error: " << error;
        return;
    }

    // physical DPI calculated by Qt itself, it's using X.org API to get physical screen dimension and resolution:
    int dpyx = physicalDpiX();
    int dpyy = physicalDpiY();
    qDebug() << "Physical dpy: " << dpyx << " x " << dpyy;

    // freetype2 is using 1/64 of point for char size, so 10 points will be 10*64:
    error = FT_Set_Char_Size( m_face, 0, size*64, dpyx, dpyy);
    if (error) {
        qDebug() << "FT_Set_Char_Size error: " << error;
        return;
    }

    int height = m_face->ascender/72;
    int width = 0;
    QString text = "The quick brown fox jumps over the lazy dog";
    for (int i = 0; i < text.size(); i++) {
        FT_UInt glyph_index = FT_Get_Char_Index( m_face, text[i].toAscii() );
        FT_Load_Glyph( m_face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
        int inc = ((m_face->glyph->advance.x + 32) & -64) >> 6;
        width += inc;
    }

    QSize s(width, height);
    resize(s);
    setMinimumSize(s);
    setMaximumSize(s);

    QPalette pltt = palette();
    pltt.setColor(QPalette::Window, Qt::white);
    setPalette(pltt);
    setAutoFillBackground(true);
}

ftlabel::~ftlabel() {
    int error = FT_Done_FreeType( m_library );
    if (error) {
        qDebug() << "FT_Done error: " << error;
    }
}
