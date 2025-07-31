/**
 * We convert a c-like file, which describes a lexer and contains regular
 * definitions of lexems and code actions, into a pure c-file. The
 * generated c-file either compiles to an executable, which takes a char-stream
 * from stdin, produces a token-stream and performs the code actions attached to
 * the lexems, or can be linked with other code.
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
 * The regex describes the lexems, and the code action (everything between
 * the special brackets) can be any c code, an is transferred as-is into the
 * resulting c file. lexems and code actions are separated by whitespace.
 */

#include "regex2c/ast.h"
#include "regex2c/ast2automaton.h"
#include "regex2c/automaton.h"
#include "regex2c/automaton2c.h"
#include "regex2c/common.h"
#include "regex2c/regex_parser.h"

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INSTR_EMIT_MAIN 1

const char *lexer_start =
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "\n"
    "extern void reglex_parse_token();\n"
    "\n"
    "typedef struct string {\n"
    "  char *data;\n"
    "  size_t length;\n"
    "} string_t;\n"
    "\n"
    "void reglex_append_char_to_str(string_t *string, char c) {\n"
    "  string->length++;\n"
    "  string->data = realloc(string->data, (string->length + 1) * "
    "sizeof(char));\n"
    "  string->data[string->length - 1] = c;\n"
    "  string->data[string->length] = 0;\n"
    "}\n"
    "\n"
    "void reglex_append_str_to_str(string_t *dest, string_t *src) {\n"
    "  if (src->data != NULL) {\n"
    "    size_t old_len = dest->length;\n"
    "    dest->length += src->length;\n"
    "    dest->data = realloc(dest->data, (dest->length + 1) * "
    "sizeof(char));\n"
    "    memcpy(&dest->data[old_len], src->data, src->length + 1);\n"
    "  }\n"
    "}\n"
    "\n"
    "void reglex_clear_str(string_t *string) {\n"
    "  free(string->data);\n"
    "  string->data = NULL;\n"
    "  string->length = 0;\n"
    "}\n"
    "\n"
    "int reglex_checkpoint_tag = -1;\n"
    "string_t reglex_lexem_str = {.data = NULL, .length = 0};\n"
    "string_t reglex_read_ahead = {.data = NULL, .length = 0};\n"
    "int reglex_read_ahead_ptr = 0;\n"
    "\n"
    "int reglex_accept(int tag) {\n"
    "  reglex_checkpoint_tag = tag;\n"
    "  reglex_append_str_to_str(&reglex_lexem_str, &reglex_read_ahead);\n"
    "  reglex_clear_str(&reglex_read_ahead);\n"
    "  return 0;\n"
    "}\n"
    "\n"
    "char *reglex_lexem() { return reglex_lexem_str.data; }\n"
    "\n"
    "int reglex_parse_result = -1;"
    "\n"
    "void reglex_reject() {\n"
    "  switch (reglex_checkpoint_tag) {\n";

const char *lexer_end =
    "  default:\n"
    "    if (reglex_read_ahead.length == 0) {\n"
    "      reglex_parse_result = 0;\n"
    "    }\n"
    "    reglex_parse_result = 1;\n"
    "    break;\n"
    "  }\n"
    "  reglex_checkpoint_tag = -1;\n"
    "  reglex_clear_str(&reglex_lexem_str);\n"
    "  reglex_read_ahead_ptr = reglex_read_ahead.length;\n"
    "}\n"
    "\n"
    "int reglex_next() {\n"
    "  int c;\n"
    "  if (reglex_read_ahead_ptr > 0) {\n"
    "    c = reglex_read_ahead.data[reglex_read_ahead.length - "
    "reglex_read_ahead_ptr--];\n"
    "  } else {\n"
    "    c = fgetc(stdin);\n"
    "    if (c != EOF) {\n"
    "      reglex_append_char_to_str(&reglex_read_ahead, c);\n"
    "    }\n"
    "  }\n"
    "  return c;\n"
    "}\n"
    "\n"
    "int reglex_parse() {\n"
    "  while (reglex_parse_result == -1) {\n"
    "    reglex_parse_token();\n"
    "  }\n"
    "  return reglex_parse_result;\n"
    "}\n";

const char *lexer_main = "\n"
                         "int main() {\n"
                         "  reglex_parse();\n"
                         "  return 0;\n"
                         "}\n";

typedef struct reg_def_list {
  struct reg_def_list *next;
  string_t name;
  ast_t ast;
} reg_def_list_t;

typedef struct token_action_list {
  struct token_action_list *next;
  ast_t token;
  string_t action;
  int tag;
} token_action_list_t;

int next_char = EOF;
int col = 0, ln = 1;
bool_t just_consumed_nl = 0;

int peek_next() { return next_char; }

int consume_next() {
  int c = peek_next();
  next_char = fgetc(stdin);
  if (next_char == EOF) {
    // Do not increment line or col on EOF
    return c;
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

reg_def_list_t *defs = NULL;

ast_t *get_definition(char *name) {
  reg_def_list_t *list = defs;
  while (list != NULL) {
    if (strcmp(name, list->name.data) == 0) {
      return &list->ast;
    }
    list = list->next;
  }
  return NULL;
}

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
    if (strcmp(name.data, "emit_main") == 0) {
      flags |= INSTR_EMIT_MAIN;
    } else {
      reject("invalid instruction '%s'", name.data);
    }
  }
}

void consume_ref_defs() {
  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      return;
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
    case EOF:
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
  int tag_ctr = 0;
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
    new_action->tag = tag_ctr++;
    actions = new_action;
  }
}

ast_list_t *to_ast_list(token_action_list_t *token_actions) {
  ast_list_t *ast_list = NULL;
  while (token_actions != NULL) {
    ast_list_t *new = malloc(sizeof(ast_list_t));
    new->next = ast_list;
    new->ast = &token_actions->token;
    ast_list = new;
    token_actions = token_actions->next;
  }
  return ast_list;
}

int main(int argc, char *argv[]) {
  consume_next();
  consume_c(0);
  int flags = consume_instructions();
  consume_ref_defs();
  token_action_list_t *token_actions = consume_token_actions();

  ast_list_t *tokens = to_ast_list(token_actions);
  automaton_t automaton = convert_ast_list_to_automaton(tokens);
  automaton_t dfa = determinize(&automaton);
  automaton_t mdfa = minimize(&dfa);

  if (mdfa.nodes[mdfa.start_index].end_tag != -1) {
    reject("no token expressions may accept an empty string");
  }

  print_automaton_to_c_code(mdfa, "reglex_parse_token", "reglex_next",
                            "reglex_accept", "reglex_reject");
  /* delete_automaton(automaton); */
  /* delete_automaton(dfa); */
  /* delete_automaton(mdfa); */

  printf("%s", lexer_start);

  while (token_actions != NULL) {
    printf("  case %d:\n", token_actions->tag);
    printf("    %s\n", token_actions->action.data);
    printf("    break;\n");
    token_actions = token_actions->next;
  }

  printf("%s", lexer_end);

  if (flags & INSTR_EMIT_MAIN) {
    printf("%s", lexer_main);
  }

  consume_c(1);
  return EXIT_SUCCESS;
}
