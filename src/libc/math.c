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
