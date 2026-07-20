#include "math.h"

double sin(double x) {
    double res;
    asm volatile ("fsin" : "=t" (res) : "0" (x));
    return res;
}

double cos(double x) {
    double res;
    asm volatile ("fcos" : "=t" (res) : "0" (x));
    return res;
}

double sqrt(double x) {
    double res;
    asm volatile ("fsqrt" : "=t" (res) : "0" (x));
    return res;
}

double tan(double x) {
    double res;
    asm volatile (
        "fptan\n"
        "fstp %%st(0)\n"
        : "=t"(res)
        : "0"(x)
    );
    return res;
}

double atan(double x) {
    double res;
    asm volatile ("fld1; fpatan" : "=t"(res) : "0"(x));
    return res;
}

double atan2(double y, double x) {
    double res;
    asm volatile ("fpatan" : "=t"(res) : "0"(x), "u"(y) : "st(1)");
    return res;
}

double acos(double x) {
    return atan2(sqrt(1.0 - x*x), x);
}

double asin(double x) {
    return atan2(x, sqrt(1.0 - x*x));
}

double fmod(double x, double y) {
    return x - (int)(x / y) * y;
}

double ceil(double x) {
    int i = (int)x;
    if (x > 0 && x != (double)i) return (double)(i + 1);
    return (double)i;
}
