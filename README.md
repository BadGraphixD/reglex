# reglex

This is a simple lexer generator written in c. It compiles a special c-like file to c, similar to lex.

# Build the project

Clone the project using the following command:
`git clone --recursive-submodules git@github.com:BadGraphixD/reglex.git`

Run `make` to build the `reglex` executable.

To clean the project, run `make clean`.

# Problem

There can be multiple tokens and code actions, and we need to check each of their regexes and choose the first one with the longest match.
I see two ways of implementing this:

1. Try each regex separately and choose the right one
   Requirements:
   We must be able to roll back to a saved location in the input stream
   We must be able to add checkpoints during parsing (possible stops to the parsing)

2. Somehow combine all regexes into one large automaton
   We must be able to add different kinds of end nodes
   Rewrite the determinisation and minimization algorithm to support that
   The automata may become much larger
   But: when combining sets of end nodes, only the smaller end node tag survives -> no tag set per node necessary

The second options seems more attractive, also because it would be more efficient (no backtracking required).
