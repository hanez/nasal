#include <math.h>
#include <string.h>

#include "nasl.h"

// The maximum number of significant (decimal!) figures in an IEEE
// double.
#define DIGITS 16

static int tonum(unsigned char* s, int len, double* result);
static int fromnum(double val, unsigned char* s);

int naStr_len(naRef s)
{
    return s.ref.ptr.str->len;
}

unsigned char* naStr_data(naRef s)
{
    return s.ref.ptr.str->data;
}

void naStr_fromdata(naRef dst, unsigned char* data, int len)
{
    FREE(dst.ref.ptr.str->data);
    dst.ref.ptr.str->len = len;
    dst.ref.ptr.str->data = ALLOC(len);
    memcpy(dst.ref.ptr.str->data, data, len);
}

void naStr_concat(naRef dest, naRef s1, naRef s2)
{
    struct naStr* dst = dest.ref.ptr.str;
    struct naStr* a = s1.ref.ptr.str;
    struct naStr* b = s2.ref.ptr.str;
    FREE(dst->data);
    dst->len = a->len + b->len;
    dst->data = ALLOC(dst->len);
    memcpy(dst->data, a->data, a->len);
    memcpy(dst->data + a->len, b->data, b->len);
}

void naStr_substr(naRef dest, naRef str, int start, int len)
{
    struct naStr* dst = dest.ref.ptr.str;
    struct naStr* s = str.ref.ptr.str;
    FREE(dst->data);
    if(start + len > s->len) ERR("substr out of range");
    dst->len = len;
    dst->data = ALLOC(dst->len);
    memcpy(dst->data, s->data + start, len);
}

int naStr_equal(naRef s1, naRef s2)
{
    struct naStr* a = s1.ref.ptr.str;
    struct naStr* b = s2.ref.ptr.str;
    if(a->data == b->data) return 1;
    if(a->len != b->len) return 0;
    if(memcmp(a->data, b->data, a->len) == 0) return 1;
    return 0;
}

void naStr_fromnum(naRef dest, double num)
{
    struct naStr* dst = dest.ref.ptr.str;
    unsigned char buf[DIGITS+8];
    dst->len = fromnum(num, buf);
    dst->data = ALLOC(dst->len);
    memcpy(dst->data, buf, dst->len);
}

int naStr_parsenum(naRef str, double* result)
{
    return tonum(str.ref.ptr.str->data, str.ref.ptr.str->len, result);
}

naRef naStr_tonum(naRef str)
{
    naRef r;
    struct naStr* s = str.ref.ptr.str;
    double n;
    if(!tonum(s->data, s->len, &n)) ERR("string not number");
    r.ref.reftag = 0;
    r.num = n;
    return r;
}

////////////////////////////////////////////////////////////////////////
// Below is a custom double<->string conversion library.  Why not
// simply use sprintf and atof?  Because they aren't acceptably
// platform independant, sadly.  I've seen some very strange results.
// This works the same way everywhere, although it is tied to an
// assumptions of standard IEEE 64 bit floating point doubles.
//
// In practice, these routines work quite well.  Testing conversions
// of random doubles to strings and back, this routine is beaten by
// glibc on roundoff error 23% of the time, beats glibc in 10% of
// cases, and ties (usually with an error of exactly zero) the
// remaining 67%.
////////////////////////////////////////////////////////////////////////

// Reads an unsigned decimal out of the scalar starting at i, stores
// it in v, and returns the next index to start at.  Zero-length
// decimal numbers are allowed, and are returned as zero.
static int readdec(unsigned char* s, int len, int i, double* v)
{
    *v = 0;
    if(i >= len) return len;
    while(i < len && s[i] >= '0' && s[i] <= '9') {
        *v= (*v) * 10 + (s[i] - '0');
        i++;
    }
    return i;
}

// Reads a signed integer out of the string starting at i, stores it
// in v, and returns the next index to start at.  Zero-length
// decimal numbers (and length-1 strings like '+') are allowed, and
// are returned as zero.
static int readsigned(unsigned char* s, int len, int i, double* v)
{
    double sgn=1, val;
    if(i >= len) { *v = 0; return len; }
    if(s[i] == '+')      { i++; }
    else if(s[i] == '-') { i++; sgn = -1; }
    i = readdec(s, len, i, &val);
    *v = sgn*val;
    return i;
}


// Integer decimal power utility, with a tweak that enforces
// integer-exactness for arguments where that is possible.
static double decpow(int exp)
{
    double v = 1;
    int absexp;
    if(exp < 0 || exp >= DIGITS)
        return pow(10, exp);
    else
        absexp = exp < 0 ? -exp : exp;
    while(absexp--) v *= 10.0;
    return v;
}

static int tonum(unsigned char* s, int len, double* result)
{
    int i=0, fraclen=0;
    double sgn=1, val, frac=0, exp=0;

    // Read the integer part
    i = readsigned(s, len, i, &val);
    if(val < 0) { sgn = -1; val = -val; }

    // Read the fractional part, if any
    if(i < len && s[i] == '.') {
        i++;
        fraclen = readdec(s, len, i, &frac) - i;
        i += fraclen;
    }

    // Read the exponent, if any
    if(i < len && (s[i] == 'e' || s[i] == 'E'))
        i = readsigned(s, len, i+1, &exp);
    
    // compute the result
    *result = sgn * (val + frac * decpow(-fraclen)) * decpow(exp);

    // if we didn't use the whole string, return failure
    if(i < len) return 0;
    return 1;
}

// Very simple positive (!) integer print routine.  Puts the result in
// s and returns the number of characters written.  Does not null
// terminate the result.
static int decprint(int val, unsigned char* s)
{
    int p=1, i=0;
    if(val == 0) { *s = '0'; return 1; }
    while(p <= val) p *= 10;
    p /= 10;
    while(p > 0) {
        int count = 0;
        while(val >= p) { val -= p; count++; }
        s[i++] = '0' + count;
        p /= 10;
    }
    return i;
}

// Takes a positive (!) floating point numbers, and returns exactly
// DIGITS decimal numbers in the buffer pointed to by s, and an
// integer exponent as the return value.  For example, printing 1.0
// will result in "1000000000000000" in the buffer and -15 as the
// exponent.  The caller can then place the floating point as needed.
static int rawprint(double val, unsigned char* s)
{
    int exponent = (int)floor(log10(val));
    double mantissa = val / pow(10, exponent);
    int i, c;
    for(i=0; i<DIGITS-1; i++) {
        int digit = (int)floor(mantissa);
        s[i] = '0' + digit;
        mantissa -= digit;
        mantissa *= 10.0;
    }
    // Round (i.e. don't floor) the last digit
    c = rint(mantissa);
    if(c < 0) c = 0;
    if(c > 9) c = 9;
    s[i] = '0' + c;
    return exponent - DIGITS + 1;
}

static int fromnum(double val, unsigned char* s)
{
    unsigned char raw[DIGITS];
    unsigned char* ptr = s;
    int exp, digs, i=0;

    // Identically zero is a special case
    if(val == 0.0) { *ptr++ = '0'; *ptr++ = 0; return 1; }

    // Handle negatives
    if(val < 0) { *ptr++ = '-'; val = -val; }

    // Get the raw digits
    exp = rawprint(val, raw);

    // Examine trailing zeros to get a significant digit count
    for(i=DIGITS-1; i>0; i--)
        if(raw[i] != '0') break;
    digs = i+1;

    if(exp > 0 || exp < -(DIGITS+2)) {
        // Standard scientific notation
        exp += DIGITS-1;
        *ptr++ = raw[0];
        if(digs > 1) {
            *ptr++ = '.';
            for(i=1; i<digs; i++) *ptr++ = raw[i];
        }
        *ptr++ = 'e';
        if(exp < 0) { exp = -exp; *ptr++ = '-'; }
        else { *ptr++ = '+'; }
        if(exp < 10) *ptr++ = '0';
        ptr += decprint(exp, ptr);
    } else if(exp < 1-DIGITS) {
        // Fraction with insignificant leading zeros
        *ptr++ = '0'; *ptr++ = '.';
        for(i=0; i<-(exp+DIGITS); i++) *ptr++ = '0';
        for(i=0; i<digs; i++) *ptr++ = raw[i];
    } else {
        // Integer part
        for(i=0; i<DIGITS+exp; i++) *ptr++ = raw[i];
        if(i < digs) {
            // Fraction, if any
            *ptr++ = '.';
            while(i<digs) *ptr++ = raw[i++];
        }
    }
    *ptr = 0;
    return ptr - s;
}

#if 0
////////////////////////////////////////////////////////////////////////
// Debug/test code below
////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>

int main()
{
    int i, len;
    double d2, err, err2;
    char buf[1024];
    union {
        char b[8];
        double d;
    } num;

    while(1) {
        for(i=0; i<8; i++) num.b[i] = (char)(rand() >> 8);
        if(!finite(num.d)) continue;

        printf("%24.16g -> ", num.d);
        
        // My way
        len = fromnum(num.d, buf);
        tonum(buf, len, &d2);
        err = 1e16 * fabs((d2 - num.d) / num.d);
        printf(" [%f] ", err);

        // glibc
        sprintf(buf, "%24.16g", num.d);
        d2 = atof(buf);
        err2 = 1e16 * fabs((d2 - num.d) / num.d);
        printf(" [%f]", err2);

        printf(" %s\n", err>err2 ? "glibc" : err==err2 ? "tie" : "nasl");
   }
}
#endif
