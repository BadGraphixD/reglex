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
`stdin` (for the syntax, look at the comment in `reglex.c` and the examples `test/*.reglex`;
for how to use the `reglex` executable, simply call it with the `-h` or `--help` option). It
converts the given regular expressions and their code actions into c code.

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

# How to use

There are generally two ways to use the generated c file:

- Use the instruction `emit_main` and use the token actions to execute code per token. The generated
  c file can then simply be compiled into an executable.
- Use a custom main function, which calls `reglex_parse` or `reglex_parse_token` (see below). Optionally
  use the function `reglex_set_is` (see below) to further control the input stream of the generated parser.

The generated code contains the following functions:

`int reglex_parse()`
Parses a stream of chars into tokens. If no tokens can
be matched, the function returns `1`. Otherwise, it continues parsing, until `EOF` is reached.
If all chars have been consumed by tokens, `0` is returned.

`void reglex_parse_token()`
Parses the next token in the input stream. Returns once exactly one token has been parsed or an
error has occurred. It tries to match the longest token possible and if two tokens are of equal
length, the one which comes first in the spec is chosen. After this functions has been called,
the global variable `int reglex_parse_result` contains `-1` if the token has been successfully parsed
and the input stream contains more chars to be parsed, `0` if the token has been successfully
parsed, and `EOF` was encountered in the input stream, and `1` if the input stream could not
be parsed into any token. To get the type of token parsed, a global variable can be used, which
can be set during the token actions and read after the call to `reglex_parse_token`.

`const char *reglex_lexem()`
After a token has been parsed, this function returns a pointer to the parsed lexem (the string
of the parsed token). The data behind the pointer may overwritten or become invalid, so it must
be copied to be used later. This function can also be used inside the token action.

`void reglex_set_is(FILE *is, const char *filename)`
This function can be called at any time to set the input stream from which to read. Optionally,
a filename can be set at this point (may be `NULL`), which can be later read with `reglex_filename`.

`const char *reglex_filename()`
Returns the filename set by `reglex_set_is` or `NULL`.

`int reglex_ln()`
Returns the line of the first char in the lexem of the last parsed token. Can be used after
`reglex_parse_token` has been called or during code actions.

`int reglex_col()`
Returns the column of the first char in the lexem of the last parsed token. Can be used after
`reglex_parse_token` has been called or during code actions.

`void reglex_switch_parser(const char *name)`
This function can be called with a string containing the name of a parser in the spec to switch
to that parser. Calling the function with any other string is undefined behaviour. This function
can be used at any time (except while a token is parsed) and even inside a token action. It is
not possible to switch to an unnamed parser.

`int main()`
Is only generated when the instruction `emit_main` is used (see below).

And contains the following global variables:

`int reglex_parse_result`
See `void reglex_parse_token()`.

# reglex instructions

- `emit_main`: Instruction to generate a `main` function, which calls `reglex_parse()` and returns its return value.
