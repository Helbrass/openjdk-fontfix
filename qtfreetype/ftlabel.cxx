#include "ftlabel.h"

#include FT_LCD_FILTER_H

#include <QtCore/QDebug>
#include <QtGui/QPainter>

ftlabel::ftlabel(QWidget *parent) : QWidget(parent) {
    int error = FT_Init_FreeType( &m_library );
    if (error) {
        qDebug() << "FT_Init error: " << error;
        return;
    }
    error = FT_New_Face( m_library, "/usr/share/fonts/truetype/DejaVuSansMono.ttf", 0, &m_face);
    if (error) {
        qDebug() << "FT_New_Face error: " << error;
        return;
    }
    int dpyx = physicalDpiX();
    int dpyy = physicalDpiY();
    qDebug() << "Physical dpy: " << dpyx << " x " << dpyy;
    error = FT_Set_Char_Size( m_face, 0, 10*64, dpyx, dpyy);
    if (error) {
        qDebug() << "FT_Set_Char_Size error: " << error;
        return;
    }
}

ftlabel::~ftlabel() {
    int error = FT_Done_FreeType( m_library );
    if (error) {
        qDebug() << "FT_Done error: " << error;
    }
}

#define LCD_RENDERING (true)

void ftlabel::paintEvent(QPaintEvent *event) {

#if(LCD_RENDERING)
    qDebug() << "LCD Rendering";
#else
    qDebug() << "Default Rendering";
#endif

    QWidget::paintEvent(event);

    QPainter painter(this);

    QString text = "The quick brown fox jumps over the lazy dog";
    //QString text = "l";
    int x = 30;
    int y = 30;
    int error = 0;

    for (int i = 0; i < text.size(); i++) {

        FT_UInt glyph_index = FT_Get_Char_Index( m_face, text[i].toAscii() );

        QImage* glyphImage;

#if(LCD_RENDERING)
        {
            error = FT_Load_Glyph( m_face, glyph_index, FT_LOAD_TARGET_LCD);
            if (error) {
                qDebug() << "FT_Load_Glyph error: " << error;
                return;
            }

            error = FT_Library_SetLcdFilter(m_library, FT_LCD_FILTER_DEFAULT);
            if (error) {
                qDebug() << "FT_Library_SetLcdFilter error: " << error;
                return;
            }

            error = FT_Render_Glyph( m_face->glyph, FT_RENDER_MODE_LCD );
            if (error) {
                qDebug() << "FT_Render_Glyph error: " << error;
                return;
            }

            int _h = m_face->glyph->bitmap.rows;
            int _w = m_face->glyph->bitmap.width/3;
            int _p = m_face->glyph->bitmap.pitch;

            uchar *src = m_face->glyph->bitmap.buffer;
            glyphImage = new QImage(_w, _h, QImage::Format_ARGB32_Premultiplied);
            glyphImage->fill(qRgba(0, 0, 0, 0xff));

            int bit = 0; // address in buffer
            for (int y = 0; y < _h; y++) {

                for (int x = 0; x < _w; x++) {

                    uchar r = src[bit++];
                    uchar g = src[bit++];
                    uchar b = src[bit++];

                    r = 0xff + ( -0xff * r / 255 );
                    g = 0xff + ( -0xff * g / 255 );
                    b = 0xff + ( -0xff * b / 255 );

                    glyphImage->setPixel(x, y, qRgb(r, g, b));

                }

                bit = _p * (y + 1); // go to next line, start from 0
            }
        }
#else
        {
            error = FT_Load_Glyph( m_face, glyph_index, FT_LOAD_DEFAULT );
            if (error) {
                qDebug() << "FT_Load_Glyph error: " << error;
                continue;
            }

            error = FT_Render_Glyph( m_face->glyph, FT_RENDER_MODE_NORMAL );
            if (error) {
                qDebug() << "FT_Render_Glyph error: " << error;
                continue;
            }

            glyphImage = new QImage(m_face->glyph->bitmap.buffer,
                              m_face->glyph->bitmap.width, // 27-7
                              m_face->glyph->bitmap.rows, // 9-9
                              m_face->glyph->bitmap.pitch, // 28-7
                              QImage::Format_Indexed8);

            QVector<QRgb> colorTable;
            for (int i = 0; i < 256; ++i) {
                colorTable << qRgba(0, 0, 0, i);
            }
            glyphImage->setColorTable(colorTable);
        }
#endif

        painter.drawImage(x + m_face->glyph->bitmap_left, y - m_face->glyph->bitmap_top, *glyphImage);

        x += m_face->glyph->advance.x >> 6;

    }
}
