#include "ftlabel.h"

#include <QtCore/QDebug>
#include <QtGui/QPainter>
#include <QX11Info>

#include FT_LCD_FILTER_H

#include <fontconfig/fontconfig.h>

#include <X11/Xlib.h>

int dpiY() {
    // display variable should come from XOpenDisplay invocation:
    Display* display = QX11Info::display();
    int hmm = DisplayHeightMM(display, 0);
    int h = DisplayHeight(display, 0);
    // basic formula is:
    // 25.4 * h / hmm + 0.5
    return (h * 254 + hmm * 5) / (hmm * 10);
}

FcPattern* matchedPattern(const FcChar8* family) {
    /*
      we will create pattern to find our family and size in
      fontconfig configuration, and then will return it's
      properties:
    */
    FcPattern* fcPattern = 0;
    fcPattern = FcPatternCreate();
    FcValue fcValue;
    fcValue.type = FcTypeString;
    fcValue.u.s = family;
    FcPatternAdd(fcPattern, FC_FAMILY, fcValue, FcTrue);
    FcPatternAddBool(fcPattern, FC_SCALABLE, FcTrue);
    // TODO FcPatternAddInteger(pattern, FC_WEIGHT, weight_value);
    // TODO FcPatternAddInteger(pattern, FC_SLANT, slant_value);
    // TODO FcPatternAddDouble(pattern, FC_PIXEL_SIZE, size_value);
    // TODO FcPatternAddInteger(pattern, FC_WIDTH, stretch); 100 in most cases
    FcConfigSubstitute(0, fcPattern, FcMatchPattern);
    FcConfigSubstitute(0, fcPattern, FcMatchFont);
    FcDefaultSubstitute(fcPattern);
    FcResult res;

    FcPattern *pattern = 0;
    pattern = FcFontMatch(0, fcPattern, &res);
    FcPatternDestroy(fcPattern);
    return pattern;
}

void readFontconfig(FT_Face ftFace, RenderingProperties &rp) {
    FcPattern *pattern = matchedPattern((const FcChar8 *)ftFace->family_name);

    double pixelSize;
    FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &pixelSize);
    qDebug() << "pixel size: " << pixelSize;
    rp.pixelSize = pixelSize;

    int weight;
    FcPatternGetInteger(pattern, FC_WEIGHT, 0, &weight);
    qDebug() << "weight: " << weight;
    switch (weight) {
    case FC_WEIGHT_BOLD:
        rp.weight = RenderingProperties::Weight_Bold;
        break;
    default:
        rp.weight = RenderingProperties::Weight_Regular;
        break;
    }

    int slant;
    FcPatternGetInteger(pattern, FC_SLANT, 0, &slant);
    qDebug() << "slant: " << slant;
    switch (slant) {
    case FC_SLANT_ITALIC:
    case FC_SLANT_OBLIQUE:
        rp.slant = RenderingProperties::Slant_Italic;
        break;
    default:
        rp.slant = RenderingProperties::Slant_Roman;
        break;
    }

    QString file_name;
    FcChar8 *fileName;
    FcPatternGetString(pattern, FC_FILE, 0, &fileName);
    file_name = (const char *)fileName;
    qDebug() << "file name: " << file_name;
    rp.fileName = file_name;

    bool antialias;
    FcBool b;
    if (FcPatternGetBool(pattern, FC_ANTIALIAS, 0, &b) == FcResultMatch)
        antialias = b;
    qDebug() << "antialias: " << antialias;
    rp.antialias = antialias;

    if (antialias) {
        int subpixel = FC_RGBA_UNKNOWN;
        FcPatternGetInteger(pattern, FC_RGBA, 0, &subpixel);
        if (!antialias || subpixel == FC_RGBA_UNKNOWN)
            subpixel = FC_RGBA_NONE;
        switch (subpixel) {
        case FC_RGBA_NONE:
            rp.subpixel = RenderingProperties::Subpixel_NONE;
            break;
        case FC_RGBA_RGB:
            rp.subpixel = RenderingProperties::Subpixel_HRGB;
            break;
        case FC_RGBA_BGR:
            rp.subpixel = RenderingProperties::Subpixel_HBGR;
            break;
        case FC_RGBA_VRGB:
            rp.subpixel = RenderingProperties::Subpixel_VRGB;
            break;
        case FC_RGBA_VBGR:
            rp.subpixel = RenderingProperties::Subpixel_VBGR;
            break;
        default: break;
        }
    }

    int hint_style = 0;
    FcPatternGetInteger (pattern, FC_HINT_STYLE, 0, &hint_style);
    qDebug() << "hint style: " << hint_style;
    switch (hint_style) {
    case FC_HINT_NONE:
        rp.hinting = RenderingProperties::Hinting_None;
        break;
    case FC_HINT_SLIGHT:
        rp.hinting = RenderingProperties::Hinting_Slight;
        break;
    case FC_HINT_MEDIUM:
        rp.hinting = RenderingProperties::Hinting_Medium;
        break;
    case FC_HINT_FULL:
        rp.hinting = RenderingProperties::Hinting_Full;
        break;
    default:
        rp.hinting = RenderingProperties::Hinting_Medium;
        break;
    }

    bool autohint = false;
    if (FcPatternGetBool(pattern, FC_AUTOHINT, 0, &b) == FcResultMatch)
        autohint = b;
    qDebug() << "autohint: " << autohint;
    rp.autohint = autohint;

    int filter = FC_LCD_NONE;
    if (FcPatternGetInteger(pattern, FC_LCD_FILTER, 0, &filter) == FcResultMatch) {
        qDebug() << "lcd filter: " << filter;
        switch (filter) {
        case FC_LCD_NONE:
            rp.lcdFilter = RenderingProperties::LCDFilter_None;
            break;
        case FC_LCD_DEFAULT:
            rp.lcdFilter = RenderingProperties::LCDFilter_Default;
            break;
        case FC_LCD_LIGHT:
            rp.lcdFilter = RenderingProperties::LCDFilter_Light;
            break;
        case FC_LCD_LEGACY:
            rp.lcdFilter = RenderingProperties::LCDFilter_Legacy;
            break;
        default:
            // new unknown lcd filter type?!
            break;
        }
    }

    FcPatternDestroy(pattern);

}

RenderingProperties loadRenderingProperties(FT_Face face) {
    RenderingProperties rp;

    readFontconfig(face, rp);
    int dpi = dpiY();
    rp.dpi = dpi;

    return rp;
}

void ftlabel::paintEvent(QPaintEvent *event) {

    QWidget::paintEvent(event);

    QPainter painter(this);

    //QString text = "The quick brown fox jumps over the lazy dog";
    // the most problematic combination:
    QString text = "wmp";
    // coordinates where we will start painting our text string:
    int x = 0;
    int y = m_face->size->metrics.ascender / 64;

    // one letter at a time:
    for (int i = 0; i < text.size(); i++) {

        qDebug() << "creating image for: " << text[i];
        FT_UInt glyph_index = FT_Get_Char_Index( m_face, text[i].toAscii() );

        QImage glyphImage = renderingProperties.subpixel != RenderingProperties::Subpixel_NONE
                ? createLcdGlyphImage(glyph_index)
                : createNormalGlyphImage(glyph_index);

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
        qDebug("x increment: %i, left: %i", inc, left);
        x += inc;

    }
}

QImage ftlabel::createLcdGlyphImage(FT_UInt glyphIndex) {
    // reusable freetype error variable:
    int error = 0;

    int ftLoad = FT_LOAD_DEFAULT;
    FT_Render_Mode ftRender = FT_RENDER_MODE_NORMAL;

    bool horizontal = true;
    switch (renderingProperties.subpixel) {
    case RenderingProperties::Subpixel_NONE:
        ftRender = FT_RENDER_MODE_NORMAL;
        break;
    case RenderingProperties::Subpixel_VBGR:
    case RenderingProperties::Subpixel_VRGB:
        ftRender = FT_RENDER_MODE_LCD_V;
        horizontal = false;
        break;
    case RenderingProperties::Subpixel_HBGR:
    case RenderingProperties::Subpixel_HRGB:
    default:
        ftRender = FT_RENDER_MODE_LCD;
        horizontal = true;
        break;
    }

    switch (renderingProperties.hinting) {
    case RenderingProperties::Hinting_Slight:
        ftLoad |= FT_LOAD_TARGET_LIGHT;
        break;
    case RenderingProperties::Hinting_Full:
        ftLoad |= horizontal ? FT_LOAD_TARGET_LCD : FT_LOAD_TARGET_LCD_V;
        break;
    case RenderingProperties::Hinting_None:
        ftLoad |= FT_LOAD_NO_HINTING;
        break;
    default:
        ftLoad |= FT_LOAD_TARGET_NORMAL;
        break;
    }

    if (renderingProperties.autohint)
        ftLoad |= FT_LOAD_FORCE_AUTOHINT;
    // not sure why do we need it, but Qt is using it:
    ftLoad |= FT_LOAD_NO_BITMAP;

    //qDebug("FT_Load_Glyph: %X", ftLoad);
    error = FT_Load_Glyph( m_face, glyphIndex, ftLoad);
    if (error) {
        qDebug() << "FT_Load_Glyph error: " << error;
        return QImage();
    }

    // in LCD mode additional filtering is required:
    FT_LcdFilter ftLcdFilter = FT_LCD_FILTER_NONE;
    switch (renderingProperties.lcdFilter) {
    case RenderingProperties::LCDFilter_None:
        //qDebug() << "FT_LCD_FILTER_NONE";
        ftLcdFilter = FT_LCD_FILTER_NONE;
        break;
    case RenderingProperties::LCDFilter_Light:
        //qDebug() << "FT_LCD_FILTER_LIGHT";
        ftLcdFilter = FT_LCD_FILTER_LIGHT;
        break;
    case RenderingProperties::LCDFilter_Legacy:
        //qDebug() << "FT_LCD_FILTER_LEGACY";
        ftLcdFilter = FT_LCD_FILTER_LEGACY;
        break;
    case RenderingProperties::LCDFilter_Default:
    default:
        //qDebug() << "FT_LCD_FILTER_DEFAULT";
        ftLcdFilter = FT_LCD_FILTER_DEFAULT;
        break;
    }

    error = FT_Library_SetLcdFilter(m_library, ftLcdFilter);
    if (error) {
        qDebug() << "FT_Library_SetLcdFilter error: " << error;
        return QImage();
    }

    //qDebug("FT_Render_Glyph: %X", ftRender);
    error = FT_Render_Glyph( m_face->glyph, ftRender );
    if (error) {
        qDebug() << "FT_Render_Glyph error: " << error;
        return QImage();
    }

    // height and width of bitmap:
    int _h = m_face->glyph->bitmap.rows;
    // in LCD mode bitmap is 3 times wider, because of R, G and B channels,
    // dividing /3 to get target image width:
    int _w = m_face->glyph->bitmap.width/3;
    qDebug("image width: %i", _w);

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

            uchar srcR = src[bit++];
            uchar srcG = src[bit++];
            uchar srcB = src[bit++];

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
            int r = 0xff + ( -0xff * srcR / 255 );
            int g = 0xff + ( -0xff * srcG / 255 );
            int b = 0xff + ( -0xff * srcB / 255 );

            qDebug("R-G-B: %x-%x-%x", r, g, b);

            glyphImage.setPixel(x, y, qRgba(r, g, b, g == 0xff ? 0 : 0xff));

        }

        // move bitmap pointer to next line:
        bit = _p * (y + 1);
    }
    return glyphImage;
}

QImage ftlabel::createNormalGlyphImage(FT_UInt glyphIndex) {
    qDebug() << "creating normal glyph image";

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

    error = FT_New_Face( m_library, font.toLatin1(), 0, &m_face);
    if (error) {
        qDebug() << "FT_New_Face error: " << error;
        return;
    }

    renderingProperties = loadRenderingProperties(m_face);

    // freetype2 is using 1/64 of point for char size, so 10 points will be 10*64:
    error = FT_Set_Char_Size( m_face, 0, size*64, 0, renderingProperties.dpi);
    if (error) {
        qDebug() << "FT_Set_Char_Size error: " << error;
        return;
    }

    int height = 0;
    {
        FT_Size_Metrics metrics = m_face->size->metrics;
        int ascender = metrics.ascender;
        int descender = metrics.descender;
        // it should be asc + desc, but descender is always negative:
        height = (ascender - descender) / 64;
    }
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
