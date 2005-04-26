#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "data.h"

// Note use of 32 bit ints, should fix at some point to use
// platform-dependent fpos_t/size_t or just punt and use int64_t
// everywhere...

// The naContext is passed in for error reporting via
// naRuntimeError().
struct naIOType {
    void (*close)(naContext c, void* f);
    int  (*read) (naContext c, void* f, char* buf, unsigned int len);
    int  (*write)(naContext c, void* f, char* buf, unsigned int len);
    void (*seek) (naContext c, void* f, unsigned int off, int whence);
    int  (*tell) (naContext c, void* f);
    void (*destroy)(void* f);
};

struct naIOGhost {
    struct naIOType* type;
    void* handle; // descriptor, FILE*, HANDLE, etc...
};

static void ghostDestroy(void* g);
naGhostType naIOGhostType = { ghostDestroy };

static struct naIOGhost* ioghost(naRef r)
{
    if(naGhost_type(r) == &naIOGhostType)
        return naGhost_ptr(r);
    return 0;
}

static naRef f_close(naContext c, naRef me, int argc, naRef* args)
{
    struct naIOGhost* g = argc==1 ? ioghost(args[0]) : 0;
    if(!g)
        naRuntimeError(c, "bad argument to close()");
    g->type->close(c, g->handle);
    return naNil();
}

static naRef f_read(naContext c, naRef me, int argc, naRef* args)
{
    struct naIOGhost* g = argc > 0 ? ioghost(args[0]) : 0;
    naRef str = argc > 1 ? args[1] : naNil();
    naRef len = argc > 2 ? naNumValue(args[2]) : naNil();
    if(!g || !MUTABLE(str) || !IS_NUM(len))
        naRuntimeError(c, "bad argument to read()");
    if(str.ref.ptr.str->len < (int)len.num)
        naRuntimeError(c, "string not big enough for read");
    return naNum(g->type->read(c, g->handle, str.ref.ptr.str->data,
                               (int)len.num));
}

static naRef f_write(naContext c, naRef me, int argc, naRef* args)
{
    struct naIOGhost* g = argc > 0 ? ioghost(args[0]) : 0;
    naRef str = argc > 1 ? args[1] : naNil();
    if(!g || !IS_STR(str))
        naRuntimeError(c, "bad argument to write()");
    return naNum(g->type->write(c, g->handle, str.ref.ptr.str->data,
                                str.ref.ptr.str->len));
}

static naRef f_seek(naContext c, naRef me, int argc, naRef* args)
{
    struct naIOGhost* g = argc > 0 ? ioghost(args[0]) : 0;
    naRef pos = argc > 1 ? naNumValue(args[1]) : naNil();
    naRef whn = argc > 2 ? naNumValue(args[2]) : naNil();
    if(!g || !IS_NUM(pos) || !IS_NUM(whn))
        naRuntimeError(c, "bad argument to seek()");
    g->type->seek(c, g->handle, (int)pos.num, (int)whn.num);
    return naNil();
}

static naRef f_tell(naContext c, naRef me, int argc, naRef* args)
{
    struct naIOGhost* g = argc==1 ? ioghost(args[0]) : 0;
    if(!g)
        naRuntimeError(c, "bad argument to tell()");
    return naNum(g->type->tell(c, g->handle));
}

static void ghostDestroy(void* g)
{
    struct naIOGhost* io = (struct naIOGhost*)g;
    io->type->destroy(io->handle);
    naFree(io);
}

////////////////////////////////////////////////////////////////////////
// stdio library implementation below:

static void ioclose(naContext c, void* f)
{
    if(fclose(f) != 0 && c) naRuntimeError(c, strerror(errno));
}

static int ioread(naContext c, void* f, char* buf, unsigned int len)
{
    int n = fread(buf, 1, len, f);
    if(n < len && !feof(f)) naRuntimeError(c, strerror(errno));
    return n;
}

static int iowrite(naContext c, void* f, char* buf, unsigned int len)
{
    int n = fwrite(buf, 1, len, f);
    if(ferror(f)) naRuntimeError(c, strerror(errno));
    return n;
}

static void ioseek(naContext c, void* f, unsigned int off, int whence)
{
    if(fseek(f, off, whence) != 0) naRuntimeError(c, strerror(errno));
}

static int iotell(naContext c, void* f)
{
    int n = ftell(f);
    if(n < 0) naRuntimeError(c, strerror(errno));
    return n;
}

static void iodestroy(void* f)
{
    ioclose(0, f);
}

static struct naIOType StdIOType = { ioclose, ioread, iowrite, ioseek,
                                     iotell, iodestroy };

static naRef newGhost(naContext c, FILE* f)
{
    struct naIOGhost* ghost = naAlloc(sizeof(struct naIOGhost));
    ghost->type = &StdIOType;
    ghost->handle = f;
    return naNewGhost(c, &naIOGhostType, ghost);
}

static naRef f_open(naContext c, naRef me, int argc, naRef* args)
{
    FILE* f;
    naRef file = argc > 0 ? naStringValue(c, args[0]) : naNil();
    naRef mode = argc > 1 ? naStringValue(c, args[1]) : naNil();
    if(!IS_STR(file)) naRuntimeError(c, "bad argument to open()");
    f = fopen(file.ref.ptr.str->data,
              IS_STR(mode) ? (const char*)mode.ref.ptr.str->data : "r");
    if(!f) naRuntimeError(c, strerror(errno));
    return newGhost(c, f);
}

// frees buffer before tossing an error
static int getcguard(naContext ctx, FILE* f, void* buf)
{
    char c = fgetc(f);
    if(ferror(f)) {
        naFree(buf);
        naRuntimeError(ctx, strerror(errno));
    }
    return c;
}

// Handles multiple EOL conventions by using stdio's ungetc.  Will not
// work for other IO types without converting them to FILE* with
// fdopen() or whatnot...
static naRef f_readln(naContext ctx, naRef me, int argc, naRef* args)
{
    naRef result;
    struct naIOGhost* g = argc==1 ? ioghost(args[0]) : 0;
    int i=0, sz = 128;
    char* buf;
    if(!g || g->type != &StdIOType)
        naRuntimeError(ctx, "bad argument to readln()");
    buf = naAlloc(sz);
    while(1) {
        char c = getcguard(ctx, g->handle, buf);
        if(c == EOF || c == '\n') break;
        if(c == '\r') {
            char c2 = getcguard(ctx, g->handle, buf);
            if(c2 != EOF && c2 != '\n')
                ungetc(c2, g->handle);
            break;
        }
        buf[i++] = c;
        if(i >= sz) buf = naRealloc(buf, sz *= 2);
    }
    result = naStr_fromdata(naNewString(ctx), buf, i);
    naFree(buf);
    return result;
}

static struct func { char* name; naCFunction func; } funcs[] = {
    { "close", f_close },
    { "read", f_read },
    { "write", f_write },
    { "seek", f_seek },
    { "tell", f_tell },
    { "open", f_open },
    { "readln", f_readln },
};

void setsym(naContext c, naRef hash, char* sym, naRef val)
{
    naRef name = naStr_fromdata(naNewString(c), sym, strlen(sym));
    naHash_set(hash, naInternSymbol(name), val);
}

naRef naIOLib(naContext c)
{
    naRef ns = naNewHash(c);
    int i, n = sizeof(funcs)/sizeof(struct func);
    for(i=0; i<n; i++)
        setsym(c, ns, funcs[i].name,
               naNewFunc(c, naNewCCode(c, funcs[i].func)));
    setsym(c, ns, "SEEK_SET", naNum(SEEK_SET));
    setsym(c, ns, "SEEK_CUR", naNum(SEEK_CUR));
    setsym(c, ns, "SEEK_END", naNum(SEEK_END));
    setsym(c, ns, "stdin", newGhost(c, stdin));
    setsym(c, ns, "stdout", newGhost(c, stdout));
    setsym(c, ns, "stderr", newGhost(c, stderr));
    return ns;
}