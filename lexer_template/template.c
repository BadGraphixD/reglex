#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#REGLEX_DECLARATIONS

extern void reglex_parse_token();

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

static void reglex_append_str_to_str(string_t *dest, string_t *src) {
  if (src->data != NULL) {
    size_t old_len = dest->length;
    dest->length += src->length;
    dest->data = realloc(dest->data, (dest->length + 1) * sizeof(char));
    memcpy(&dest->data[old_len], src->data, src->length + 1);
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
  reglex_append_str_to_str(&reglex_lexem_str, &reglex_read_ahead);
  reglex_clear_str(&reglex_read_ahead);
  return 0;
}

static char *reglex_lexem() { return reglex_lexem_str.data; }

static int reglex_parse_result = -1;

void reglex_reject() {
  switch (reglex_checkpoint_tag) {
#REGLEX_TOKEN_ACTIONS
  default:
    if (reglex_read_ahead.length == 0) {
      reglex_parse_result = 0;
    } else {
      reglex_parse_result = 1;
    }
    break;
  }
  reglex_checkpoint_tag = -1;
  reglex_clear_str(&reglex_lexem_str);
  reglex_read_ahead_ptr = reglex_read_ahead.length;
}

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
    reglex_parse_token();
  }
  return reglex_parse_result;
}

#REGLEX_MAIN
