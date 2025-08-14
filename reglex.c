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
 * emit_input_fs_var
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

#include "regex2c/not_enough_cli/not_enough_cli.h"

#include "regex2c/ast.h"
#include "regex2c/ast2automaton.h"
#include "regex2c/automaton.h"
#include "regex2c/automaton2c.h"
#include "regex2c/common.h"
#include "regex2c/regex_parser.h"

#include <err.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer_template/lexer_template.c"

#define INSTR_EMIT_MAIN 1
#define INSTR_EMIT_INPUT_FS_VAR 2

#define REGLEX_DECLARATIONS "#REGLEX_DECLARATIONS"
#define REGLEX_TOKEN_ACTIONS "#REGLEX_TOKEN_ACTIONS"
#define REGLEX_INPUT_FS "#REGLEX_INPUT_FS"
#define REGLEX_MAIN "#REGLEX_MAIN"

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

static int next_char = EOF;
static int col = 0, ln = 1;
static bool_t just_consumed_nl = 0;

static bool_t has_undo_char = 0;
static int undo_char_ = 0;

static char **in_files = NULL;
static int fin_idx = 0;
static FILE *fin = NULL;

static char *out_file_name = NULL;
static FILE *out_file = NULL;

static reg_def_list_t *defs = NULL;

static void delete_reg_def_list(reg_def_list_t *list) {
  while (list != NULL) {
    reg_def_list_t *next = list->next;
    free(list->name.data);
    delete_ast(list->ast);
    free(list);
    list = next;
  }
}

static void delete_token_action_list(token_action_list_t *list) {
  while (list != NULL) {
    token_action_list_t *next = list->next;
    delete_ast(list->token);
    free(list->action.data);
    free(list);
    list = next;
  }
}

static void open_next_in_file() {
  if (fin != NULL) {
    fclose(fin);
  }
  char *next_in_file_name = in_files[fin_idx++];
  if (next_in_file_name == NULL) {
    fin = NULL;
    return;
  }
  if (strcmp(next_in_file_name, "-") == 0) {
    fin = stdin;
  } else {
    fin = fopen(next_in_file_name, "r");
    if (fin == NULL) {
      errx(EXIT_FAILURE, "Cannot open file \"%s\"\n", next_in_file_name);
    }
  }
}

static int get_next_input_char() {
  if (in_files == NULL) {
    return getc(stdin);
  } else {
    if (fin == NULL) {
      return EOF;
    }
    int next = getc(fin);
    if (next == EOF) {
      open_next_in_file();
      return get_next_input_char();
    }
    return next;
  }
}

static void undo_char(int c) {
  has_undo_char = 1;
  undo_char_ = next_char;
  next_char = c;
  col--; // TODO: if '\n' is undone, line counting breaks
}

int peek_next() { return next_char; }

int consume_next() {
  int c = peek_next();
  if (has_undo_char) {
    next_char = undo_char_;
    has_undo_char = 0;
  } else {
    next_char = get_next_input_char();
  }
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

bool_t is_end(int c) {
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

static void consume_c(bool_t expect_eof) {
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
        fputc('%', out_file);
      }
      break;
    default:
      fputc(consume_next(), out_file);
      break;
    }
  }
}

static void consume_whitespace() {
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

static string_t consume_name() {
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

static bool_t try_consume_delimiter() {
  if (peek_next() == '%') {
    consume_next();
    if (peek_next() == '%') {
      consume_next();
      return 1;
    } else {
      undo_char('%');
    }
  }
  return 0;
}

static int consume_instructions() {
  int flags = 0;
  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      return flags;
    }
    string_t name = consume_name();
    if (strcmp(name.data, "emit_main") == 0) {
      flags |= INSTR_EMIT_MAIN;
    } else if (strcmp(name.data, "emit_input_fs_var") == 0) {
      flags |= INSTR_EMIT_INPUT_FS_VAR;
    } else {
      reject("invalid instruction '%s'", name.data);
    }
  }
}

static void consume_ref_defs() {
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

static string_t consume_action() {
  if (peek_next() != '%') {
    reject("expected action (starts with '%%{)");
  }
  consume_next();
  if (peek_next() != '{') {
    reject("expected action (starts with '%%{)");
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

static token_action_list_t *consume_token_actions() {
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

static ast_list_t *to_ast_list(token_action_list_t *token_actions) {
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

static void delete_ast_list(ast_list_t *list) {
  while (list != NULL) {
    ast_list_t *next = list->next;
    free(list);
    list = next;
  }
}

static void fprintsl(FILE *fout, const char *str, size_t start, size_t end) {
  char fstr[20];
  if (end == -1) {
    end = strlen(str);
  }
  sprintf(fstr, "%%.%lds", end - start);
  fprintf(fout, fstr, str + start);
}

static void strstr_bounds(const char *haystack, char *needle, int *before,
                          int *after) {
  char *ptr = strstr(haystack, needle);
  *before = ptr - haystack;
  *after = *before + strlen(needle);
}

static struct option OPTIONS_LONG[] = {{"help", no_argument, NULL, 'h'},
                                       {"version", no_argument, NULL, 'v'},
                                       {"output", required_argument, NULL, 'o'},
                                       {NULL, 0, NULL, 0}};

static char *OPTIONS_HELP[] = {
    ['h'] = "print this help list",
    ['v'] = "print program version",
    ['o'] = "set output file name",
};

_Noreturn static void version() {
  printf("reglex 1.0\n");
  exit(EXIT_SUCCESS);
}

_Noreturn static void usage(int status) {
  FILE *fout = status == 0 ? stdout : stderr;
  nac_print_usage_header(fout, "[OPTION]... [FILE]...");
  fprintf(
      fout,
      "Converts c-like lexer specification into a pattern matcher in c.\n\n");
  fprintf(fout, "With no FILE, or when FILE is -, read standard input.\n\n");
  nac_print_options(fout);
  exit(status);
}

static void handle_option(char opt) {
  switch (opt) {
  case 'o':
    out_file_name = nac_optarg_trimmed();
    if (out_file_name[0] == '\0') {
      nac_missing_arg('o');
    }
    break;
  }
}

static void parse_args(int *argc, char ***argv) {
  nac_set_opts(**argv, OPTIONS_LONG, OPTIONS_HELP);
  nac_simple_parse_args(argc, argv, handle_option);

  nac_opt_check_excl("hv");
  nac_opt_check_max_once("hvo");

  if (nac_get_opt('h')) {
    usage(*argc > 0 ? EXIT_FAILURE : EXIT_SUCCESS);
  }
  if (nac_get_opt('v')) {
    if (*argc > 0) {
      usage(EXIT_FAILURE);
    }
    version();
  }

  if (out_file_name == NULL) {
    out_file = stdout;
  } else {
    out_file = fopen(out_file_name, "w");
    if (out_file == NULL) {
      errx(EXIT_FAILURE, "Failed to open specified output file \"%s\"\n",
           out_file_name);
    }
  }

  if (*argc > 0) {
    in_files = malloc(sizeof(char *) * (*argc + 1));
    for (int i = 0; i < *argc; i++) {
      in_files[i] = (*argv)[i];
    }
    in_files[*argc] = NULL;
    open_next_in_file();
  }

  nac_cleanup();
}

int main(int argc, char *argv[]) {
  parse_args(&argc, &argv);
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
                            "reglex_accept", "reglex_reject",
                            REGEX2C_ALL_DECL_STATIC, out_file);

  delete_automaton(automaton);
  delete_automaton(dfa);
  delete_automaton(mdfa);

  int declarations_before, declarations_after;
  int token_actions_before, token_actions_after;
  int input_fs_before, input_fs_after;
  int main_before, main_after;

  strstr_bounds(lexer_template, REGLEX_DECLARATIONS, &declarations_before,
                &declarations_after);
  strstr_bounds(lexer_template, REGLEX_TOKEN_ACTIONS, &token_actions_before,
                &token_actions_after);
  strstr_bounds(lexer_template, REGLEX_INPUT_FS, &input_fs_before,
                &input_fs_after);
  strstr_bounds(lexer_template, REGLEX_MAIN, &main_before, &main_after);

  fprintsl(out_file, lexer_template, 0, declarations_before);

  if (flags & INSTR_EMIT_INPUT_FS_VAR) {
    fprintf(out_file, "FILE *reglex_input_fs;\n");
  }

  fprintsl(out_file, lexer_template, declarations_after, token_actions_before);

  while (token_actions != NULL) {
    fprintf(out_file, "  case %d:\n", token_actions->tag);
    fprintf(out_file, "    %s\n", token_actions->action.data);
    fprintf(out_file, "    break;\n");
    token_actions = token_actions->next;
  }

  delete_ast_list(tokens);
  delete_token_action_list(token_actions);
  delete_reg_def_list(defs);
  defs = NULL;

  fprintsl(out_file, lexer_template, token_actions_after, input_fs_before);

  if (flags & INSTR_EMIT_INPUT_FS_VAR) {
    fprintf(out_file, "reglex_input_fs");
  } else {
    fprintf(out_file, "stdin");
  }

  fprintsl(out_file, lexer_template, input_fs_after, main_before);

  if (flags & INSTR_EMIT_MAIN) {
    fprintf(out_file, "%s", lexer_main);
  }

  consume_c(1);
  return EXIT_SUCCESS;
}
