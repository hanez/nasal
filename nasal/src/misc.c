#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "nasl.h"
#include "code.h"

void FREE(void* m) { free(m); }
void* ALLOC(int n) { return malloc(n); }
void BZERO(void* m, int n) { bzero(m, n); }

int naTrue(naRef r)
{
    if(IS_NIL(r)) return 0;
    if(IS_NUM(r)) return r.num != 0;
    if(IS_STR(r)) return 1;
    return 0;
}

naRef naNumValue(naRef n)
{
    double d;
    if(IS_NUM(n)) return n;
    if(IS_STR(n) && naStr_tonum(n, &d))
        return naNum(d);
    return naNil();
}

naRef naStringValue(naContext c, naRef r)
{
    if(IS_NIL(r) || IS_STR(r)) return r;
    if(IS_NUM(r)) {
        naRef s = naNewString(c);
        naStr_fromnum(s, r.num);
        return s;
    }
    return naNil();
}

naRef naNew(struct Context* c, int type)
{
    struct naObj* o;
    if((o = naGC_get(&(c->pools[type]))) == 0) {
        naGarbageCollect();
        o = naGC_get(&(c->pools[type]));
    }
    return naObj(type, o);
}

naRef naNewString(struct Context* c)
{
    return naNew(c, T_STR);
}

naRef naNewVector(struct Context* c)
{
    naRef r = naNew(c, T_VEC);
    naVec_init(r);
    return r;
}

naRef naNewHash(struct Context* c)
{
    naRef r = naNew(c, T_HASH);
    naHash_init(r);
    return r;
}

naRef naNewCode(struct Context* c)
{
    return naNew(c, T_CODE);
}

naRef naNewCCode(struct Context* c, naCFunction fptr)
{
    naRef r = naNew(c, T_CCODE);
    r.ref.ptr.ccode->fptr = fptr;
    return r;
}

naRef naNewFunc(struct Context* c, naRef code, naRef closure)
{
    naRef func = naNew(c, T_FUNC);
    func.ref.ptr.func->code = code;
    func.ref.ptr.func->closure = closure;
    return func;
}

naRef naNewClosure(struct Context* c, naRef namespace, naRef next)
{
    naRef closure = naNew(c, T_CLOSURE);
    closure.ref.ptr.closure->namespace = namespace;
    closure.ref.ptr.closure->next = next;
    return closure;
}

naRef naNil()
{
    naRef r;
    r.ref.reftag = NASL_REFTAG;
    r.ref.ptr.obj = 0;
    return r;
}

naRef naNum(double num)
{
    naRef r;
    r.num = num;
    return r;
}

naRef naObj(int type, struct naObj* o)
{
    naRef r;
    r.ref.reftag = NASL_REFTAG;
    r.ref.ptr.obj = o;
    o->type = type;
    return r;
}

int naEqual(naRef a, naRef b)
{
    double na=0, nb=0;
    if(IS_REF(a) && IS_REF(b) && a.ref.ptr.obj == b.ref.ptr.obj)
        return 1; // Object identity (and nil == nil)
    if(IS_NUM(a) && IS_NUM(b) && a.num == b.num)
        return 1; // Numeric equality
    if(IS_STR(a) && IS_STR(b) && naStr_equal(a, b))
        return 1; // String equality

    // Numeric equality after conversion
    if(IS_NUM(a)) { na = a.num; }
    else if(!(IS_STR(a) && naStr_tonum(a, &na))) { return 0; }

    if(IS_NUM(b)) { nb = b.num; }
    else if(!(IS_STR(b) && naStr_tonum(b, &nb))) { return 0; }

    return na == nb ? 1 : 0;
}

int naTypeSize(int type)
{
    switch(type) {
    case T_STR: return sizeof(struct naStr);
    case T_VEC: return sizeof(struct naVec);
    case T_HASH: return sizeof(struct naHash);
    case T_CODE: return sizeof(struct naCode);
    case T_FUNC: return sizeof(struct naFunc);
    case T_CLOSURE: return sizeof(struct naClosure);
    case T_CCODE: return sizeof(struct naCCode);
    };
    return 0x7fffffff; // Make sure the answer is nonsense :)
}

int naIsNil(naRef r)
{
    return IS_NIL(r);
}

int naIsNum(naRef r)
{
    return IS_NUM(r);
}

int naIsString(naRef r)
{
    return IS_STR(r);
}

int naIsScalar(naRef r)
{
    return IS_SCALAR(r);
}

int naIsVector(naRef r)
{
    return IS_VEC(r);
}

int naIsHash(naRef r)
{
    return IS_HASH(r);
}

int naIsFunc(naRef r)
{
    return IS_FUNC(r);
}

int naIsCode(naRef r)
{
    return IS_CODE(r);
}

int naIsCCode(naRef r)
{
    return IS_CCODE(r);
}

