/*----------------------------------------------------------------------------
 * demo.c
 *
 * This program demonstrates routines from the louis graphics library.
 *----------------------------------------------------------------------------*/

#include "louis.h"

/*----------------------------------------------------------------------------
 * main
 *
 *----------------------------------------------------------------------------*/
int main()
{
    char c;
    float a = 0.1;
    float d = 0.01;

    initLouis();

    Surface s;
    initSurface(&s);

    Surface bmp = loadBitmap("louis.bmp");
    initSurface(&s);

    while (1) {
        read(0, &c, 1);
        if (c == 'q') {
            break;
        }

        usleep(20000);

        clearSurface(&s);

        drawCurve(&s, 0, 80, a, 10, 87);
        drawCurve(&s, 0, 80, -a, 10, 1000);
        drawBitmap(&s, &bmp, 85, 0);
        drawRect(&s, 200, 100, 20, 20, 1);
        drawRect(&s, 250, 50, 20, 20, 1);
        drawRect(&s, 300, 10, 20, 20, 1);
        drawLine(&s, 200, 150, 280, 150);
        drawLine(&s, 200, 150, 280, 100);

        render(&s);

        a += d;
        if (a > 0.5 || a < -0.5) {
            d *= -1;
        }
    }

    endLouis();
    free(s.data);

    return 0;
}
