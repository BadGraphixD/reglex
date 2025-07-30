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
 * <regex> {%<code action>}%
 *
 * The regex describes the lexem, and the code action (everything between the
 * special brackets) can be any c code, an is transferred as-is into the
 * resulting c file. Lexems and code actions are separated by whitespace.
 */

#include "regex2c/ast.h"

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int next_char = EOF;
int char_pos = 0;

int peek_next() { return next_char; }

int consume_next() {
  int c = peek_next();
  next_char = getc(stdin);
  char_pos++;
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
  errx(EXIT_FAILURE, "Rejected at char %d: %s", char_pos, errf);
}

ast_t *get_definition(char *name) { return NULL; }

int main(int argc, char *argv[]) { return EXIT_SUCCESS; }
