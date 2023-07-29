#include <cstdlib>
#include <cmath>
#include "../postprocessors/gamma.h"
#include "mathMacros.h"

void
cubic_spline(const int *x_, const int *y_, const int len) {
    float **A;
    float *b;
    float *c;
    float *d;
    float *x;
    float *y;
    int i;
    int j;

    A = (float **)calloc(((2 * len + 4) * sizeof **A + sizeof *A), 2 * len);
    if ( !A ) {
        return;
    }
    A[0] = (float *) (A + 2 * len);
    for ( i = 1; i < 2 * len; i++ ) {
        A[i] = A[0] + 2 * len * i;
    }
    y = len + (x = i + (d = i + (c = i + (b = A[0] + i * i))));
    for ( i = 0; i < len; i++ ) {
        x[i] = x_[i] / 65535.0;
        y[i] = y_[i] / 65535.0;
    }
    for ( i = len - 1; i > 0; i-- ) {
        b[i] = (y[i] - y[i - 1]) / (x[i] - x[i - 1]);
        d[i - 1] = x[i] - x[i - 1];
    }
    for ( i = 1; i < len - 1; i++ ) {
        A[i][i] = 2 * (d[i - 1] + d[i]);
        if ( i > 1 ) {
            A[i][i - 1] = d[i - 1];
            A[i - 1][i] = d[i - 1];
        }
        A[i][len - 1] = 6 * (b[i + 1] - b[i]);
    }
    for ( i = 1; i < len - 2; i++ ) {
        float v = A[i + 1][i] / A[i][i];
        for ( j = 1; j <= len - 1; j++ ) {
            A[i + 1][j] -= v * A[i][j];
        }
    }
    for ( i = len - 2; i > 0; i-- ) {
        float acc = 0;
        for ( j = i; j <= len - 2; j++ ) {
            acc += A[i][j] * c[j];
        }
        c[i] = (A[i][len - 1] - acc) / A[i][i];
    }
    for ( i = 0; i < 0x10000; i++ ) {
        float x_out = (float) (i / 65535.0);
        float y_out = 0;
        for ( j = 0; j < len - 1; j++ ) {
            if ( x[j] <= x_out && x_out <= x[j + 1] ) {
                float v = x_out - x[j];
                y_out = y[j] +
                        ((y[j + 1] - y[j]) / d[j] - (2 * d[j] * c[j] + c[j + 1] * d[j]) / 6) * v
                        + (c[j] * 0.5) * v * v + ((c[j + 1] - c[j]) / (6 * d[j])) * v * v * v;
            }
        }
        GAMMA_curveFunctionLookupTable[i] = y_out < 0.0 ? 0 : (y_out >= 1.0 ? 65535 : (unsigned short)lround(y_out * 65535.0 + 0.5));
    }
    free(A);
}
