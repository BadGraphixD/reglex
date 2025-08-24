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

#define REGLEX_DECLARATIONS "#REGLEX_DECLARATIONS"
#define REGLEX_PARSER_SWITCHING "#REGLEX_PARSER_SWITCHING"
#define REGLEX_REJECT_FUNCTIONS "#REGLEX_REJECT_FUNCTIONS"
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

typedef struct parser_spec {
  struct parser_spec *next;
  token_action_list_t *tal;
  ast_list_t *ast_list;
  string_t name;
  string_t unique_name;
  bool_t is_default;
  bool_t is_named;
  int idx;
} parser_spec_t;

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

static bool_t output_debug_info = 0;

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
  // Note: if '\n' is undone, line counting breaks
  has_undo_char = 1;
  undo_char_ = next_char;
  next_char = c;
  col--;
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

static bool_t next_is_whitespace() {
  switch (peek_next()) {
  case '\n':
  case '\r':
  case '\t':
  case ' ':
    return 1;
  default:
    return 0;
  }
}

static void consume_whitespace() {
  while (next_is_whitespace()) {
    consume_next();
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
        reject("expected name");
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

static bool_t next_is_parser_name() {
  if (peek_next() == '%') {
    consume_next();
    if (peek_next() == '{') {
      undo_char('%');
      return 1;
    } else {
      undo_char('%');
    }
  }
  return 0;
}

static bool_t try_consume_parser_name(string_t *name) {
  if (peek_next() == '%') {
    consume_next();
    if (peek_next() == '{') {
      consume_next();
      consume_whitespace();
      *name = consume_name();
      consume_whitespace();
      if (peek_next() != '%') {
        reject("expected '%}' after parser name");
      }
      consume_next();
      if (peek_next() != '}') {
        reject("expected '%}' after parser name");
      }
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
    } else {
      reject("invalid instruction '%s'", name.data);
    }
    free(name.data);
  }
}

static void consume_reg_defs() {
  if (output_debug_info) {
    fprintf(out_file, "--- Regular definitions:\n");
  }
  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      if (output_debug_info && defs == NULL) {
        fprintf(out_file, "None given\n");
      }
      fprintf(out_file, "\n");
      return;
    }
    string_t name = consume_name();
    consume_whitespace();
    ast_t ast = consume_regex_expr();
    if (output_debug_info) {
      fprintf(out_file, "\nAST of %s:\n", name.data);
      print_ast_indented(&ast, 1, out_file);
    }
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

static bool_t consume_token_actions(token_action_list_t **list, string_t *name,
                                    bool_t *found_name) {
  int tag_ctr = 0;
  *found_name = 0;
  *list = NULL;

  consume_whitespace();
  if (try_consume_parser_name(name)) {
    *found_name = 1;
  }

  while (1) {
    consume_whitespace();
    if (try_consume_delimiter()) {
      return 0;
    }
    if (next_is_parser_name()) {
      return 1;
    }
    ast_t token = consume_regex_expr();
    consume_whitespace();
    string_t action = consume_action();
    token_action_list_t *new_action = malloc(sizeof(token_action_list_t));
    new_action->token = token;
    new_action->next = *list;
    new_action->action = action;
    new_action->tag = tag_ctr++;
    *list = new_action;
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

static string_t get_unique_default_name(parser_spec_t *specs) {
  while (specs != NULL) {
    if (specs->is_default) {
      return specs->unique_name;
    }
    specs = specs->next;
  }
  errx(EXIT_FAILURE,
       "internal error: parser specs do not contain a default spec");
}

static void print_parser_switching(parser_spec_t *specs) {
  bool_t is_first = 1;
  fprintf(out_file,
          "static void (*reglex_token_parser_fn)() = reglex_parse_token_%s;\n",
          get_unique_default_name(specs).data);
  fprintf(out_file, "void reglex_switch_parser(const char *parser_name) {\n");
  while (specs != NULL) {
    if (specs->is_named) {
      fprintf(out_file,
              " %s (strcmp(parser_name, \"%s\") == 0) {\n"
              "    reglex_token_parser_fn = reglex_parse_token_%s;\n"
              "  }",
              is_first ? " if" : "else if", specs->name.data,
              specs->unique_name.data);
    }
    specs = specs->next;
    is_first = 0;
  }
  fprintf(out_file, "}\n");
}

static void print_token_actions(token_action_list_t *token_actions) {
  while (token_actions != NULL) {
    fprintf(out_file, "  case %d:\n", token_actions->tag);
    fprintf(out_file, "    %s\n", token_actions->action.data);
    fprintf(out_file, "    break;\n");
    token_actions = token_actions->next;
  }
}

static void print_token_actions_list_debug_info(token_action_list_t *tal) {
  while (tal != NULL) {
    fprintf(out_file, "  Tag: '%d'\n", tal->tag);
    fprintf(out_file, "  Action: '%s'\n", tal->action.data);
    fprintf(out_file, "  AST:\n");
    print_ast_indented(&tal->token, 3, out_file);
    tal = tal->next;
  }
}

static void print_reject_functions(parser_spec_t *specs) {
  while (specs != NULL) {
    fprintf(out_file,
            "void reglex_reject_%s() {\n  switch (reglex_checkpoint_tag) {\n",
            specs->unique_name.data);
    print_token_actions(specs->tal);
    fprintf(out_file, "  default:\n"
                      "    if (reglex_read_ahead.length == 0) {\n"
                      "      reglex_parse_result = 0;\n"
                      "    } else {\n"
                      "      reglex_parse_result = 1;\n"
                      "    }\n"
                      "    break;\n"
                      "  }\n"
                      "  reglex_reset_to_checkpoint();\n"
                      "}\n");
    specs = specs->next;
  }
}

static void delete_ast_list(ast_list_t *list) {
  while (list != NULL) {
    ast_list_t *next = list->next;
    free(list);
    list = next;
  }
}

static void delete_parser_specs(parser_spec_t *specs) {
  while (specs != NULL) {
    parser_spec_t *next = specs->next;
    delete_token_action_list(specs->tal);
    delete_ast_list(specs->ast_list);
    if (specs->is_named) {
      free(specs->name.data);
    }
    free(specs->unique_name.data);
    free(specs);
    specs = next;
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
                                       {"debug", no_argument, NULL, 'd'},
                                       {"output", required_argument, NULL, 'o'},
                                       {NULL, 0, NULL, 0}};

static char *OPTIONS_HELP[] = {
    ['h'] = "print this help list",
    ['v'] = "print program version",
    ['d'] = "output debug information",
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
  case 'd':
    output_debug_info = 1;
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
  consume_reg_defs();

  if (output_debug_info) {
    fprintf(out_file, " --- Parser spec(s):\n");
  }
  parser_spec_t *specs = NULL;
  int parser_idx = 0;
  bool_t c;
  do {
    parser_spec_t *next_specs = malloc(sizeof(parser_spec_t));
    next_specs->next = specs;
    next_specs->is_default = parser_idx == 0;
    next_specs->idx = parser_idx;
    specs = next_specs;
    c = consume_token_actions(&specs->tal, &specs->name, &specs->is_named);

    // Ensure each parser has a unique name
    if (specs->is_named) {
      specs->unique_name = create_string(specs->name.data);
      append_str_to_str(&specs->unique_name, "_named");
    } else {
      specs->unique_name.length =
          asprintf(&specs->unique_name.data, "unnamed_%d", parser_idx);
    }
    specs->ast_list = to_ast_list(specs->tal);

    automaton_t automaton = convert_ast_list_to_automaton(specs->ast_list);
    automaton_t dfa = determinize(&automaton);
    automaton_t mdfa = minimize(&dfa);

    if (mdfa.nodes[mdfa.start_index].end_tag != -1) {
      reject("no token expressions may accept an empty string");
    }

    char *parse_token_fn_name;
    asprintf(&parse_token_fn_name, "reglex_parse_token_%s",
             specs->unique_name.data);
    char *reject_fn_name;
    asprintf(&reject_fn_name, "reglex_reject_%s", specs->unique_name.data);

    print_automaton_to_c_code(mdfa, parse_token_fn_name, "reglex_next",
                              "reglex_accept", reject_fn_name,
                              REGEX2C_ALL_DECL_STATIC, out_file);

    if (output_debug_info) {
      fprintf(out_file, "New parser spec (name='%s', unique_name='%s'):\n",
              specs->is_named ? specs->name.data : "<unnamed>",
              specs->unique_name.data);
      fprintf(out_file, " Tokens & Actions:\n");
      print_token_actions_list_debug_info(specs->tal);
      fprintf(out_file, " NFA:\n");
      print_automaton(&automaton, out_file);
      fprintf(out_file, " DFA:\n");
      print_automaton(&dfa, out_file);
      fprintf(out_file, " Minimal DFA:\n");
      print_automaton(&mdfa, out_file);
    }

    free(parse_token_fn_name);
    free(reject_fn_name);
    parse_token_fn_name = NULL;
    reject_fn_name = NULL;

    delete_automaton(automaton);
    delete_automaton(dfa);
    delete_automaton(mdfa);

    parser_idx++;
  } while (c);

  int declarations_before, declarations_after;
  int switching_before, switching_after;
  int reject_functions_before, reject_functions_after;
  int main_before, main_after;

  strstr_bounds(lexer_template, REGLEX_DECLARATIONS, &declarations_before,
                &declarations_after);
  strstr_bounds(lexer_template, REGLEX_PARSER_SWITCHING, &switching_before,
                &switching_after);
  strstr_bounds(lexer_template, REGLEX_REJECT_FUNCTIONS,
                &reject_functions_before, &reject_functions_after);
  strstr_bounds(lexer_template, REGLEX_MAIN, &main_before, &main_after);

  fprintsl(out_file, lexer_template, 0, declarations_before);

  fprintsl(out_file, lexer_template, declarations_after, switching_before);
  print_parser_switching(specs);
  fprintsl(out_file, lexer_template, switching_after, reject_functions_before);
  print_reject_functions(specs);
  delete_parser_specs(specs);
  delete_reg_def_list(defs);
  specs = NULL;
  defs = NULL;

  fprintsl(out_file, lexer_template, reject_functions_after, main_before);

  if (flags & INSTR_EMIT_MAIN) {
    fprintf(out_file, "%s", lexer_main);
  }

  consume_c(1);

  if (out_file != NULL && out_file != stdout) {
    fclose(out_file);
  }
  free(in_files);
  in_files = NULL;

  return EXIT_SUCCESS;
}
