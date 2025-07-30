/**
 * We convert a c-like file, which describes a lexer and contains regular
 * definitions of lexems and code actions, into a pure c-file. The generated
 * c-file either compiles to an executable, which takes a char-stream from
 * stdin, produces a token-stream and performs the code actions attached to the
 * lexems, or can be linked with other code.
 *
 * The syntax for the consumed file is as follows:
 *
 * <c code>
 * %%
 * <reglex instructions>
 * %%
 * <regular definitions>
 * %%
 * <lexems and code actions>
 * %%
 * <c code>
 *
 * Whitespace is defined as follows: [\n\r\t\s]+
 *
 * The c code is not touched, and transferred as-is to the output file in that
 * order.
 *
 * The following reglex instructions exist:
 *
 * emit_main
 *
 * The instructions are separated by whitespace.
 *
 * The regular definitions sections may contain definitions in the following
 * form:
 *
 * NAME <regex>
 *
 * The name of the definition may have the following form: [a-zA-Z0-9_]+ The
 * regex must be parsable by the regex2c library (see regex2c/README.md).
 * Definitions are separated by whitespace.
 *
 * The lexems and code actions secion may contain the following:
 *
 * <regex> %{<code action>%}
 *
 * The regex describes the lexem, and the code action (everything between the
 * special brackets) can be any c code, an is transferred as-is into the
 * resulting c file. Lexems and code actions are separated by whitespace.
 */

#include "regex2c/ast.h"
#include "regex2c/common.h"
#include "regex2c/regex_parser.h"

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct reg_def_list {
  struct reg_def_list *next;
  string_t name;
  ast_t ast;
} reg_def_list_t;

typedef struct token_action_list {
  struct token_action_list *next;
  ast_t token;
  string_t action;
} token_action_list_t;

int next_char = EOF;
int col = 0, ln = 1;
bool_t just_consumed_nl = 0;

int peek_next() { return next_char; }

int consume_next() {
  int c = peek_next();
  next_char = getc(stdin);
  if (next_char == EOF) {
    // Do not increment line or col on EOF
    return EOF;
  }
  if (just_consumed_nl) {
    just_consumed_nl = 0;
    ln++;
    col = 0;
  }
  if (next_char == '\n') {
    just_consumed_nl = 1;
  }
  col++;
  return c;
}

int reject(char *err, ...) {
  va_list args;
  va_start(args, err);
  char *errf = NULL;
  if (vasprintf(&errf, err, args) == -1) {
    errx(EXIT_FAILURE, "Failed to print error message");
  }
  va_end(args);
  errx(EXIT_FAILURE, "%d:%d: %s", ln, col, errf);
}

ast_t *get_definition(char *name) { return NULL; }

extern bool_t is_end(int c) {
  switch (c) {
  case EOF:
  case '\n':
  case '\r':
  case '\t':
  case '\0':
  case ' ':
    return 1;
  default:
    return 0;
  }
}

void consume_c(bool_t expect_eof) {
  while (1) {
    switch (peek_next()) {
    case EOF:
      if (expect_eof) {
        return;
      }
      reject("unexpected EOF");
      break;
    case '%':
      consume_next();
      if (peek_next() == '%') {
        consume_next();
        return;
      } else {
        fputc('%', stdout);
      }
      break;
    default:
      fputc(consume_next(), stdout);
      break;
    }
  }
}

void consume_whitespace() {
  while (1) {
    switch (peek_next()) {
    case '\n':
    case '\r':
    case '\t':
    case ' ':
      consume_next();
      break;
    default:
      return;
    }
  }
}

string_t consume_name() {
  string_t name = create_string(NULL);
  while (1) {
    switch (peek_next()) {
    case 'a' ... 'z':
    case 'A' ... 'Z':
    case '0' ... '9':
    case '_':
      append_char_to_str(&name, consume_next());
      break;
    default:
      if (name.length <= 0) {
        reject("expected regular definition name");
      }
      return name;
    }
  }
}

bool_t try_consume_delimiter() {
  if (peek_next() == '%') {
    consume_next();
    if (peek_next() == '%') {
      consume_next();
      return 1;
    } else {
      reject("expected '%'");
    }
  }
  return 0;
}

int consume_instructions() {
  int flags = 0;
  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      return flags;
    }
    string_t name = consume_name();
    if (strcmp(name.data, "emit_main")) {
      flags &= (1 << 0);
    } else {
      reject("invalid instruction '%s'", name.data);
    }
  }
}

reg_def_list_t *consume_ref_defs() {
  reg_def_list_t *defs = NULL;
  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      return defs;
    }
    string_t name = consume_name();
    consume_whitespace();
    ast_t ast = consume_regex_expr();
    reg_def_list_t *new_def = malloc(sizeof(reg_def_list_t));
    new_def->name = name;
    new_def->ast = ast;
    new_def->next = defs;
    defs = new_def;
  }
}

string_t consume_action() {
  if (peek_next() != '%') {
    reject("expected action (starts with '%{)");
  }
  consume_next();
  if (peek_next() != '{') {
    reject("expected action (starts with '%{)");
  }
  consume_next();
  string_t action = create_string(NULL);
  while (1) {
    switch (peek_next()) {
      reject("unexpected EOF");
      break;
    case '%':
      consume_next();
      if (peek_next() == '}') {
        consume_next();
        return action;
      } else {
        append_char_to_str(&action, '%');
      }
      break;
    default:
      append_char_to_str(&action, consume_next());
      break;
    }
  }
}

token_action_list_t *consume_token_actions() {
  token_action_list_t *actions = NULL;
  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      return actions;
    }
    ast_t token = consume_regex_expr();
    consume_whitespace();
    string_t action = consume_action();
    token_action_list_t *new_action = malloc(sizeof(token_action_list_t));
    new_action->token = token;
    new_action->next = actions;
    new_action->action = action;
    actions = new_action;
  }
}

int main(int argc, char *argv[]) {
  consume_next();
  consume_c(0);
  int flags = consume_instructions();
  reg_def_list_t *defs = consume_ref_defs();
  token_action_list_t *actions = consume_token_actions();
  // TODO: generate lexer
  consume_c(1);
  return EXIT_SUCCESS;
}
