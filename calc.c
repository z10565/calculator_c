// Zarina Alliyarova 251ADB103
// Optional GitHub URL: https://github.com/z10565/calculator_c
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: RTU calculator lab specification and its calc-tests.zip;
//   ChatGPT (OpenAI, 2026-10-19, this conversation). https://chatgpt.com/share/6ade5136-286c-83ed-bbb6-d541fddcd39c
//   ChatGPT assisted with the tokenizer, recursive-descent parser, safe file
//   buffering, error handling, and command-line processing; review and adapt.
// Websites: https://www.w3schools.com/c/c_structs.php
// https://www.geeksforgeeks.org/enum-in-c/
// https://www.geeksforgeeks.org/typedef-in-c/
// https://www.geeksforgeeks.org/c/dangling-void-null-wild-pointers/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define STUDENT_NAME "zarina"
#define STUDENT_LASTNAME "alliyarova"
#define STUDENT_ID "251ADB103"

typedef enum {
    T_END, T_NUMBER, T_PLUS, T_MINUS, T_MUL, T_DIV, T_POWER,
    T_OPEN, T_CLOSE, T_INVALID
} TokenType;

typedef struct {
    int is_float;
    long long integer;
    double floating;
} Value;

typedef struct {
    TokenType type;
    Value number;
    size_t position;             /* 1-based position in the entire input file */
} Token;

typedef struct {
    const char *line;
    size_t length;
    size_t cursor;
    size_t line_offset;          /* num of bytes before this line */
    size_t error;                /* 0 means that no error occurred */
    Token current;
} Parser;

static Value integer_value(long long x)
{
    Value v = {0, x, (double)x};
    return v;
}

static Value float_value(double x)
{
    Value v = {1, 0, x};
    return v;
}

static double as_double(Value v)
{
    return v.is_float ? v.floating : (double)v.integer;
}

static void set_error(Parser *p, size_t pos)
{
    /* Preserve the 1st error: subsequent parser failures must not replace it. */
    if (p->error == 0)
        p->error = pos;
}

/*
 * Token positions are computed from the original line offset, not from a
 * whitespace-stripped expression. So comments, blanks, and newlines
 * still count toward the file-wide, 1-based error position.
 */
static void next_token(Parser *p)
{
    size_t start;
    const char *s = p->line;
    Token *t = &p->current;

    while (p->cursor < p->length &&
           (s[p->cursor] == ' ' || s[p->cursor] == '\t'))
        p->cursor++;

    start = p->cursor;
    t->position = p->line_offset + start + 1;
    t->type = T_END;

    if (start == p->length)
        return;                 /* Points at '\n', or EOF + 1. */

    if (isdigit((unsigned char)s[start])) {
        size_t i = start;
        int floating = 0;
        while (i < p->length && isdigit((unsigned char)s[i]))
            i++;
        if (i + 1 < p->length && s[i] == '.' &&
            isdigit((unsigned char)s[i + 1])) {
            floating = 1;
            i += 2;
            while (i < p->length && isdigit((unsigned char)s[i]))
                i++;
        }
        t->type = T_NUMBER;
        t->number = floating ? float_value(strtod(s + start, NULL))
                             : integer_value(strtoll(s + start, NULL, 10));
        p->cursor = i;
        return;
    }

    p->cursor++;
    switch (s[start]) {
        case '+': t->type = T_PLUS; break;
        case '-': t->type = T_MINUS; break;
        case '/': t->type = T_DIV; break;
        case '(': t->type = T_OPEN; break;
        case ')': t->type = T_CLOSE; break;
        case '*':
            if (p->cursor < p->length && s[p->cursor] == '*') {
                t->type = T_POWER;
                p->cursor++;
            } else {
                t->type = T_MUL;
            }
            break;
        default: t->type = T_INVALID; break;
    }
}

static Value expression(Parser *p);
static Value power(Parser *p);

static Value primary(Parser *p)
{
    Value result = integer_value(0);
    if (p->error)
        return result;

    if (p->current.type == T_NUMBER) {
        result = p->current.number;
        next_token(p);
    } else if (p->current.type == T_OPEN) {
        next_token(p);
        result = expression(p);
        if (p->error)
            return result;
        if (p->current.type != T_CLOSE) {
            set_error(p, p->current.position); /* Missing ')' or bad token. */
            return result;
        }
        next_token(p);
    } else {
        /* An operand was expected, including after an operator or '('. */
        set_error(p, p->current.position);
    }
    return result;
}

static Value power(Parser *p)
{
    Value left = primary(p);
    if (p->error)
        return left;

    if (p->current.type == T_POWER) {
        size_t operator_pos = p->current.position;
        size_t rhs_pos;
        Value right;
        next_token(p);
        rhs_pos = p->current.position;

        // Recursing on the RIGHT makes 2**3**2 equal 2**(3**2), not (2**3)**2.//

        right = power(p);
        if (p->error)
            return left;

        if (!left.is_float && !right.is_float) {
            unsigned long long exponent;
            long long base = left.integer;
            long long answer = 1;
            if (right.integer < 0) {
                set_error(p, rhs_pos); // Exponent starts here, possibly '('. //

                return left;
            }
            exponent = (unsigned long long)right.integer;
            // Squaring repeatedly avoids O(exponent) multiplication. //
            while (exponent > 0) {
                if (exponent & 1ULL)
                    answer *= base;
                exponent >>= 1;
                if (exponent > 0)
                    base *= base;
            }
            return integer_value(answer);
        }
        {
            double answer = pow(as_double(left), as_double(right));
            if (!isfinite(answer))
                set_error(p, operator_pos);
            return float_value(answer);
        }
    }
    return left;
}

static Value term(Parser *p)
{
    Value left = power(p);
    while (!p->error && (p->current.type == T_MUL || p->current.type == T_DIV)) {
        TokenType operation = p->current.type;
        size_t operator_pos = p->current.position;
        size_t rhs_pos;
        Value right;
        next_token(p);
        rhs_pos = p->current.position;
        right = power(p);
        if (p->error)
            break;

        if (operation == T_DIV &&
            (right.is_float ? right.floating == 0.0 : right.integer == 0)) {
            set_error(p, rhs_pos); // Division-by-zero points at the operand.
            break;
        }

        // A float anywhere in the operation promotes both values to double. //
        if (left.is_float || right.is_float) {
            double a = as_double(left), b = as_double(right);
            double answer = operation == T_MUL ? a * b : a / b;
            if (!isfinite(answer)) {
                set_error(p, operator_pos);
                break;
            }
            left = float_value(answer);
        } else if (operation == T_MUL) {
            left = integer_value(left.integer * right.integer);
        } else {
            left = integer_value(left.integer / right.integer);
        }
    }
    return left;
}

/*
 * expression -> term -> power -> primary implements operator precedence:
 * each level consumes its own operators and delegates higher-precedence ones.
 * Loops implement left-associativity for +,-,*,/; power() is recursive.
 */
static Value expression(Parser *p)
{
    Value left = term(p);
    while (!p->error && (p->current.type == T_PLUS || p->current.type == T_MINUS)) {
        TokenType operation = p->current.type;
        size_t operator_pos = p->current.position;
        Value right;
        next_token(p);
        right = term(p);
        if (p->error)
            break;
        if (left.is_float || right.is_float) {
            double a = as_double(left), b = as_double(right);
            double answer = operation == T_PLUS ? a + b : a - b;
            if (!isfinite(answer)) {
                set_error(p, operator_pos);
                break;
            }
            left = float_value(answer);
        } else {
            left = integer_value(operation == T_PLUS
                                 ? left.integer + right.integer
                                 : left.integer - right.integer);
        }
    }
    return left;
}

// Return 1 for a line, 0 at EOF, and -1 for an allocation/read error. //
static int read_line(FILE *in, char **data, size_t *capacity,
                     size_t *length, int *had_newline)
{
    int ch;
    size_t n = 0;
    *had_newline = 0;
    while ((ch = fgetc(in)) != EOF) {
        if (ch == '\n') {
            *had_newline = 1;
            break;
        }
        if (n + 1 >= *capacity) {
            /*
             * Doubling capacity is amortized O(n): no repeated whole-line
             * copies when a file contains multi-megabyte expressions.
             */
            size_t new_capacity = *capacity ? *capacity * 2 : 256;
            char *expanded;
            if (new_capacity <= *capacity)
                return -1;
            expanded = realloc(*data, new_capacity);
            if (!expanded)
                return -1;
            *data = expanded;
            *capacity = new_capacity;
        }
        (*data)[n++] = (char)ch;
    }
    if (ferror(in))
        return -1;
    if (ch == EOF && n == 0)
        return 0;
    if (n + 1 >= *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 256;
        char *expanded;
        if (new_capacity <= *capacity)
            return -1;
        expanded = realloc(*data, new_capacity);
        if (!expanded)
            return -1;
        *data = expanded;
        *capacity = new_capacity;
    }
    (*data)[n] = '\0';
    *length = n;
    return 1;
}

static char *combine(const char *a, const char *b, const char *c)
{
    size_t na = strlen(a), nb = strlen(b), nc = strlen(c);
    char *p = malloc(na + nb + nc + 1);
    if (!p)
        return NULL;
    memcpy(p, a, na);
    memcpy(p + na, b, nb);
    memcpy(p + na + nb, c, nc + 1);
    return p;
}

static char *join_path(const char *dir, const char *filename)
{
    size_t n = strlen(dir);
    return combine(dir, n && dir[n - 1] == '/' ? "" : "/", filename);
}

static char *basename_copy(const char *path, int strip_txt)
{
    size_t len = strlen(path);
    const char *begin;
    char *out;
    while (len > 1 && path[len - 1] == '/')
        len--;
    begin = path;
    while (len > 0 && path[len - 1] != '/')
        len--;
    begin += len;
    len = strlen(begin);
    while (len > 0 && begin[len - 1] == '/')
        len--;
    if (strip_txt && len >= 4 && strncmp(begin + len - 4, ".txt", 4) == 0)
        len -= 4;
    out = malloc(len + 1);
    if (out) {
        memcpy(out, begin, len);
        out[len] = '\0';
    }
    return out;
}

static int ensure_directory(const char *directory)
{
    struct stat information;
    if (mkdir(directory, 0777) == 0)
        return 0;
    if (errno == EEXIST && stat(directory, &information) == 0 &&
        S_ISDIR(information.st_mode))
        return 0;
    fprintf(stderr, "Cannot create output directory '%s': %s\n",
            directory, strerror(errno));
    return -1;
}

/*
 * Results go first to an unnamed temporary file. We publish them only after
 * successfully parsing the entire input, so a late error replaces EVERY
 * previous result with exactly one ERROR:<absolute-position> line.
 */
static int evaluate_file(const char *input_path, const char *output_path)
{
    FILE *in = NULL, *stage = NULL, *out = NULL;
    char *line = NULL;
    size_t capacity = 0, offset = 0, length = 0, error = 0;
    int had_newline, state, status = -1;
    unsigned char buffer[8192];
    size_t bytes;

    in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "Cannot open input '%s': %s\n", input_path, strerror(errno));
        goto cleanup;
    }
    stage = tmpfile();
    if (!stage) {
        fprintf(stderr, "Cannot create temporary file: %s\n", strerror(errno));
        goto cleanup;
    }

    while ((state = read_line(in, &line, &capacity, &length, &had_newline)) == 1) {
        size_t i = 0;
        Parser p;
        Value result;
        while (i < length && (line[i] == ' ' || line[i] == '\t'))
            i++;
        if (i == length || line[i] == '#') {
            offset += length + (size_t)had_newline;
            continue;
        }
        p.line = line;
        p.length = length;
        p.cursor = 0;
        p.line_offset = offset;
        p.error = 0;
        next_token(&p);
        result = expression(&p);
        if (!p.error && p.current.type != T_END)
            set_error(&p, p.current.position); // Extra ')' or trailing junk. //
        if (p.error) {
            error = p.error;
            break;
        }
        if (result.is_float) {
            if (fprintf(stage, "%.15g\n", result.floating) < 0)
                break;
        } else if (fprintf(stage, "%lld\n", result.integer) < 0) {
            break;
        }
        offset += length + (size_t)had_newline;
    }

    if (!error && (ferror(stage) || state == -1)) {
        fprintf(stderr, "Cannot read or stage results for '%s'\n", input_path);
        goto cleanup;
    }
    out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Cannot write output '%s': %s\n", output_path, strerror(errno));
        goto cleanup;
    }
    if (error) {
        if (fprintf(out, "ERROR:%zu\n", error) < 0)
            goto write_failure;
    } else {
        if (fseek(stage, 0, SEEK_SET) != 0)
            goto write_failure;
        while ((bytes = fread(buffer, 1, sizeof(buffer), stage)) > 0) {
            if (fwrite(buffer, 1, bytes, out) != bytes)
                goto write_failure;
        }
        if (ferror(stage))
            goto write_failure;
    }
    if (fclose(out) != 0) {
        out = NULL;
        fprintf(stderr, "Cannot finalize output '%s'\n", output_path);
        goto cleanup;
    }
    out = NULL;
    status = 0;                // ERROR:<pos> is a valid result, not a CLI error. //
    goto cleanup;

write_failure:
    fprintf(stderr, "Cannot finish writing output '%s'\n", output_path);
cleanup:
    if (in) fclose(in);
    if (stage) fclose(stage);
    if (out) fclose(out);
    free(line);
    return status;
}

static int has_txt_extension(const char *name)
{
    size_t len = strlen(name);
    return len >= 4 && strcmp(name + len - 4, ".txt") == 0;
}

static int process_one(const char *input_path, const char *output_dir)
{
    char *base = basename_copy(input_path, 1);
    char *first = NULL, *filename = NULL, *full_path = NULL;
    int status = -1;
    if (!base)
        goto cleanup;
    first = combine(base, "_" STUDENT_NAME "_" STUDENT_LASTNAME "_", STUDENT_ID);
    if (!first)
        goto cleanup;
    filename = combine(first, ".txt", "");
    if (!filename)
        goto cleanup;
    full_path = join_path(output_dir, filename);
    if (!full_path)
        goto cleanup;
    status = evaluate_file(input_path, full_path);
cleanup:
    if (status != 0 && !full_path)
        fprintf(stderr, "Out of memory while constructing output path\n");
    free(base);
    free(first);
    free(filename);
    free(full_path);
    return status;
}

static int process_directory(const char *directory, const char *output_dir)
{
    DIR *d = opendir(directory);
    struct dirent *entry;
    char **files = NULL;
    size_t count = 0, capacity = 0, i;
    int status = 0;
    if (!d) {
        fprintf(stderr, "Cannot open input directory '%s': %s\n",
                directory, strerror(errno));
        return -1;
    }

    /*
     * Snapshot all input filenames BEFORE writing outputs. Otherwise, if
     * -d and -o refer to the same folder, readdir() could accidentally
     * discover result files that this very run just created.
     */
    errno = 0;
    while ((entry = readdir(d)) != NULL) {
        char *path;
        struct stat info;
        if (!has_txt_extension(entry->d_name))
            continue;
        path = join_path(directory, entry->d_name);
        if (!path) {
            fprintf(stderr, "Out of memory while processing directory\n");
            status = -1;
            break;
        }
        if (stat(path, &info) != 0) {
            fprintf(stderr, "Cannot inspect input '%s': %s\n", path, strerror(errno));
            free(path);
            status = -1;
            break;
        }
        if (!S_ISREG(info.st_mode)) {
            free(path);
            errno = 0;
            continue;
        }
        if (count == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 16;
            char **expanded;
            if (new_capacity <= capacity ||
                !(expanded = realloc(files, new_capacity * sizeof(*files)))) {
                fprintf(stderr, "Out of memory collecting filenames\n");
                free(path);
                status = -1;
                break;
            }
            files = expanded;
            capacity = new_capacity;
        }
        files[count++] = path;
        errno = 0;
    }
    if (status == 0 && errno != 0) {
        fprintf(stderr, "Cannot read directory '%s': %s\n", directory, strerror(errno));
        status = -1;
    }
    closedir(d);
    if (status == 0) {
        for (i = 0; i < count; i++) {
            if (process_one(files[i], output_dir) != 0)
                status = -1;
        }
    }
    for (i = 0; i < count; i++)
        free(files[i]);
    free(files);
    return status;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n",
            program);
}

int main(int argc, char **argv)
{
    const char *input = NULL, *directory = NULL, *output_option = NULL;
    char *default_dir = NULL, *base = NULL;
    const char *out_dir;
    int i, result;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (++i >= argc || directory) {
                usage(argv[0]);
                return 1;
            }
            directory = argv[i];
        } else if (strcmp(argv[i], "-o") == 0 ||
                   strcmp(argv[i], "--output-dir") == 0) {
            if (++i >= argc || output_option) {
                usage(argv[0]);
                return 1;
            }
            output_option = argv[i];
        } else if (argv[i][0] == '-' || input) {
            usage(argv[0]);
            return 1;
        } else {
            input = argv[i];
        }
    }
    if ((directory && input) || (!directory && !input)) {
        usage(argv[0]);
        return 1;
    }
    if (!directory && !has_txt_extension(input)) {
        fprintf(stderr, "Expected input file ending in .txt\n");
        return 1;
    }
    if (output_option) {
        out_dir = output_option;
    } else {
        base = basename_copy(directory ? directory : input, input != NULL);
        if (!base) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
        default_dir = combine(base, "_" STUDENT_NAME "_", STUDENT_ID);
        free(base);
        if (!default_dir) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
        out_dir = default_dir;
    }
    result = ensure_directory(out_dir);
    if (result == 0)
        result = directory ? process_directory(directory, out_dir)
                           : process_one(input, out_dir);
    free(default_dir);
    return result == 0 ? 0 : 1;
}
