/*----------------------------------------------------------------------------
 * louis: braille graphics library
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Copyright 2020 Sean Tommasi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * louis.h
 *
 * This header file contains routines for drawing points, lines, rectangles,
 * curves, and bitmaps to a VT100 terminal using only Unicode braille
 * characters.
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static struct termios origterm, rawterm;
static int brailleTab[256];
unsigned char *screenBuffer;

/* This array contains the values of the bit positions in a single braille
 * character. The UCS character code is composed of bit values that represent
 * the dots as follows:
 *
 *     1   8
 *     2   16
 *     4   32
 *     64  128
 *
 * This arrangement intends the dots to be considered first in the left column
 * and then the next. The bottom two dots were added historically later. I have
 * arranged the values in the array to instead be processed from left to right,
 * since we want the dots to conform to an X, Y grid in our system. I have also
 * flipped the numbers upside down, because I want the Y axis to grow upward as
 * in normal geometric representations, rather than downward as in normal
 * computer screen operations. */
static int braillePositionVals[8] = {64, 128, 4, 32, 2, 16, 1, 8};

typedef struct Surface {
    unsigned char *data;
    int width;
    int height;
} Surface;

/*----------------------------------------------------------------------------
 * power, fAbs, fRound, littleToBigEndian
 *
 * Miscellaneous utility functions.
 *----------------------------------------------------------------------------*/
static float fAbs(float a, float b)
{
    return (a > b) ? (a - b) : (b - a);
}

static int fRound(float n)
{
    return (int)(n + 0.5);
}

static int littleToBigEndian(unsigned char *src)
{
    return *src | *(src + 1) << 8 | *(src + 2) << 16 | *(src + 3) << 24;
}

/*----------------------------------------------------------------------------
 * utf8Encode
 *
 * Take a UCS code number in the range of 0x800 - 0xFFFF and return the
 * correct encoded byte sequence. See the utf-8 man page for details.
 *----------------------------------------------------------------------------*/
static unsigned int utf8Encode(int utf8)
{
    /* Encoding: 1110xxxx 10xxxxxx 10xxxxxx */
    unsigned char enc1 = 0xE0 | ((utf8 & 0xF000) >> 12);
    unsigned char enc2 = 0x80 | ((utf8 & 0x0FC0) >> 6);
    unsigned char enc3 = 0x80 | (utf8 & 0x003F);

    /* Pack into an int, little-endian */
    return (enc3 << 16) | (enc2 << 8) | enc1;
}

/*----------------------------------------------------------------------------
 * genBrailleTab
 *
 * Rather than generating an ASCII escape sequence every time a braille
 * character is needed, this function generates all 256 of them and stores them
 * in an array for fast retrieval. The UCS for each braille is 0x2800 plus the
 * index of the array.
 *----------------------------------------------------------------------------*/
static void genBrailleTab()
{
    /* The Unicode braille characters are in the UCS range of 0x2800 - 0x28FF */
    for (int i = 0x00; i <= 0xFF; ++i) {
        brailleTab[i] = utf8Encode(0x2800 + i);
    }
}

/*----------------------------------------------------------------------------
 * drawPoint
 *
 * Take x and y coordinates to draw an individual point abstracted from the
 * braille character it's actually part of. Append to the screen buffer the
 * character that contains that point plus any others already drawn around it.
 * The value parameter is set to 1 to place a point and 0 to place no point or
 * erase one already there.
 *----------------------------------------------------------------------------*/
int drawPoint(Surface *s, float fx, float fy, int value)
{
    int x = fRound(fx);
    int y = fRound(fy);
    if (x < 0 || y < 0 || x >= s->width * 2 || y >= s->height * 4)
        return -1;

    /* Derive character position on screen from point position */
    unsigned char *p = s->data + (s->width * s->height) - s->width - ((y / 4) * s->width) + (x / 2);

    /* Bit value of dot within braille cell corresponding to x, y position */
    unsigned char positionVal = braillePositionVals[((y % 4) * 2) + (x % 2)];

    /* Either turn off or turn on the bit */
    if (!value) {
        *p &= ~positionVal;
    } else {
        *p |= positionVal;
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * drawLine
 *
 * Take two points and draw a line segment.
 *----------------------------------------------------------------------------*/
void drawLine(Surface *s, float x1, float y1, float x2, float y2)
{
    float slope, yint;
    int inf = 0;

    /* Account for the undefined slope */
    if (x1 == x2) {
        inf = 1;
    } else {
        slope = (y2 - y1) / (x2 - x1);
        yint = y1 - (slope * x1);
    }

    drawPoint(s, x1, y1, 1);
    while (fAbs(x1, x2) > 1 || fAbs(y1, y2) > 1) {
        /* An undefined slope is just a vertical line */
        if (inf) {
            y1 += (y1 < y2) ? 1.0f : -1.0f;
        /* A slight incline needs more X-axis points to fill out the line */
        } else if (slope < 1) {
            x1 += (x1 < x2) ? 1.0f : -1.0f;
            y1 = x1 * slope + yint;
        /* A steeper slope needs more Y-axis points */
        } else {
            y1 += (y1 < y2) ? 1.0f : -1.0f;
            x1 = (y1 - yint) / slope;
        }
        drawPoint(s, x1, y1, 1);
    }
}

/*----------------------------------------------------------------------------
 * drawCurve
 *
 * Take a starting point on the X axis and the three coefficients of a
 * quadratic equation to determine the curve, slope, and y-intercept. Draw the
 * resulting curve.
 *----------------------------------------------------------------------------*/
void drawCurve(Surface *s, float x1, float x2, float a, float b, float c)
{
    int y;
    while (x1 < x2) {
        x1 += 0.2f;

        /* Quadratic equation */
        y = (a * (x1 * x1)) + (b * x1) + c;

        /* Exaggerate the X axis */
        y /= 10;

        drawPoint(s, x1, y, 1);
    }
}

/*----------------------------------------------------------------------------
 * drawRect
 *
 * Take x and y coordinates and draw a w by h rectangle, filled or unfilled,
 * using the drawPoint function.
 *----------------------------------------------------------------------------*/
void drawRect(Surface *s, int x, int y, int w, int h, int fill)
{
    if (fill) {
        for (int i = 0; i < h; ++i) {
            for (int j = 0; j < w; ++j) {
                drawPoint(s, x + j, y + i, 1);
            }
        }
    } else {
        for (int i = 0; i < w; ++i) {
            drawPoint(s, x + i, y, 1);
            drawPoint(s, x + i, y + h - 1, 1);
        }
        for (int i = 1; i < h - 1; ++i) {
            drawPoint(s, x, y + i, 1);
            drawPoint(s, x + w - 1, y + i, 1);
        }
    }
}

/*----------------------------------------------------------------------------
 * drawBitmap
 *
 * Draw a bitmap, an array of ones and zeros representing pixels, to the
 * screen at position x, y, using drawPoint.
 *----------------------------------------------------------------------------*/
void drawBitmap(Surface *s, Surface *bitmap, int x, int y)
{
    unsigned char *p = bitmap->data;

    /* Even though the Y origin of our coordinate system is at the bottom of
     * the screen, BMP data is already "upside down", so we can read the data
     * in the usual top to bottom way. */
    for (int i = 0; i < bitmap->height; ++i) {
        for (int j = 0; j < bitmap->width; ++j) {
            drawPoint(s, x + j, y + i, *p++);
        }
    }
}


/*----------------------------------------------------------------------------
 * render
 *
 * Convert the lower-byte-UCS values in the data buffer to their UTF-8 encoded
 * values, storing them in another buffer, and write that buffer to STDOUT.
 *----------------------------------------------------------------------------*/
void render(Surface *s)
{
    if (!screenBuffer)
        screenBuffer = (unsigned char *)malloc((s->width * s->height * 3) + 12);

    int len = s->width * s->height;
    unsigned char *p = screenBuffer;
    memcpy(p, "\x1b[?25l", 6);
    p += 6;
    memcpy(p, "\x1b[H", 3);
    p += 3;
    for (int i = 0; i < len; ++i) {
        memcpy(p, brailleTab + *(s->data + i), 3);
        p += 3;
    }
    memcpy(p, "\x1b[H", 3);

    write(1, screenBuffer, (len * 3) + 12);
}

/*----------------------------------------------------------------------------
 * clearSurface
 *
 * Set the whole block of Surface data to 0.
 *----------------------------------------------------------------------------*/
void clearSurface(Surface *s)
{
    for (int y = 0; y < s->height; ++y) {
        for (int x = 0; x < s->width; ++x) {
            *(s->data + (y * s->width) + x) = 0;
        }
    }
}

/*----------------------------------------------------------------------------
 * initSurface
 *
 * Initialize a Surface struct with the width and height of the terminal window
 * and a zeroed block of memory.
 *----------------------------------------------------------------------------*/
void initSurface(Surface *s)
{
    struct winsize ws;
    ioctl(0, TIOCGWINSZ, &ws);

    s->width = ws.ws_col;
    s->height = ws.ws_row;

    s->data = (unsigned char *)malloc(s->width * s->height);

    clearSurface(s);
}

/*----------------------------------------------------------------------------
 * loadBitmap
 *
 * Extract the bitmap data from a BMP file and put it in a buffer that can be
 * passed to drawBitmap. Write 0xFFFFFF (white) as 0 and everything else as 1.
 * In other words, the function expects two-value data, where white is
 * interpreted as the image's transparent background and black is the solid
 * foreground where braille dots are printed.
 *----------------------------------------------------------------------------*/
struct Surface loadBitmap(char *filename)
{
    FILE *fp;
    Surface bitmap;
    unsigned char r, g, b;
    unsigned int pixel;
    unsigned int dataOffset;

    fp = fopen(filename, "r");

    fseek(fp, 0L, SEEK_END);

    int size = ftell(fp);
    rewind(fp);

    unsigned char *buf = (unsigned char *)malloc(sizeof(char) * size);

    fread(buf, 1, size, fp);

    fclose(fp);

    /* Read the width, height, and data offset information from the bitmap
     * file, converting from little-endian order. */
    bitmap.width = littleToBigEndian(buf + 0x12);
    bitmap.height = littleToBigEndian(buf + 0x16);
    dataOffset = littleToBigEndian(buf + 0x0A);

    bitmap.data = (unsigned char *)malloc(bitmap.width * bitmap.height);
    unsigned char *bp = bitmap.data;

    /* BMP data is "upside down" from a screen's perspective, but we leave it
     * this way, because our drawPoint function writes data "upside down" too,
     * in order to comply with the standard coordinate system employed in
     * mathematics. */
    for (int y = 0; y < bitmap.height; y++) {
        for (int x = 0; x < bitmap.width; x++) {
            int bufOffset = (y * bitmap.width) + x;
            b = *(buf + dataOffset + (bufOffset * 3));
            g = *(buf + dataOffset + (bufOffset * 3) + 1);
            r = *(buf + dataOffset + (bufOffset * 3) + 2);
            pixel = (r << 24) | (g << 16) | (b << 8);
            *bp++ = pixel ? 0 : 1;
        }
    }

    free(buf);

    return bitmap;
}


/*----------------------------------------------------------------------------
 * initLouis
 *
 * Perform various routines necessary for the graphics library to work:
 *
 * 1) Save the terminal's attributes on program entry.
 * 2) Switch to raw input mode.
 * 3) Generate the table of braille escape sequences.
 *----------------------------------------------------------------------------*/
void initLouis()
{
    tcgetattr(0, &origterm);
    rawterm = origterm;
    rawterm.c_iflag |= IGNBRK;
    rawterm.c_lflag &= ~ICANON;
    rawterm.c_lflag &= ~ECHO;
    rawterm.c_cc[VMIN] = 0;
    rawterm.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &rawterm);

    genBrailleTab();
}

/*----------------------------------------------------------------------------
 * endLouis
 *
 * Clear the screen, show the cursor, restore the original terminal attributes
 * for a clean exit, and free the screen buffer.
 *----------------------------------------------------------------------------*/
void endLouis()
{
    write(1, "\x1b[2J", 4);
    write(1, "\x1b[?25h", 6);
    tcsetattr(0, TCSAFLUSH, &origterm);
    free(screenBuffer);
}

