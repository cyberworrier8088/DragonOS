/* minitcc.c -- Self-contained C subset compiler/interpreter for DragonOS
 *
 * Implements a recursive-descent parser for a useful subset of C:
 *   - Types: int, char, long, void, pointers, arrays, struct
 *   - Control: if/else, while, for, do-while, switch/case, return, break, continue
 *   - Expressions: arithmetic, comparison, logical, bitwise, assignment, ternary
 *   - Functions: definitions, calls, forward declarations
 *   - Preprocessor: #define (simple constants), #include (ignored)
 *   - Built-ins: printf, puts, putchar, malloc, free, strlen, strcmp, memset, memcpy
 *
 * Compiles to a stack-based bytecode VM and executes entirely in-memory.
 * No file output or ELF generation -- this is a compile-and-run interpreter.
 *
 * Copyright (c) 2026 DragonOS Project. MIT License.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

extern void* malloc(size_t size);
extern void  free(void* ptr);
extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);
extern size_t strlen(const char* s);
extern int strcmp(const char* s1, const char* s2);
extern int strncmp(const char* s1, const char* s2, size_t n);
extern char* strcpy(char* dest, const char* src);
extern char* strncpy(char* dest, const char* src, size_t n);
extern char* strchr(const char* s, int c);
extern int printf(const char* format, ...);
extern int sprintf(char* str, const char* format, ...);

/* ============================================================
 * Section 1: Token Definitions
 * ============================================================ */

enum {
    /* Single-character tokens are their ASCII value */
    TK_NUM = 256, TK_STR, TK_ID,
    /* Keywords */
    TK_INT, TK_CHAR, TK_LONG, TK_VOID, TK_STRUCT, TK_ENUM,
    TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_DO, TK_RETURN,
    TK_BREAK, TK_CONTINUE, TK_SWITCH, TK_CASE, TK_DEFAULT,
    TK_SIZEOF,
    /* Multi-character operators */
    TK_EQ, TK_NE, TK_LE, TK_GE, TK_AND, TK_OR,
    TK_SHL, TK_SHR, TK_INC, TK_DEC,
    TK_ADDASSIGN, TK_SUBASSIGN, TK_MULASSIGN, TK_DIVASSIGN,
    TK_ARROW,
    TK_EOF
};

/* ============================================================
 * Section 2: Bytecode VM Opcodes
 * ============================================================ */

enum {
    OP_NOP,
    OP_IMM,       /* push immediate value */
    OP_STR,       /* push string address */
    OP_LOCAL,     /* load local variable address (offset from BP) */
    OP_GLOBAL,    /* load global variable address */
    OP_LOAD,      /* dereference: *top */
    OP_LOADB,     /* dereference byte: *(char*)top */
    OP_STORE,     /* store: *second = top */
    OP_STOREB,    /* store byte */
    OP_PUSH,      /* push top to argument stack */
    OP_JMP,       /* unconditional jump */
    OP_JZ,        /* jump if zero */
    OP_JNZ,       /* jump if nonzero */
    OP_CALL,      /* call function */
    OP_BUILTIN,   /* call built-in function */
    OP_RET,       /* return from function */
    OP_ENTER,     /* function entry: allocate locals */
    OP_LEAVE,     /* function exit: deallocate locals */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR,
    OP_LAND, OP_LOR,
    OP_NEG, OP_NOT, OP_BNOT,
    OP_POP,       /* discard top of stack */
    OP_DUP,       /* duplicate top of stack */
    OP_EXIT       /* halt VM */
};

/* ============================================================
 * Section 3: Compiler State
 * ============================================================ */

#define MAX_CODE    65536
#define MAX_DATA    32768
#define MAX_STACK   8192
#define MAX_SYMS    1024
#define MAX_STRINGS 4096
#define MAX_DEFINES 256
#define MAX_ID_LEN  64

typedef struct {
    char name[MAX_ID_LEN];
    int  addr;       /* code address for functions, data offset for globals */
    int  is_func;
    int  n_params;
    int  n_locals;
    int  type_size;  /* 1 for char, 8 for int/long/pointer */
} Symbol;

typedef struct {
    char name[MAX_ID_LEN];
    long value;
} Define;

typedef struct {
    /* Source input */
    const char* src;
    const char* src_end;
    const char* pos;
    int  line;

    /* Current token */
    int  token;
    long token_val;
    char token_str[1024];
    char token_id[MAX_ID_LEN];

    /* Code segment */
    int  code[MAX_CODE];
    int  code_pos;

    /* Data segment (strings, globals) */
    char data[MAX_DATA];
    int  data_pos;

    /* Symbol table */
    Symbol syms[MAX_SYMS];
    int    n_syms;

    /* #define table */
    Define defines[MAX_DEFINES];
    int    n_defines;

    int is_lvalue;

    /* Local variable tracking for current function */
    struct { char name[MAX_ID_LEN]; int offset; int size; } locals[256];
    int n_locals;
    int local_offset;

    /* Break/continue stack for loops */
    int break_stack[64];
    int continue_stack[64];
    int loop_depth;

    /* Nested-parenthesis depth in parse_primary(), guarded against unbounded
     * recursion (see parse_primary). */
    int paren_depth;

    /* Error tracking */
    int  has_error;
    char error_msg[256];
} TccState;

/* ============================================================
 * Section 4: Lexer
 * ============================================================ */

static void tcc_error(TccState* s, const char* msg) {
    if (!s->has_error) {
        s->has_error = 1;
        sprintf(s->error_msg, "tcc: line %d: %s", s->line, msg);
    }
}

static void skip_whitespace(TccState* s) {
    while (s->pos < s->src_end) {
        if (*s->pos == ' ' || *s->pos == '\t' || *s->pos == '\r') {
            s->pos++;
        } else if (*s->pos == '\n') {
            s->line++;
            s->pos++;
        } else if (s->pos + 1 < s->src_end && s->pos[0] == '/' && s->pos[1] == '/') {
            /* Single-line comment */
            s->pos += 2;
            while (s->pos < s->src_end && *s->pos != '\n') s->pos++;
        } else if (s->pos + 1 < s->src_end && s->pos[0] == '/' && s->pos[1] == '*') {
            /* Multi-line comment */
            s->pos += 2;
            while (s->pos + 1 < s->src_end && !(s->pos[0] == '*' && s->pos[1] == '/')) {
                if (*s->pos == '\n') s->line++;
                s->pos++;
            }
            if (s->pos + 1 < s->src_end) s->pos += 2;
        } else {
            break;
        }
    }
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static void next_token(TccState* s) {
    skip_whitespace(s);

    if (s->pos >= s->src_end) {
        s->token = TK_EOF;
        return;
    }

    /* Preprocessor directives */
    if (*s->pos == '#') {
        s->pos++;
        skip_whitespace(s);
        /* Parse directive name */
        char dir[32];
        int di = 0;
        while (s->pos < s->src_end && is_alpha(*s->pos) && di < 31) {
            dir[di++] = *s->pos++;
        }
        dir[di] = '\0';

        if (strcmp(dir, "define") == 0) {
            skip_whitespace(s);
            /* Parse define name */
            char dname[MAX_ID_LEN];
            int ni = 0;
            while (s->pos < s->src_end && is_alnum(*s->pos) && ni < MAX_ID_LEN - 1) {
                dname[ni++] = *s->pos++;
            }
            dname[ni] = '\0';

            skip_whitespace(s);
            /* Parse define value (simple integer) */
            long val = 0;
            int neg = 0;
            if (s->pos < s->src_end && *s->pos == '-') { neg = 1; s->pos++; }
            while (s->pos < s->src_end && is_digit(*s->pos)) {
                val = val * 10 + (*s->pos - '0');
                s->pos++;
            }
            if (neg) val = -val;

            if (s->n_defines < MAX_DEFINES) {
                strcpy(s->defines[s->n_defines].name, dname);
                s->defines[s->n_defines].value = val;
                s->n_defines++;
            }
        }
        /* Skip rest of line for any directive */
        while (s->pos < s->src_end && *s->pos != '\n') s->pos++;
        next_token(s);
        return;
    }

    /* Numbers */
    if (is_digit(*s->pos)) {
        long val = 0;
        if (s->pos + 1 < s->src_end && s->pos[0] == '0' && (s->pos[1] == 'x' || s->pos[1] == 'X')) {
            s->pos += 2;
            while (s->pos < s->src_end) {
                char c = *s->pos;
                if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                else break;
                s->pos++;
            }
        } else {
            while (s->pos < s->src_end && is_digit(*s->pos)) {
                val = val * 10 + (*s->pos - '0');
                s->pos++;
            }
        }
        /* Skip type suffix (L, U, etc.) */
        while (s->pos < s->src_end && (*s->pos == 'L' || *s->pos == 'l' ||
               *s->pos == 'U' || *s->pos == 'u')) s->pos++;
        s->token = TK_NUM;
        s->token_val = val;
        return;
    }

    /* Character literals */
    if (*s->pos == '\'') {
        s->pos++;
        long val = 0;
        if (*s->pos == '\\') {
            s->pos++;
            switch (*s->pos) {
                case 'n': val = '\n'; break;
                case 't': val = '\t'; break;
                case 'r': val = '\r'; break;
                case '0': val = '\0'; break;
                case '\\': val = '\\'; break;
                case '\'': val = '\''; break;
                default: val = *s->pos; break;
            }
        } else {
            val = *s->pos;
        }
        s->pos++;
        if (s->pos < s->src_end && *s->pos == '\'') s->pos++;
        s->token = TK_NUM;
        s->token_val = val;
        return;
    }

    /* String literals */
    if (*s->pos == '"') {
        s->pos++;
        int si = 0;
        while (s->pos < s->src_end && *s->pos != '"' && si < 1022) {
            if (*s->pos == '\\') {
                s->pos++;
                switch (*s->pos) {
                    case 'n': s->token_str[si++] = '\n'; break;
                    case 't': s->token_str[si++] = '\t'; break;
                    case 'r': s->token_str[si++] = '\r'; break;
                    case '0': s->token_str[si++] = '\0'; break;
                    case '\\': s->token_str[si++] = '\\'; break;
                    case '"': s->token_str[si++] = '"'; break;
                    default: s->token_str[si++] = *s->pos; break;
                }
            } else {
                s->token_str[si++] = *s->pos;
            }
            s->pos++;
        }
        s->token_str[si] = '\0';
        if (s->pos < s->src_end) s->pos++; /* skip closing quote */
        s->token = TK_STR;
        return;
    }

    /* Identifiers and keywords */
    if (is_alpha(*s->pos)) {
        int ni = 0;
        while (s->pos < s->src_end && is_alnum(*s->pos) && ni < MAX_ID_LEN - 1) {
            s->token_id[ni++] = *s->pos++;
        }
        s->token_id[ni] = '\0';

        /* Check keywords */
        if (strcmp(s->token_id, "int") == 0)       { s->token = TK_INT; return; }
        if (strcmp(s->token_id, "char") == 0)      { s->token = TK_CHAR; return; }
        if (strcmp(s->token_id, "long") == 0)      { s->token = TK_LONG; return; }
        if (strcmp(s->token_id, "void") == 0)      { s->token = TK_VOID; return; }
        if (strcmp(s->token_id, "struct") == 0)     { s->token = TK_STRUCT; return; }
        if (strcmp(s->token_id, "enum") == 0)       { s->token = TK_ENUM; return; }
        if (strcmp(s->token_id, "if") == 0)         { s->token = TK_IF; return; }
        if (strcmp(s->token_id, "else") == 0)       { s->token = TK_ELSE; return; }
        if (strcmp(s->token_id, "while") == 0)      { s->token = TK_WHILE; return; }
        if (strcmp(s->token_id, "for") == 0)        { s->token = TK_FOR; return; }
        if (strcmp(s->token_id, "do") == 0)         { s->token = TK_DO; return; }
        if (strcmp(s->token_id, "return") == 0)     { s->token = TK_RETURN; return; }
        if (strcmp(s->token_id, "break") == 0)      { s->token = TK_BREAK; return; }
        if (strcmp(s->token_id, "continue") == 0)   { s->token = TK_CONTINUE; return; }
        if (strcmp(s->token_id, "switch") == 0)     { s->token = TK_SWITCH; return; }
        if (strcmp(s->token_id, "case") == 0)       { s->token = TK_CASE; return; }
        if (strcmp(s->token_id, "default") == 0)    { s->token = TK_DEFAULT; return; }
        if (strcmp(s->token_id, "sizeof") == 0)     { s->token = TK_SIZEOF; return; }

        /* Check #define'd constants */
        int d;
        for (d = 0; d < s->n_defines; d++) {
            if (strcmp(s->token_id, s->defines[d].name) == 0) {
                s->token = TK_NUM;
                s->token_val = s->defines[d].value;
                return;
            }
        }

        s->token = TK_ID;
        return;
    }

    /* Multi-character operators */
    char c = *s->pos++;
    if (s->pos < s->src_end) {
        char c2 = *s->pos;
        if (c == '=' && c2 == '=') { s->pos++; s->token = TK_EQ; return; }
        if (c == '!' && c2 == '=') { s->pos++; s->token = TK_NE; return; }
        if (c == '<' && c2 == '=') { s->pos++; s->token = TK_LE; return; }
        if (c == '>' && c2 == '=') { s->pos++; s->token = TK_GE; return; }
        if (c == '&' && c2 == '&') { s->pos++; s->token = TK_AND; return; }
        if (c == '|' && c2 == '|') { s->pos++; s->token = TK_OR; return; }
        if (c == '<' && c2 == '<') { s->pos++; s->token = TK_SHL; return; }
        if (c == '>' && c2 == '>') { s->pos++; s->token = TK_SHR; return; }
        if (c == '+' && c2 == '+') { s->pos++; s->token = TK_INC; return; }
        if (c == '-' && c2 == '-') { s->pos++; s->token = TK_DEC; return; }
        if (c == '+' && c2 == '=') { s->pos++; s->token = TK_ADDASSIGN; return; }
        if (c == '-' && c2 == '=') { s->pos++; s->token = TK_SUBASSIGN; return; }
        if (c == '*' && c2 == '=') { s->pos++; s->token = TK_MULASSIGN; return; }
        if (c == '/' && c2 == '=') { s->pos++; s->token = TK_DIVASSIGN; return; }
        if (c == '-' && c2 == '>') { s->pos++; s->token = TK_ARROW; return; }
    }
    s->token = c;
}

static void expect(TccState* s, int tok) {
    if (s->token != tok) {
        char msg[128];
        sprintf(msg, "expected '%c' (got token %d)", tok, s->token);
        tcc_error(s, msg);
    }
    next_token(s);
}

/* ============================================================
 * Section 5: Code Generation Helpers
 * ============================================================ */

static void emit(TccState* s, int op) {
    if (s->code_pos < MAX_CODE) s->code[s->code_pos++] = op;
}

static void emit2(TccState* s, int op, int val) {
    emit(s, op);
    emit(s, val);
}

static int emit_placeholder(TccState* s) {
    int addr = s->code_pos;
    emit(s, 0);
    emit(s, 0);
    return addr;
}

static void patch(TccState* s, int addr, int op, int target) {
    s->code[addr] = op;
    s->code[addr + 1] = target;
}

static int store_string(TccState* s, const char* str) {
    int addr = s->data_pos;
    int len = strlen(str) + 1;
    if (s->data_pos + len < MAX_DATA) {
        memcpy(s->data + s->data_pos, str, len);
        s->data_pos += len;
        /* Align to 8 bytes */
        s->data_pos = (s->data_pos + 7) & ~7;
    }
    return addr;
}

/* ============================================================
 * Section 6: Symbol Table
 * ============================================================ */

static Symbol* find_symbol(TccState* s, const char* name) {
    int i;
    for (i = s->n_syms - 1; i >= 0; i--) {
        if (strcmp(s->syms[i].name, name) == 0) return &s->syms[i];
    }
    return NULL;
}

static Symbol* add_symbol(TccState* s, const char* name) {
    if (s->n_syms >= MAX_SYMS) {
        tcc_error(s, "too many symbols");
        return &s->syms[0];
    }
    Symbol* sym = &s->syms[s->n_syms++];
    strncpy(sym->name, name, MAX_ID_LEN - 1);
    sym->name[MAX_ID_LEN - 1] = '\0';
    sym->addr = 0;
    sym->is_func = 0;
    sym->n_params = 0;
    sym->n_locals = 0;
    sym->type_size = 8;
    return sym;
}

static int find_local(TccState* s, const char* name) {
    int i;
    for (i = s->n_locals - 1; i >= 0; i--) {
        if (strcmp(s->locals[i].name, name) == 0) return s->locals[i].offset;
    }
    return -1;
}

static int add_local(TccState* s, const char* name, int size) {
    if (s->n_locals >= 256) {
        tcc_error(s, "too many local variables");
        return 0;
    }
    s->local_offset += size;
    int offset = s->local_offset;
    strcpy(s->locals[s->n_locals].name, name);
    s->locals[s->n_locals].offset = offset;
    s->locals[s->n_locals].size = size;
    s->n_locals++;
    return offset;
}

/* ============================================================
 * Section 7: Parser -- Expressions
 * ============================================================ */

/* Forward declarations */
static void parse_expr(TccState* s);

static void load_rvalue(TccState* s) {
    if (s->is_lvalue) {
        emit(s, OP_LOAD);
        s->is_lvalue = 0;
    }
}

static void parse_assign_expr(TccState* s);
static void parse_statement(TccState* s);
static void parse_block(TccState* s);

static int is_type_keyword(int token) {
    return token == TK_INT || token == TK_CHAR || token == TK_LONG || token == TK_VOID;
}

/* Parse type specifier, return size (1 for char, 8 for int/long/void/pointer) */
static int parse_type(TccState* s) {
    int size = 8;
    if (s->token == TK_CHAR) size = 1;
    next_token(s);
    /* Handle pointer stars */
    while (s->token == '*') { next_token(s); size = 8; }
    return size;
}

/* Built-in function IDs */
enum {
    BI_PRINTF = 1, BI_PUTS, BI_PUTCHAR,
    BI_MALLOC, BI_FREE,
    BI_STRLEN, BI_STRCMP, BI_MEMSET, BI_MEMCPY,
    BI_STRCPY, BI_STRNCPY, BI_STRCAT
};

static int get_builtin_id(const char* name) {
    if (strcmp(name, "printf") == 0)  return BI_PRINTF;
    if (strcmp(name, "puts") == 0)    return BI_PUTS;
    if (strcmp(name, "putchar") == 0) return BI_PUTCHAR;
    if (strcmp(name, "malloc") == 0)  return BI_MALLOC;
    if (strcmp(name, "free") == 0)    return BI_FREE;
    if (strcmp(name, "strlen") == 0)  return BI_STRLEN;
    if (strcmp(name, "strcmp") == 0)   return BI_STRCMP;
    if (strcmp(name, "memset") == 0)  return BI_MEMSET;
    if (strcmp(name, "memcpy") == 0)  return BI_MEMCPY;
    if (strcmp(name, "strcpy") == 0)  return BI_STRCPY;
    if (strcmp(name, "strncpy") == 0) return BI_STRNCPY;
    if (strcmp(name, "strcat") == 0)  return BI_STRCAT;
    return 0;
}

static void parse_primary(TccState* s) {
    if (s->has_error) return;

    if (s->token == TK_NUM) {
        emit2(s, OP_IMM, (int)s->token_val);
        next_token(s);
        s->is_lvalue = 0;
    }
    else if (s->token == TK_STR) {
        int addr = store_string(s, s->token_str);
        emit2(s, OP_STR, addr);
        next_token(s);
        /* Handle adjacent string concatenation */
        while (s->token == TK_STR) {
            /* Already stored separately, just skip for simplicity */
            next_token(s);
        }
    }
    else if (s->token == TK_SIZEOF) {
        next_token(s);
        expect(s, '(');
        int sz = 8;
        if (is_type_keyword(s->token)) {
            if (s->token == TK_CHAR) sz = 1;
            next_token(s);
            while (s->token == '*') { next_token(s); sz = 8; }
        } else {
            parse_expr(s); /* evaluate but discard */
            emit(s, OP_POP);
        }
        expect(s, ')');
        emit2(s, OP_IMM, sz);
    }
    else if (s->token == '(') {
        next_token(s);
        // Each nested '(' recurses back through the entire precedence chain
        // (parse_expr -> ... -> parse_unary -> parse_postfix ->
        // parse_primary, roughly a dozen native frames). Nothing bounded that
        // depth, so a source file with enough nested parens exhausts the
        // native C call stack while compiling, before the VM even runs.
        // tcc_error() sets has_error, which the `if (s->has_error) return;`
        // at the top of this function (and equivalent checks elsewhere)
        // unwinds through on the way back out, same as any other parse error.
        if (s->paren_depth >= 200) {
            tcc_error(s, "expression nested too deeply");
            return;
        }
        s->paren_depth++;
        /* Check if this is a cast expression */
        if (is_type_keyword(s->token)) {
            parse_type(s);
            expect(s, ')');
            parse_primary(s); /* parse the casted expression */
        } else {
            parse_expr(s);
            expect(s, ')');
        }
        s->paren_depth--;
    }
    else if (s->token == TK_ID) {
        char name[MAX_ID_LEN];
        strcpy(name, s->token_id);
        next_token(s);

        /* Function call */
        if (s->token == '(') {
            next_token(s);
            int nargs = 0;
            while (s->token != ')' && s->token != TK_EOF) {
                parse_assign_expr(s);
                emit(s, OP_PUSH);
                nargs++;
                if (s->token == ',') next_token(s);
            }
            expect(s, ')');

            int bi = get_builtin_id(name);
            if (bi) {
                emit2(s, OP_BUILTIN, bi);
                emit(s, nargs);
                s->is_lvalue = 0;
            } else {
                Symbol* sym = find_symbol(s, name);
                if (!sym) {
                    /* Forward reference -- add as undefined symbol */
                    sym = add_symbol(s, name);
                    sym->is_func = 1;
                }
                emit2(s, OP_CALL, sym->addr);
                emit(s, nargs);
                s->is_lvalue = 0;
            }
        }
        /* Variable access */
        else {
            int local_off = find_local(s, name);
            if (local_off >= 0) {
                emit2(s, OP_LOCAL, local_off);
                s->is_lvalue = 1;
                /* Don't auto-load for assignment targets */
            } else {
                Symbol* sym = find_symbol(s, name);
                if (sym) {
                    emit2(s, OP_GLOBAL, sym->addr);
                    s->is_lvalue = 1;
                } else {
                    char msg[128];
                    sprintf(msg, "undefined variable '%s'", name);
                    tcc_error(s, msg);
                    emit2(s, OP_IMM, 0);
                    s->is_lvalue = 0;
                }
            }
        }
    }
    else {
        char msg[64];
        sprintf(msg, "unexpected token %d", s->token);
        tcc_error(s, msg);
        next_token(s);
        emit2(s, OP_IMM, 0);
    }
}

/* Postfix: [], ++, --, ., -> */
static void parse_postfix(TccState* s) {
    parse_primary(s);
    while (s->token == '[' || s->token == TK_INC || s->token == TK_DEC) {
        if (s->token == '[') {
            next_token(s);
            load_rvalue(s); /* load array base address */
            parse_expr(s);
            emit2(s, OP_IMM, 8);
            emit(s, OP_MUL);
            emit(s, OP_ADD); /* base + index * size */
            s->is_lvalue = 1;
            expect(s, ']');
        } else if (s->token == TK_INC || s->token == TK_DEC) {
            int op = s->token;
            next_token(s);
            emit(s, OP_DUP);   /* keep address */
            emit(s, OP_LOAD);  /* load current value */
            s->is_lvalue = 0; /* value is now on stack */
            emit2(s, OP_IMM, 1);
            emit(s, op == TK_INC ? OP_ADD : OP_SUB);
            emit(s, OP_STORE); /* store new value */
            s->is_lvalue = 0;
        }
    }
}

/* Unary: -, !, ~, *, &, ++, -- */
static void parse_unary(TccState* s) {
    if (s->token == '-') {
        next_token(s);
        parse_unary(s);
        load_rvalue(s);
        emit(s, OP_NEG);
        s->is_lvalue = 0;
    } else if (s->token == '!') {
        next_token(s);
        parse_unary(s);
        load_rvalue(s);
        emit(s, OP_NOT);
        s->is_lvalue = 0;
    } else if (s->token == '~') {
        next_token(s);
        parse_unary(s);
        load_rvalue(s);
        emit(s, OP_BNOT);
        s->is_lvalue = 0;
    } else if (s->token == '*') {
        next_token(s);
        parse_unary(s);
        load_rvalue(s);
        s->is_lvalue = 1;
    } else if (s->token == '&') {
        next_token(s);
        /* Address-of: just parse the primary and don't load */
        parse_primary(s);
    } else if (s->token == TK_INC || s->token == TK_DEC) {
        int op = s->token;
        next_token(s);
        parse_unary(s);
        emit(s, OP_DUP);
        emit(s, OP_LOAD);
        emit2(s, OP_IMM, 1);
        emit(s, op == TK_INC ? OP_ADD : OP_SUB);
        emit(s, OP_STORE);
        s->is_lvalue = 0;
    } else {
        parse_postfix(s);
    }
}

/* Multiplicative: *, /, % */
static void parse_mul(TccState* s) {
    parse_unary(s);
    while (s->token == '*' || s->token == '/' || s->token == '%') {
        int op = s->token;
        next_token(s);
        // Every sibling precedence level (parse_add, parse_shift, etc.) calls
        // load_rvalue(s) here, which only emits OP_LOAD if the left operand
        // actually was an lvalue. This unconditionally emitted OP_LOAD,
        // dereferencing the left operand's raw value as a pointer whenever it
        // wasn't a bare lvalue -- a literal, a call result, a parenthesized
        // expression, or a previous '*'/'/' in a chain (is_lvalue is forced
        // to 0 at the end of each loop iteration below). `6 * 7` dereferenced
        // address 6; `a * b * c` broke on the second '*' even for plain int
        // locals.
        load_rvalue(s);
        parse_unary(s);
        load_rvalue(s);
        s->is_lvalue = 1;
        if (op == '*') emit(s, OP_MUL);
        else if (op == '/') emit(s, OP_DIV);
        else emit(s, OP_MOD);
        s->is_lvalue = 0;
    }
}

/* Additive: +, - */
static void parse_add(TccState* s) {
    parse_mul(s);
    while (s->token == '+' || s->token == '-') {
        int op = s->token;
        next_token(s);
        load_rvalue(s);
        parse_mul(s);
        load_rvalue(s);
        emit(s, op == '+' ? OP_ADD : OP_SUB);
        s->is_lvalue = 0;
    }
}

/* Shift: <<, >> */
static void parse_shift(TccState* s) {
    parse_add(s);
    while (s->token == TK_SHL || s->token == TK_SHR) {
        int op = s->token;
        next_token(s);
        load_rvalue(s);
        parse_add(s);
        load_rvalue(s);
        emit(s, op == TK_SHL ? OP_SHL : OP_SHR);
        s->is_lvalue = 0;
    }
}

/* Relational: <, >, <=, >= */
static void parse_relational(TccState* s) {
    parse_shift(s);
    while (s->token == '<' || s->token == '>' || s->token == TK_LE || s->token == TK_GE) {
        int op = s->token;
        next_token(s);
        load_rvalue(s);
        parse_shift(s);
        load_rvalue(s);
        if (op == '<') emit(s, OP_LT);
        else if (op == '>') emit(s, OP_GT);
        else if (op == TK_LE) emit(s, OP_LE);
        else emit(s, OP_GE);
        s->is_lvalue = 0;
    }
}

/* Equality: ==, != */
static void parse_equality(TccState* s) {
    parse_relational(s);
    while (s->token == TK_EQ || s->token == TK_NE) {
        int op = s->token;
        next_token(s);
        load_rvalue(s);
        parse_relational(s);
        load_rvalue(s);
        emit(s, op == TK_EQ ? OP_EQ : OP_NE);
        s->is_lvalue = 0;
    }
}

/* Bitwise AND */
static void parse_bitand(TccState* s) {
    parse_equality(s);
    while (s->token == '&') {
        next_token(s);
        load_rvalue(s);
        parse_equality(s);
        load_rvalue(s);
        emit(s, OP_AND);
        s->is_lvalue = 0;
    }
}

/* Bitwise XOR */
static void parse_bitxor(TccState* s) {
    parse_bitand(s);
    while (s->token == '^') {
        next_token(s);
        load_rvalue(s);
        parse_bitand(s);
        load_rvalue(s);
        emit(s, OP_XOR);
        s->is_lvalue = 0;
    }
}

/* Bitwise OR */
static void parse_bitor(TccState* s) {
    parse_bitxor(s);
    while (s->token == '|') {
        next_token(s);
        load_rvalue(s);
        parse_bitxor(s);
        load_rvalue(s);
        emit(s, OP_OR);
        s->is_lvalue = 0;
    }
}

/* Logical AND */
static void parse_logand(TccState* s) {
    parse_bitor(s);
    while (s->token == TK_AND) {
        next_token(s);
        load_rvalue(s);
        parse_bitor(s);
        load_rvalue(s);
        emit(s, OP_LAND);
        s->is_lvalue = 0;
    }
}

/* Logical OR */
static void parse_logor(TccState* s) {
    parse_logand(s);
    while (s->token == TK_OR) {
        next_token(s);
        load_rvalue(s);
        parse_logand(s);
        load_rvalue(s);
        emit(s, OP_LOR);
        s->is_lvalue = 0;
    }
}

/* Ternary: ? : */
static void parse_ternary(TccState* s) {
    parse_logor(s);
    if (s->token == '?') {
        next_token(s);
        load_rvalue(s);
        int jz = emit_placeholder(s);
        parse_expr(s);
        int jmp_end = emit_placeholder(s);
        expect(s, ':');
        patch(s, jz, OP_JZ, s->code_pos);
        parse_ternary(s);
        patch(s, jmp_end, OP_JMP, s->code_pos);
        s->is_lvalue = 0;
    }
}

/* Assignment expression */
static void parse_assign_expr(TccState* s) {
    parse_ternary(s);

    if (s->token == '=') {
        next_token(s);
        /* top of stack has the lvalue address */
        parse_assign_expr(s);
        load_rvalue(s); /* load rvalue */
        emit(s, OP_STORE);
        s->is_lvalue = 0;
        /* After store, the value remains for chained assignment */
    }
    else if (s->token == TK_ADDASSIGN || s->token == TK_SUBASSIGN ||
             s->token == TK_MULASSIGN || s->token == TK_DIVASSIGN) {
        int op = s->token;
        next_token(s);
        emit(s, OP_DUP);   /* duplicate address */
        emit(s, OP_LOAD);  /* load current value */
        s->is_lvalue = 0;
        parse_assign_expr(s);
        load_rvalue(s);
        if (op == TK_ADDASSIGN) emit(s, OP_ADD);
        else if (op == TK_SUBASSIGN) emit(s, OP_SUB);
        else if (op == TK_MULASSIGN) emit(s, OP_MUL);
        else emit(s, OP_DIV);
        emit(s, OP_STORE);
        s->is_lvalue = 0;
    }
}

/* Full expression (comma operator) */
static void parse_expr(TccState* s) {
    parse_assign_expr(s);
    while (s->token == ',') {
        next_token(s);
        emit(s, OP_POP);
        parse_assign_expr(s);
    }
}

/* ============================================================
 * Section 8: Parser -- Statements
 * ============================================================ */

static void parse_statement(TccState* s) {
    if (s->has_error) return;

    if (s->token == '{') {
        parse_block(s);
    }
    else if (s->token == TK_IF) {
        next_token(s);
        expect(s, '(');
        parse_expr(s);
        load_rvalue(s);
        expect(s, ')');
        int jz = emit_placeholder(s);
        parse_statement(s);
        if (s->token == TK_ELSE) {
            next_token(s);
            int jmp_end = emit_placeholder(s);
            patch(s, jz, OP_JZ, s->code_pos);
            parse_statement(s);
            patch(s, jmp_end, OP_JMP, s->code_pos);
        } else {
            patch(s, jz, OP_JZ, s->code_pos);
        }
    }
    else if (s->token == TK_WHILE) {
        next_token(s);
        int loop_start = s->code_pos;
        expect(s, '(');
        parse_expr(s);
        load_rvalue(s);
        expect(s, ')');
        int jz = emit_placeholder(s);

        // break_stack/continue_stack are fixed int[64]; unlike add_local's
        // "too many local variables" check, nothing guarded this increment,
        // so 64+ levels of nested loops would index past the array and
        // corrupt adjacent TccState fields (loop_depth itself next, then
        // has_error/error_msg).
        if (s->loop_depth < 63) s->loop_depth++;
        else tcc_error(s, "loops nested too deeply");
        s->break_stack[s->loop_depth] = -1;
        s->continue_stack[s->loop_depth] = loop_start;
        parse_statement(s);
        emit2(s, OP_JMP, loop_start);
        patch(s, jz, OP_JZ, s->code_pos);
        if (s->break_stack[s->loop_depth] >= 0)
            patch(s, s->break_stack[s->loop_depth], OP_JMP, s->code_pos);
        s->loop_depth--;
    }
    else if (s->token == TK_FOR) {
        next_token(s);
        expect(s, '(');
        /* Init */
        if (s->token != ';') {
            if (is_type_keyword(s->token)) {
                /* Variable declaration in for-init */
                int size = parse_type(s);
                if (s->token == TK_ID) {
                    int off = add_local(s, s->token_id, size);
                    next_token(s);
                    if (s->token == '=') {
                        next_token(s);
                        emit2(s, OP_LOCAL, off);
                        s->is_lvalue = 1;
                        parse_assign_expr(s);
                        load_rvalue(s);
                        emit(s, OP_STORE);
                        s->is_lvalue = 0;
                        emit(s, OP_POP);
                    }
                }
            } else {
                parse_expr(s);
                emit(s, OP_POP);
            }
        }
        expect(s, ';');
        /* Condition */
        int loop_start = s->code_pos;
        int jz = -1;
        if (s->token != ';') {
            parse_expr(s);
            load_rvalue(s);
            jz = emit_placeholder(s);
        }
        expect(s, ';');
        /* Save increment expression position */
        int incr_jmp = -1;
        if (s->token != ')') {
            incr_jmp = emit_placeholder(s); /* jump over increment to body */
        }
        int incr_code = s->code_pos;
        if (s->token != ')') {
            /* We already jumped past, so parse the increment here */
            parse_expr(s);
            emit(s, OP_POP);
            emit2(s, OP_JMP, loop_start); /* jump back to condition */
        }
        expect(s, ')');

        /* Body */
        // See the matching check in the while-loop case above.
        if (s->loop_depth < 63) s->loop_depth++;
        else tcc_error(s, "loops nested too deeply");
        s->break_stack[s->loop_depth] = -1;
        s->continue_stack[s->loop_depth] = (incr_jmp != -1) ? incr_code : loop_start;

        if (incr_jmp != -1) {
            patch(s, incr_jmp, OP_JMP, s->code_pos);
        }

        parse_statement(s);
        
        if (incr_jmp != -1) {
            emit2(s, OP_JMP, incr_code);
        } else {
            emit2(s, OP_JMP, loop_start);
        }

        if (jz >= 0) patch(s, jz, OP_JZ, s->code_pos);
        if (s->break_stack[s->loop_depth] >= 0)
            patch(s, s->break_stack[s->loop_depth], OP_JMP, s->code_pos);
        s->loop_depth--;
    }
    else if (s->token == TK_RETURN) {
        next_token(s);
        if (s->token != ';') {
            parse_expr(s);
            load_rvalue(s);
        } else {
            emit2(s, OP_IMM, 0);
        }
        // No OP_LEAVE here: OP_RET already does sp = bp itself (see its VM
        // case). Emitting OP_LEAVE first reset sp = bp BEFORE OP_RET read the
        // return value we just pushed above bp -- so OP_RET's `*--sp` popped
        // bp[-1] (the caller's saved base pointer, placed there by OP_CALL)
        // instead of the actual return value. Every function that returned a
        // value returned garbage; this affected every call site.
        emit(s, OP_RET);
        expect(s, ';');
    }
    else if (s->token == TK_BREAK) {
        next_token(s);
        if (s->loop_depth > 0) {
            s->break_stack[s->loop_depth] = emit_placeholder(s);
        }
        expect(s, ';');
    }
    else if (s->token == TK_CONTINUE) {
        next_token(s);
        if (s->loop_depth > 0) {
            emit2(s, OP_JMP, s->continue_stack[s->loop_depth]);
        }
        expect(s, ';');
    }
    else if (is_type_keyword(s->token)) {
        /* Local variable declaration */
        int size = parse_type(s);
        while (s->token == TK_ID) {
            int off = add_local(s, s->token_id, size);
            next_token(s);

            /* Array declaration */
            if (s->token == '[') {
                next_token(s);
                /* Parse array size -- allocate on data segment. Computed as
                 * `long` first (token_val is a long) so a huge element count
                 * can't silently wrap the multiplication itself before the
                 * bounds check below ever sees it. */
                long arr_size = 8; /* default */
                if (s->token == TK_NUM) {
                    arr_size = s->token_val * size;
                    next_token(s);
                }
                expect(s, ']');
                /* Store array base address in the local. Unlike store_string
                 * (which bounds-checks data_pos + len against MAX_DATA), this
                 * had no check at all: data_pos could grow arbitrarily past
                 * the fixed 32KB `data[MAX_DATA]` field, and every later
                 * global/string address computed from data_pos would then
                 * point outside data[] -- and potentially outside the whole
                 * TccState allocation -- silently corrupting unrelated
                 * memory the first time that array was indexed near its end. */
                if (arr_size < 0 || arr_size > MAX_DATA || s->data_pos + arr_size >= MAX_DATA) {
                    tcc_error(s, "array too large for data segment");
                    arr_size = 0;
                }
                int arr_addr = s->data_pos;
                s->data_pos += (int)arr_size;
                s->data_pos = (s->data_pos + 7) & ~7;
                emit2(s, OP_LOCAL, off);
                emit2(s, OP_IMM, arr_addr);
                emit(s, OP_STORE);
                emit(s, OP_POP);
            }
            /* Initializer */
            else if (s->token == '=') {
                next_token(s);
                emit2(s, OP_LOCAL, off);
                parse_assign_expr(s);
                // OP_STORE expects [address, value] on the stack. The
                // unconditional OP_LOAD here treated whatever the initializer
                // expression left on top of the stack as an ADDRESS to
                // dereference, regardless of whether it actually was one.
                // For a literal or any other non-lvalue initializer (e.g.
                // "int x = 5;"), that value IS the value to store -- loading
                // it first dereferenced address 5 instead of just storing 5.
                load_rvalue(s);
                emit(s, OP_STORE);
                emit(s, OP_POP);
            }

            if (s->token == ',') next_token(s);
            else break;
        }
        expect(s, ';');
    }
    else if (s->token == ';') {
        next_token(s); /* empty statement */
    }
    else {
        /* Expression statement */
        parse_expr(s);
        emit(s, OP_POP);
        expect(s, ';');
    }
}

static void parse_block(TccState* s) {
    expect(s, '{');
    while (s->token != '}' && s->token != TK_EOF && !s->has_error) {
        parse_statement(s);
    }
    expect(s, '}');
}

/* ============================================================
 * Section 9: Parser -- Top-Level Declarations
 * ============================================================ */

static void parse_program(TccState* s) {
    next_token(s);

    while (s->token != TK_EOF && !s->has_error) {
        /* Skip standalone semicolons */
        if (s->token == ';') { next_token(s); continue; }

        if (!is_type_keyword(s->token)) {
            tcc_error(s, "expected type declaration at top level");
            break;
        }

        int size = parse_type(s);
        if (s->token != TK_ID) {
            tcc_error(s, "expected identifier");
            break;
        }

        char name[MAX_ID_LEN];
        strcpy(name, s->token_id);
        next_token(s);

        /* Function definition or declaration */
        if (s->token == '(') {
            next_token(s);
            Symbol* sym = find_symbol(s, name);
            if (!sym) sym = add_symbol(s, name);
            sym->is_func = 1;
            sym->addr = s->code_pos;

            /* Parse parameters */
            s->n_locals = 0;
            s->local_offset = 0;
            int nparams = 0;
            while (s->token != ')' && s->token != TK_EOF) {
                if (is_type_keyword(s->token)) {
                    int psize = parse_type(s);
                    if (s->token == TK_ID) {
                        add_local(s, s->token_id, psize);
                        next_token(s);
                    }
                }
                nparams++;
                if (s->token == ',') next_token(s);
            }
            expect(s, ')');
            sym->n_params = nparams;

            if (s->token == ';') {
                /* Forward declaration */
                next_token(s);
                continue;
            }

            /* Function body */
            int enter_addr = s->code_pos;
            emit2(s, OP_ENTER, 0); /* placeholder for local count */

            parse_block(s);

            /* Default return 0 */
            emit2(s, OP_IMM, 0);
            // See the OP_RET comment in the explicit `return` case above:
            // OP_LEAVE must not run before OP_RET, which already does its
            // own sp = bp after popping the return value.
            emit(s, OP_RET);

            /* Patch ENTER with actual local count */
            s->code[enter_addr + 1] = s->local_offset;
            sym->n_locals = s->n_locals;
        }
        /* Global variable */
        else {
            Symbol* sym = add_symbol(s, name);
            sym->is_func = 0;
            sym->addr = s->data_pos;
            sym->type_size = size;
            s->data_pos += 8;
            s->data_pos = (s->data_pos + 7) & ~7;

            if (s->token == '=') {
                next_token(s);
                if (s->token == TK_NUM) {
                    /* Store initial value in data segment */
                    long* p = (long*)(s->data + sym->addr);
                    *p = s->token_val;
                    next_token(s);
                }
            }
            if (s->token == ',') {
                next_token(s);
                continue; /* another var in same declaration */
            }
            expect(s, ';');
        }
    }
}

/* ============================================================
 * Section 10: VM Executor
 * ============================================================ */

static int vm_execute(TccState* s) {
    long stack[MAX_STACK];
    long* sp = stack;     /* stack pointer */
    long* bp = stack;     /* base pointer */
    long* asp = stack + MAX_STACK - 256; /* argument stack */
    int   pc;
    int   ax = 0;         /* accumulator */
    int   cycle = 0;

    /* Find main() entry point */
    Symbol* main_sym = find_symbol(s, "main");
    if (!main_sym || !main_sym->is_func) {
        printf("tcc: error: undefined reference to 'main'\n");
        return 1;
    }

    /* Push a sentinel call frame before jumping into main(), exactly like
     * OP_CALL does for a nested call. Without this, main()'s eventual OP_RET
     * pops its "saved pc"/"saved bp" from one and two slots *before* the
     * start of stack[] (sp and bp both start equal to `stack`, so OP_RET's
     * sp = bp; bp = *--sp; pc = *--sp; reads stack[-1] and stack[-2]) --
     * an out-of-bounds read that also corrupts adjacent locals in this
     * function via the out-of-bounds write right after. The garbage pc read
     * back is essentially never 0, so the `pc == 0` "returned from main"
     * check misses too, and the VM goes on to execute whatever garbage
     * bytecode offset that pc happened to be. pc=0 here guarantees that
     * check fires correctly the moment main() returns. */
    *sp++ = 0;
    *sp++ = (long)bp;
    bp = sp;
    pc = main_sym->addr;

    /* VM execution loop */
    while (cycle < 10000000) { /* safety limit */
        cycle++;
        if (pc < 0 || pc >= s->code_pos) {
            printf("tcc: runtime error: PC out of bounds (%d)\n", pc);
            return 1;
        }

        // Every push path (OP_IMM/OP_STR/OP_LOCAL/OP_GLOBAL, OP_CALL's
        // pc/bp/args, OP_BUILTIN's result, OP_RET, OP_DUP, OP_PUSH) writes via
        // `*sp++ = ...` with no bounds check against stack+MAX_STACK. Nothing
        // here previously limited recursion depth, so e.g. `fib(30)` or
        // straightforward unbounded recursion runs sp past the end of this
        // 8192-long array -- and since `stack` is a local of this function,
        // that's a real native-stack-smashing write, not just a VM-level
        // failure, and the 10,000,000-cycle limit above does nothing to stop
        // it (each call only costs a handful of cycles). The largest single
        // instruction can push is OP_CALL's 2 + nargs; 256 slots of margin,
        // checked once per instruction fetch, comfortably covers any
        // realistic argument count before the *next* check would catch it.
        if (sp >= stack + MAX_STACK - 256) {
            printf("tcc: runtime error: stack overflow (recursion too deep?)\n");
            return 1;
        }

        int op = s->code[pc++];

        switch (op) {
        case OP_NOP:
            break;

        case OP_IMM:
            ax = s->code[pc++];
            *sp++ = ax;
            break;

        case OP_STR:
            ax = s->code[pc++];
            *sp++ = (long)(s->data + ax);
            break;

        case OP_LOCAL: {
            int offset = s->code[pc++];
            // add_local()/local_offset are BYTE offsets (Symbol.type_size is
            // 1 for char, 8 for int/long/pointer; OP_ENTER above reserves
            // (nlocals_bytes+7)/8 slots to match). `bp` is a long*, so plain
            // pointer arithmetic (bp + offset) scaled offset by another 8 --
            // e.g. the second int local (byte offset 8) resolved to bp+8
            // longs = 64 bytes away instead of bp+1 long = 8 bytes away.
            // Every local/parameter read or write landed on the wrong slot.
            // Casting to char* first makes the addition byte-exact, matching
            // OP_GLOBAL's (already-correct) `s->data + offset` below.
            *sp++ = (long)((char*)bp + offset);
            break;
        }

        case OP_GLOBAL: {
            int offset = s->code[pc++];
            *sp++ = (long)(s->data + offset);
            break;
        }

        case OP_LOAD:
            if (sp > stack) {
                long addr = sp[-1];
                /* Check if the address points into our stack or data */
                if (addr != 0) {
                    sp[-1] = *(long*)addr;
                }
            }
            break;

        case OP_LOADB:
            if (sp > stack) {
                long addr = sp[-1];
                if (addr != 0) sp[-1] = *(char*)addr;
            }
            break;

        case OP_STORE:
            if (sp - stack >= 2) {
                long val = *--sp;
                long addr = sp[-1];
                if (addr != 0) {
                    *(long*)addr = val;
                }
                sp[-1] = val; /* leave value on stack */
            }
            break;

        case OP_STOREB:
            if (sp - stack >= 2) {
                long val = *--sp;
                long addr = sp[-1];
                if (addr != 0) *(char*)addr = (char)val;
                sp[-1] = val;
            }
            break;

        case OP_PUSH:
            if (sp > stack) *--asp = *--sp;
            break;

        case OP_JMP:
            pc = s->code[pc];
            break;

        case OP_JZ:
            if (sp > stack && *--sp == 0) pc = s->code[pc];
            else pc++;
            break;

        case OP_JNZ:
            if (sp > stack && *--sp != 0) pc = s->code[pc];
            else pc++;
            break;

        case OP_CALL: {
            int target = s->code[pc++];
            int nargs = s->code[pc++];
            /* Push return address and current BP */
            *sp++ = pc;
            *sp++ = (long)bp;
            bp = sp;
            /* Copy arguments from arg stack to locals area */
            int i;
            for (i = 0; i < nargs; i++) {
                *sp++ = *asp++;
            }
            pc = target;
            break;
        }

        case OP_BUILTIN: {
            int id = s->code[pc++];
            int nargs = s->code[pc++];
            long args[16];
            int i;
            for (i = 0; i < nargs && i < 16; i++) {
                args[i] = *asp++;
            }

            long result = 0;
            switch (id) {
            case BI_PRINTF: {
                if (nargs > 0) {
                    const char* fmt = (const char*)args[0];
                    /* Simple printf with up to 8 arguments */
                    switch (nargs - 1) {
                    case 0: result = printf("%s", fmt); break;
                    case 1: result = printf(fmt, args[1]); break;
                    case 2: result = printf(fmt, args[1], args[2]); break;
                    case 3: result = printf(fmt, args[1], args[2], args[3]); break;
                    case 4: result = printf(fmt, args[1], args[2], args[3], args[4]); break;
                    default: result = printf(fmt, args[1], args[2], args[3], args[4]); break;
                    }
                }
                break;
            }
            case BI_PUTS:
                if (nargs > 0) { printf("%s\n", (const char*)args[0]); result = 0; }
                break;
            case BI_PUTCHAR:
                if (nargs > 0) { char c = (char)args[0]; printf("%c", c); result = c; }
                break;
            case BI_MALLOC:
                if (nargs > 0) result = (long)malloc((size_t)args[0]);
                break;
            case BI_FREE:
                if (nargs > 0) free((void*)args[0]);
                break;
            case BI_STRLEN:
                if (nargs > 0) result = (long)strlen((const char*)args[0]);
                break;
            case BI_STRCMP:
                if (nargs >= 2) result = (long)strcmp((const char*)args[0], (const char*)args[1]);
                break;
            case BI_MEMSET:
                if (nargs >= 3) result = (long)memset((void*)args[0], (int)args[1], (size_t)args[2]);
                break;
            case BI_MEMCPY:
                if (nargs >= 3) result = (long)memcpy((void*)args[0], (const void*)args[1], (size_t)args[2]);
                break;
            case BI_STRCPY:
                if (nargs >= 2) result = (long)strcpy((char*)args[0], (const char*)args[1]);
                break;
            case BI_STRNCPY:
                if (nargs >= 3) result = (long)strncpy((char*)args[0], (const char*)args[1], (size_t)args[2]);
                break;
            case BI_STRCAT:
                if (nargs >= 2) {
                    char* d = (char*)args[0];
                    const char* sc = (const char*)args[1];
                    while (*d) d++;
                    while (*sc) *d++ = *sc++;
                    *d = '\0';
                    result = args[0];
                }
                break;
            }
            *sp++ = result;
            break;
        }

        case OP_RET: {
            long retval = (sp > stack) ? *--sp : 0;
            sp = bp;
            bp = (long*)*--sp;
            pc = (int)*--sp;
            *sp++ = retval;
            if (pc == 0) return (int)retval; /* returned from main */
            break;
        }

        case OP_ENTER: {
            int nlocals = s->code[pc++];
            /* Reserve space for locals (in units of long) */
            sp += (nlocals + 7) / 8;
            break;
        }

        case OP_LEAVE:
            sp = bp;
            break;

        /* Arithmetic */
        case OP_ADD:  if (sp - stack >= 2) { long b = *--sp; sp[-1] += b; } break;
        case OP_SUB:  if (sp - stack >= 2) { long b = *--sp; sp[-1] -= b; } break;
        case OP_MUL:  if (sp - stack >= 2) { long b = *--sp; sp[-1] *= b; } break;
        case OP_DIV:  if (sp - stack >= 2) { long b = *--sp; if(b) sp[-1] /= b; } break;
        case OP_MOD:  if (sp - stack >= 2) { long b = *--sp; if(b) sp[-1] %= b; } break;

        /* Comparison */
        case OP_EQ:   if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] == b); } break;
        case OP_NE:   if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] != b); } break;
        case OP_LT:   if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] < b); } break;
        case OP_GT:   if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] > b); } break;
        case OP_LE:   if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] <= b); } break;
        case OP_GE:   if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] >= b); } break;

        /* Bitwise */
        case OP_AND:  if (sp - stack >= 2) { long b = *--sp; sp[-1] &= b; } break;
        case OP_OR:   if (sp - stack >= 2) { long b = *--sp; sp[-1] |= b; } break;
        case OP_XOR:  if (sp - stack >= 2) { long b = *--sp; sp[-1] ^= b; } break;
        case OP_SHL:  if (sp - stack >= 2) { long b = *--sp; sp[-1] <<= b; } break;
        case OP_SHR:  if (sp - stack >= 2) { long b = *--sp; sp[-1] >>= b; } break;

        /* Logical */
        case OP_LAND: if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] && b); } break;
        case OP_LOR:  if (sp - stack >= 2) { long b = *--sp; sp[-1] = (sp[-1] || b); } break;

        /* Unary */
        case OP_NEG:  if (sp > stack) sp[-1] = -sp[-1]; break;
        case OP_NOT:  if (sp > stack) sp[-1] = !sp[-1]; break;
        case OP_BNOT: if (sp > stack) sp[-1] = ~sp[-1]; break;

        case OP_POP:
            if (sp > stack) sp--;
            break;

        case OP_DUP:
            if (sp > stack) { *sp = sp[-1]; sp++; }
            break;

        case OP_EXIT:
            return (int)((sp > stack) ? sp[-1] : 0);

        default:
            printf("tcc: runtime error: unknown opcode %d at PC=%d\n", op, pc - 1);
            return 1;
        }
    }

    printf("tcc: runtime error: execution limit exceeded\n");
    return 1;
}

/* ============================================================
 * Section 11: Public API
 * ============================================================ */

int tcc_main_string(const char* code) {
    TccState* s = (TccState*)malloc(sizeof(TccState));
    if (!s) {
        printf("tcc: error: out of memory\n");
        return 1;
    }

    memset(s, 0, sizeof(TccState));
    s->src = code;
    s->src_end = code + strlen(code);
    s->pos = code;
    s->line = 1;
    s->data_pos = 256; /* reserve space for globals at start */

    parse_program(s);

    if (s->has_error) {
        printf("%s\n", s->error_msg);
        free(s);
        return 1;
    }

    int result = vm_execute(s);
    free(s);
    return result;
}

int tcc_main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: tcc <filename.c>\n");
        return 1;
    }

    /* Read file from VFS */
    extern int open(const char* path, int flags);
    extern int read(int fd, void* buf, int count);
    extern int close(int fd);
    extern int lseek(int fd, int offset, int whence);

    int fd = open(argv[1], 0);
    if (fd < 0) {
        printf("tcc: cannot open '%s'\n", argv[1]);
        return 1;
    }

    /* Get file size */
    int size = lseek(fd, 0, 2); /* SEEK_END */
    lseek(fd, 0, 0);            /* SEEK_SET */

    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        close(fd);
        printf("tcc: out of memory\n");
        return 1;
    }

    read(fd, buf, size);
    buf[size] = '\0';
    close(fd);

    int result = tcc_main_string(buf);
    free(buf);
    return result;
}
