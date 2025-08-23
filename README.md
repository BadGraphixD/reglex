# reglex

This is a simple lexer generator written in c. It compiles a special c-like file to c, similar to lex.

# Build the project

Clone the project using the following command:
`git clone --recursive-submodules git@github.com:BadGraphixD/reglex.git`

Run `make` to build the `reglex` executable.

To clean the project, run `make clean`.

To test the project, write a lexer specification (with the reglex instruction `emit_main`)
into the file `test/lexer.reglex` and run `make test` in the root directory. This will
build the executable `./test/lexer`, which reads from `stdin`, tries to divide the input stream
into tokens and executes the corresponding code actions.

# How it works

The `reglex` executable converts a valid lexer specification, which can be passed as a file or via
`stdin` (for the syntax, look at the comment in `reglex.c` and the example `test/lexer.reglex`;
for how to use the `reglex` executable, simply call it with the `-h` or `--help` option). It
converts the given regular expressions and their code actions into c code.

The generated code contains the function `int reglex_parse()`, which parses a stream of chars
into tokens. It tries to match the longest token possible and if two tokens are of equal length,
the one which comes first in the spec is chosen. If no tokens can be matched, the function returns
`1`. Otherwise, it continues parsing, until `EOF` is reached. If all chars have been consumed
by tokens, `0` is returned.

The token specs in the spec file are converted into automata, combined into one large automata,
optimized and then converted into c code. For that, my c library `regex2c` is used. This
library allows the automata's end nodes to be tagged, which makes them behave differently. This
is the mechanism, which allows the lexer to differentiate between tokens during parsing, while
only using one state-machine.

There is only one problem: backtracking
When a token can be matched, the parser continues to parse, with the hopes of finding an even
bigger match. Each time a token could be matched, it saves a checkpoint. When the next char
can't be matched anymore, it has to backtrack to the last checkpoint. Since all tokens are parsed
by the same state-machine, we don't need to run the parser over the same chars multiple times.
Only the chars since the last checkpoint need to be parsed again.

The lexer specification also allows for multiple parsers to be specified in a single `.reglex` file.
For this to work, each new parser must be named. The first parser does not necessarily need a name.
During a code action, the function `void reglex_switch_parser(const char *name)` can be called, to
change the parser. Per default, the first parser in the spec file is chosen in the beginning.

### Example:

We want to match the words 'aba', 'a' and 'b'.

If the string 'ab' is read, the parser finds the match for 'a' and creates a checkpoint. It then
tries to continue parsing (to match 'aba'), but once the end of the string is reached, the parser
has to backtrack. It then saves the token 'a' and continues parsing at 'b'.

If we were to use one state-machine per token, the parser would have to backtrack to the beginning
of the string instead of continuing at 'b'.

# reglex instructions

- `emit_main`: Instruction to generate a `main` function, which calls `reglex_parse()`.
- `emit_input_fs_var`: Instruction to generate a global variable `FILE *reglex_input_fs`, from which
  input is read instead of `stdin`. Must be set by external code and can be used to switch files.
