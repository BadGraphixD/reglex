#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#REGLEX_DECLARATIONS

typedef struct string {
  char *data;
  size_t length;
} string_t;

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

int reglex_accept(int tag) {
  reglex_checkpoint_tag = tag;
  size_t chars_to_accept = reglex_read_ahead.length - reglex_read_ahead_ptr;
  reglex_append_str_to_str_n(&reglex_lexem_str, &reglex_read_ahead,
                             chars_to_accept);
  reglex_shift_str(&reglex_read_ahead, chars_to_accept);
  return 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#REGLEX_PARSER_SWITCHING

static char *reglex_lexem() { return reglex_lexem_str.data; }

#pragma GCC diagnostic pop

static int reglex_parse_result = -1;

#REGLEX_REJECT_FUNCTIONS

int reglex_next() {
  int c;
  if (reglex_read_ahead_ptr > 0) {
    c = reglex_read_ahead
            .data[reglex_read_ahead.length - reglex_read_ahead_ptr--];
  } else {
    c = fgetc(
#REGLEX_INPUT_FS
    );
    if (c != EOF) {
      reglex_append_char_to_str(&reglex_read_ahead, c);
    }
  }
  return c;
}

int reglex_parse() {
  while (reglex_parse_result == -1) {
    reglex_token_parser_fn();
  }
  return reglex_parse_result;
}

#REGLEX_MAIN
