#include <stdio.h> // DEBUG

#include "parse.h"
#include "code.h"

// These are more sensical predicate names in most contexts in this file
#define LEFT(tok)   ((tok)->children)
#define RIGHT(tok)  ((tok)->lastChild)
#define BINARY(tok) (LEFT(tok) != RIGHT(tok))

// Forward references for recursion
static void genExpr(struct Parser* p, struct Token* t);
static void genExprList(struct Parser* p, struct Token* t);

static void emit(struct Parser* p, int byte)
{
    if(p->nBytes >= p->codeAlloced) {
        int i, sz = p->codeAlloced * 2;
        unsigned char* buf = naParseAlloc(p, sz);
        for(i=0; i<p->codeAlloced; i++) buf[i] = p->byteCode[i];
        p->byteCode = buf;
        p->codeAlloced = sz;
    }
    p->byteCode[p->nBytes++] = (unsigned char)byte;
}

static void genBinOp(int op, struct Parser* p, struct Token* t)
{
    genExpr(p, LEFT(t));
    genExpr(p, RIGHT(t));
    emit(p, op);
}

void printRefDEBUG(naRef r);
static void genConstant(struct Parser* p, struct Token* t)
{
    naRef c, val;
    int idx;

    // Generate a Nasl scalar from the token data
    if(t->str) {
        c = naNewString(p->context);
        naStr_fromdata(c, t->str, t->strlen);
    } else {
        c = naNum(t->num);
    }

    // Is it already there?  Create a new index if not.
    val = naHash_get(p->constHash, c);
    if(IS_NIL(val)) {
        idx = p->nConsts++;
        val = naNum(idx);
        naHash_set(p->constHash, c, val);
        printf("CONSTANT %d = ", idx);
        printRefDEBUG(c);
    } else {
        idx = val.num;
    }

    // Static limit
    if(p->nConsts > 0x10000)
        naParseError(p, "Too many constants in code block", 0);
    
    // Emit the code
    emit(p, OP_PUSHCONST);
    emit(p, idx >> 8);
    emit(p, idx & 0xff);
}

static int genLValue(struct Parser* p, struct Token* t)
{
    if(t->type == TOK_LPAR) {
        return genLValue(p, LEFT(t)); // Handle stuff like "(a) = 1"
    } else if(t->type == TOK_SYMBOL) {
        genConstant(p, t);
        return OP_SETLOCAL;
    } else if(t->type == TOK_DOT && RIGHT(t) && RIGHT(t)->type == TOK_SYMBOL) {
        genExpr(p, LEFT(t));
        genConstant(p, RIGHT(t));
        return OP_SETMEMBER;
    } else if(t->type == TOK_LBRA) {
        genExpr(p, LEFT(t));
        genExpr(p, RIGHT(t));
        return OP_INSERT;
    } else {
        naParseError(p, "bad lvalue", t->line);
        return -1;
    }
}

static void genLambda(struct Parser* p, struct Token* t)
{
    *(int*)0=0;
}

static void genList(struct Parser* p, struct Token* t)
{
    *(int*)0=0;
}

static void genHash(struct Parser* p, struct Token* t)
{
    *(int*)0=0;
}

static void genFuncall(struct Parser* p, struct Token* t)
{
    *(int*)0=0;
}

static void genExpr(struct Parser* p, struct Token* t)
{
    int i;

    switch(t->type) {
    case TOK_IF: break;
    case TOK_FOR: break;
    case TOK_FOREACH: break;
    case TOK_WHILE: break;
    case TOK_BREAK: break;
    case TOK_CONTINUE: break;

    case TOK_TOP: genExprList(p, LEFT(t)); break;
    case TOK_FUNC:
        genLambda(p, t);
        break;
    case TOK_LPAR:
        if(BINARY(t)) genFuncall(p, t);    // function invocation
        else          genExpr(p, LEFT(t)); // simple parenthesis
        break;
    case TOK_LBRA:
        if(BINARY(t)) genList(p, t);
        else          genBinOp(OP_EXTRACT, p, t);
        break;
    case TOK_LCURL:
        genHash(p, t);
        break;
    case TOK_ASSIGN:
        i = genLValue(p, LEFT(t));
        genExpr(p, RIGHT(t));
        emit(p, i); // use the op appropriate to the lvalue
        break;
    case TOK_RETURN:
        genExpr(p, LEFT(t));
        emit(p, OP_RETURN);
        break;
    case TOK_NOT:
        genExpr(p, LEFT(t));
        emit(p, OP_NOT);
        break;
    case TOK_SYMBOL:
        genConstant(p, t);
        emit(p, OP_LOCAL);
        break;
    case TOK_LITERAL:
        genConstant(p, t);
        break;
    case TOK_MINUS:
        if(BINARY(t)) {
            genBinOp(OP_MINUS,  p, t);  // binary subtraction
        } else if(RIGHT(t)->type == TOK_LITERAL && !RIGHT(t)->str) {
            RIGHT(t)->num *= -1;        // Pre-negate constants
            genConstant(p, RIGHT(t));
        } else {
            genExpr(p, RIGHT(t));       // unary negation
            emit(p, OP_NEG);
        }
        break;
    case TOK_EMPTY: emit(p, OP_PUSHNIL); break; // *NOT* a noop!
    case TOK_AND:   genBinOp(OP_AND,    p, t); break;
    case TOK_OR:    genBinOp(OP_OR,     p, t); break;
    case TOK_MUL:   genBinOp(OP_MUL,    p, t); break;
    case TOK_PLUS:  genBinOp(OP_PLUS,   p, t); break;
    case TOK_DIV:   genBinOp(OP_DIV,    p, t); break;
    case TOK_CAT:   genBinOp(OP_CAT,    p, t); break;
    case TOK_DOT:   genBinOp(OP_MEMBER, p, t); break;
    case TOK_LT:    genBinOp(OP_LT,     p, t); break;
    case TOK_LTE:   genBinOp(OP_LTE,    p, t); break;
    case TOK_EQ:    genBinOp(OP_EQ,     p, t); break;
    case TOK_NEQ:   genBinOp(OP_NEQ,    p, t); break;
    case TOK_GT:    genBinOp(OP_GT,     p, t); break;
    case TOK_GTE:   genBinOp(OP_GTE,    p, t); break;
    default:
        naParseError(p, "parse error", t->line);
    };
}

static void genExprList(struct Parser* p, struct Token* t)
{
    if(t->type == TOK_SEMI) {
        genExpr(p, t->children);
        emit(p, OP_POP);
        genExprList(p, t->lastChild);
    } else {
        genExpr(p, t);
    }
}

naRef naCodeGen(struct Parser* p, struct Token* t)
{
    int i;
    naRef codeObj, vec;
    struct naCode* code;

    p->codeAlloced = 1024; // Start fairly big, this is a cheap allocation
    p->byteCode = naParseAlloc(p, p->codeAlloced);
    p->nBytes = 0;

    p->constHash = naNewHash(p->context);
    p->nConsts = 0;

    genExprList(p, t);

    // Now make a code object
    codeObj = naNewCode(p->context);
    code = codeObj.ref.ptr.code;
    code->nBytes = p->nBytes;
    code->byteCode = ALLOC(code->nBytes);
    for(i=0; i < code->nBytes; i++)
        code->byteCode[i] = p->byteCode[i];
    code->nConstants = p->nConsts;
    code->constants = ALLOC(code->nConstants * sizeof(naRef));

    // Invert the hash.  In C.  Fun.  I wish I could do this part in
    // Nasl. :)
    vec = naNewVector(p->context);
    naHash_keys(vec, p->constHash);
    for(i=0; i < code->nConstants; i++) {
        naRef key = naVec_get(vec, i);
        naRef idx = naHash_get(p->constHash, key);
        int i = (int)idx.num;
        code->constants[i] = key;
    }

    return codeObj;
}
