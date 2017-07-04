#include "video.h"

#include <QPainter>
#include <QKeyEvent>

#include "StdAfx.h"
#include "Video.h"
#include "Memory.h"

namespace
{
    LPBYTE        g_pTextBank1; // Aux
    LPBYTE        g_pTextBank0; // Main
    LPBYTE        g_pHiresBank1;
    LPBYTE        g_pHiresBank0;

    #define  SW_80COL         (g_uVideoMode & VF_80COL)
    #define  SW_DHIRES        (g_uVideoMode & VF_DHIRES)
    #define  SW_HIRES         (g_uVideoMode & VF_HIRES)
    #define  SW_80STORE       (g_uVideoMode & VF_80STORE)
    #define  SW_MIXED         (g_uVideoMode & VF_MIXED)
    #define  SW_PAGE2         (g_uVideoMode & VF_PAGE2)
    #define  SW_TEXT          (g_uVideoMode & VF_TEXT)

//    bool g_bTextFlashState = false;

    void halfScanLines(QPixmap & charset)
    {
        const int height = charset.height();
        const int width = charset.width();

        const QColor background = charset.toImage().pixelColor(0, height - 1);

        QPainter paint(&charset);
        paint.setPen(background);

        for (int i = 0; i < height; i += 2)
        {
            paint.drawLine(0, i, width - 1, i);
        }
    }

    BYTE keyCode = 0;
    bool keyWaiting = false;

}

bool Video::Update40ColCell (QPainter & painter, int x, int y, int xpixel, int ypixel, int offset)
{
    Q_UNUSED(xpixel)
    Q_UNUSED(ypixel)

    BYTE ch = *(g_pTextBank0+offset);

    const int row = ch / 16;
    const int column = ch % 16;

    const int sx = 16 * column;
    const int sy = 16 * row;
    painter.drawPixmap(x * 14, y * 16, myCharset40, sx, sy, 14, 16);

    return true;
}

bool Video::Update80ColCell(QPainter & painter, int x, int y, int xpixel, int ypixel, int offset)
{
    Q_UNUSED(xpixel)
    Q_UNUSED(ypixel)

    {
        BYTE ch1 = *(g_pTextBank1+offset);

        const int row = ch1 / 16;
        const int column = ch1 % 16;

        const int sx = 8 * column;
        const int sy = 16 * row;
        painter.drawPixmap(x * 14, y * 16, myCharset80, sx, sy, 7, 16);
    }

    {
        BYTE ch2 = *(g_pTextBank0+offset);

        const int row = ch2 / 16;
        const int column = ch2 % 16;

        const int sx = 8 * column;
        const int sy = 16 * row;
        painter.drawPixmap(x * 14 + 7, y * 16, myCharset80, sx, sy, 7, 16);
    }

    return true;
}

bool Video::UpdateLoResCell(QPainter & painter, int x, int y, int xpixel, int ypixel, int offset)
{
    Q_UNUSED(painter)
    Q_UNUSED(x)
    Q_UNUSED(y)
    Q_UNUSED(xpixel)
    Q_UNUSED(ypixel)

    BYTE val = *(g_pTextBank0+offset);

    Q_UNUSED(val)

    return true;
}

bool Video::UpdateDLoResCell(QPainter & painter, int x, int y, int xpixel, int ypixel, int offset)
{
    Q_UNUSED(painter)
    Q_UNUSED(x)
    Q_UNUSED(y)
    Q_UNUSED(xpixel)
    Q_UNUSED(ypixel)
    Q_UNUSED(offset)

    return true;
}

bool Video::UpdateHiResCell(QPainter & painter, int x, int y, int xpixel, int ypixel, int offset)
{
    Q_UNUSED(painter)
    Q_UNUSED(x)
    Q_UNUSED(y)
    Q_UNUSED(xpixel)
    Q_UNUSED(ypixel)

    const BYTE * base = g_pHiresBank0 + offset;

    Q_UNUSED(base)

    return true;
}

bool Video::UpdateDHiResCell(QPainter & painter, int x, int y, int xpixel, int ypixel, int offset)
{
    Q_UNUSED(painter)
    Q_UNUSED(x)
    Q_UNUSED(y)
    Q_UNUSED(xpixel)
    Q_UNUSED(ypixel)
    Q_UNUSED(offset)

    return true;
}

Video::Video(QWidget *parent) : VIDEO_BASECLASS(parent)
{
    myCharset40.load(":/resources/CHARSET4.BMP");
    halfScanLines(myCharset40);

    myCharset80 = myCharset40.scaled(myCharset40.width() / 2, myCharset40.height());
}

void Video::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    const QSize min = minimumSize();
    const QSize actual = size();
    const double sx = double(actual.width()) / double(min.width());
    const double sy = double(actual.height()) / double(min.height());

    painter.scale(sx, sy);

    const int displaypage2 = (SW_PAGE2) == 0 ? 0 : 1;

    g_pHiresBank1 = MemGetAuxPtr (0x2000 << displaypage2);
    g_pHiresBank0 = MemGetMainPtr(0x2000 << displaypage2);
    g_pTextBank1  = MemGetAuxPtr (0x400  << displaypage2);
    g_pTextBank0  = MemGetMainPtr(0x400  << displaypage2);

    typedef bool (Video::*VideoUpdateFuncPtr_t)(QPainter&,int,int,int,int,int);

    VideoUpdateFuncPtr_t update = SW_TEXT
        ? SW_80COL
        ? &Video::Update80ColCell
        : &Video::Update40ColCell
        : SW_HIRES
        ? (SW_DHIRES && SW_80COL)
        ? &Video::UpdateDHiResCell
        : &Video::UpdateHiResCell
        : (SW_DHIRES && SW_80COL)
        ? &Video::UpdateDLoResCell
        : &Video::UpdateLoResCell;

    int  y        = 0;
    int  ypixel   = 0;
    while (y < 20)
    {
        int offset = ((y & 7) << 7) + ((y >> 3) * 40);
        int x      = 0;
        int xpixel = 0;
        while (x < 40)
        {
            (this->*update)(painter, x, y, xpixel, ypixel, offset + x);
            ++x;
            xpixel += 14;
        }
        ++y;
        ypixel += 16;
    }

    if (SW_MIXED)
        update = SW_80COL ? &Video::Update80ColCell : &Video::Update40ColCell;

    while (y < 24)
    {
        int offset = ((y & 7) << 7) + ((y >> 3) * 40);
        int x      = 0;
        int xpixel = 0;
        while (x < 40)
        {
            (this->*update)(painter, x, y, xpixel, ypixel, offset + x);
            ++x;
            xpixel += 14;
        }
        ++y;
        ypixel += 16;
    }
}

void Video::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();

    BYTE ch = 0;

    switch (key)
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        ch = 0x0d;
        break;
    case Qt::Key_Left:
        ch = 0x08;
        break;
    case Qt::Key_Right:
        ch = 0x15;
        break;
    case Qt::Key_Up:
        ch = 0x0b;
        break;
    case Qt::Key_Down:
        ch = 0x0a;
        break;
    case Qt::Key_Delete:
        ch = 0x7f;
        break;
    default:
        if (key < 0x80)
        {
            ch = key;
            if (ch >= 'A' && ch <= 'Z')
            {
                const Qt::KeyboardModifiers modifiers = event->modifiers();
                if (modifiers & Qt::ShiftModifier)
                {
                    ch += 'a' - 'A';
                }
            }
        }
        break;
    }

    if (ch)
    {
        keyCode = ch;
        keyWaiting = true;
    }
    else
    {
        VIDEO_BASECLASS::keyPressEvent(event);
    }
}

void Video::keyReleaseEvent(QKeyEvent *event)
{
    VIDEO_BASECLASS::keyReleaseEvent(event);
}

// Keyboard

BYTE    KeybGetKeycode ()
{
  return keyCode;
}

BYTE __stdcall KeybReadData (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft)
{
    Q_UNUSED(pc)
    Q_UNUSED(addr)
    Q_UNUSED(bWrite)
    Q_UNUSED(d)
    Q_UNUSED(nCyclesLeft)

    return keyCode | (keyWaiting ? 0x80 : 0x00);
}

BYTE __stdcall KeybReadFlag (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft)
{
    Q_UNUSED(pc)
    Q_UNUSED(addr)
    Q_UNUSED(bWrite)
    Q_UNUSED(d)
    Q_UNUSED(nCyclesLeft)

    keyWaiting = false;
    return keyCode;
}