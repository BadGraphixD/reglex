#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#REGLEX_DECLARATIONS

typedef struct string {
  char *data;
  size_t length;
} string_t;

typedef struct location {
  int ln;
  int col;
  char eol;
} location_t;

static void reglex_increment_loc(location_t *loc, int c) {
  if (loc->eol) {
    loc->eol = 0;
    loc->col = 0;
    loc->ln++;
  }
  if (c == '\n') {
    loc->eol = 1;
  }
  loc->col++;
}

static void reglex_append_char_to_str(string_t *string, char c) {
  string->length++;
  string->data = realloc(string->data, (string->length + 1) * sizeof(char));
  string->data[string->length - 1] = c;
  string->data[string->length] = 0;
}

static void reglex_append_str_to_str_n(string_t *dest, string_t *src,
                                       size_t n) {
  size_t old_len = dest->length;
  dest->length += n;
  dest->data = realloc(dest->data, (dest->length + 1) * sizeof(char));
  memcpy(&dest->data[old_len], src->data, n);
  dest->data[dest->length] = '\0';
}

static void reglex_shift_str(string_t *str, size_t n) {
  if (n > 0) {
    memmove(str->data, &str->data[n], str->length - n + 1);
    str->length -= n;
  }
}

static void reglex_clear_str(string_t *string) {
  free(string->data);
  string->data = NULL;
  string->length = 0;
}

static int reglex_checkpoint_tag = -1;
static string_t reglex_lexem_str = {.data = NULL, .length = 0};
static string_t reglex_read_ahead = {.data = NULL, .length = 0};
static int reglex_read_ahead_ptr = 0;

static location_t reglex_curr_loc = {.ln = 1, .col = 0, .eol = 0};
static location_t reglex_checkpoint_loc;
static location_t reglex_lexem_start_loc;

int reglex_accept(int tag) {
  reglex_checkpoint_tag = tag;
  reglex_checkpoint_loc = reglex_curr_loc;
  size_t chars_to_accept = reglex_read_ahead.length - reglex_read_ahead_ptr;
  reglex_append_str_to_str_n(&reglex_lexem_str, &reglex_read_ahead,
                             chars_to_accept);
  reglex_shift_str(&reglex_read_ahead, chars_to_accept);
  return 0;
}

#REGLEX_PARSER_SWITCHING

const char *reglex_lexem() { return reglex_lexem_str.data; }

int reglex_parse_result = -1;

static void reglex_reset_to_checkpoint() {
  reglex_checkpoint_tag = -1;
  reglex_curr_loc = reglex_checkpoint_loc;
  reglex_clear_str(&reglex_lexem_str);
  reglex_read_ahead_ptr = reglex_read_ahead.length;
}

static FILE *reglex_is = NULL;
static const char *reglex_filename_ = NULL;

void reglex_set_is(FILE *is, const char *filename) {
  reglex_is = is;
  reglex_filename_ = filename;
  reglex_curr_loc.ln = 1;
  reglex_curr_loc.col = 0;
  reglex_curr_loc.eol = 0;
}

const char *reglex_filename() { return reglex_filename_; }
int reglex_col() { return reglex_lexem_start_loc.col; }
int reglex_ln() { return reglex_lexem_start_loc.ln; }

#REGLEX_REJECT_FUNCTIONS

static char reglex_just_started_token = 0;

int reglex_next() {
  int c;
  if (reglex_read_ahead_ptr > 0) {
    c = reglex_read_ahead
            .data[reglex_read_ahead.length - reglex_read_ahead_ptr--];
  } else {
    c = fgetc(reglex_is);
    if (c != EOF) {
      reglex_append_char_to_str(&reglex_read_ahead, c);
    }
  }
  reglex_increment_loc(&reglex_curr_loc, c);
  if (reglex_just_started_token) {
    reglex_just_started_token = 0;
    reglex_lexem_start_loc = reglex_curr_loc;
  }
  return c;
}

int reglex_parse_token() {
  if (reglex_is == NULL) {
    reglex_is = stdin;
  }
  reglex_just_started_token = 1;
  reglex_token_parser_fn();
  return reglex_parse_result;
}

int reglex_parse() {
  int result;
  do {
    result = reglex_parse_token();
  } while (result == -1);
  return result;
}

#REGLEX_MAIN
