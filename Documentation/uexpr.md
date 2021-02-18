# Introduction
This is the documentation for the core `uexpr` language. The name `uexpr`
stands for micro expression language.

## Design principles
- Simple: the specification and the implementation should be as simple as
  possible.
- Simple to write: the design process should first consider what problems the
  user of the language would like to solve using the language, and design
  syntax & semantics so that solutions to these problems are simple to express
  using the language.

# Syntax

## Tokens
The grammar below operates on tokens. Tokens are split along one or more
whitespace characters, except for the "()[]{},$~&|=%;," characters, which
always constitute as their own tokens.

## Grammar
```
(* basic definitions *)
char = ? any character ? ;

(* string literal *)
str = ? [a-zA-Z0-9-_] ? ;
str = "\"", { char - "\"" }, "\"" ;

expr = str ; (* literal *)
expr = "(", expr, ")" ; (* parenthesized expression *)

expr = "[", expr, { ",", expr }, "]" | "[", "]" ; (* list *)
expr = "{", expr, { ";", expr }, "}" | "{", "}"; (* block of expressions *)
expr = str, "(", expr, { ",", expr }, ")" | str, "(", ")" ; (* function call *)

expr = "$", str ; (* variable *)

unary-op = "~" ;
binary-op = "&", "|", "=", "%" ;
expr = unary-op, expr ;
expr = expr, binary-op, expr ;

grammar = expr
```

# Semantics of evaluation
If an expression is evaluated, the semantics described here apply.

## Values
Every expression evaluates to a value. Each value is associated with a type.
(See below for a list of types.)

## Types
### `string`
Any UTF-8 string, excluding null characters.
### `boolean`
Has the values `True` and `False`.
### `list`
An ordered list of heterogenous values.
### `void`
Has the only value `Void`.
### `nativeobj`
Represents an opaque, implementation defined object.
### `fn`, `nativefn`
Represents a function (either a `uexpr`, or a native one, respectively). For
how to call such a function, see the description for the function call
expression. For how to obtain a value of type `fn`, see the built-in function
`let_fn`.
### `error`
Has the only value `Error`.

Note that the value names such as `True`, `False`, `Void`, or `Error` don't
actually appear in the syntax of the language, so they really don't serve for
much other than documentation purposes.

## Variables
When we try to *set* a variable to a value, an implementation defined native
variable handling function is asked to handle the request. If the request is
NOT handled by this function, the variable is set to the value in a global
variable store.

When we try to *get* a variable, an implementation defined native variable
handling function is asked to handle the request. If the request is NOT handled
by this function, the variable is looked up in the global variable store. If
it's found, the previously stored value is returned. Otherwise, `Error` is
returned.

## String literal expression
A string literal evaluates to a value of type `string`, with the contents of
the literal.

## List expression
Each subexpressions is evaluated in sequence. The expression evaluates to a
value of type `list`, with it's items being the values of the evaluated
subexpressions.

## Block expression
Each subexpression is evaluated in sequence. The expression evaluates to the
value of the last subexpressions, or void, if there weren't actually any
subexpressions.

## Function call expression
If the name of the function matches a built-in function, evaluation proceeds in
a manner defined for that built-in function.

If the name of the function matches the name of a variable, and the variable
contains a value of type `fn` or `nativefn`, the function associated with this
value is called. The arguments of an `fn` are reserved for future use, it's not
defined as of now what will happen to them. It's recommended that you call it
with no arguments. In case of a `nativefn`, it's entirely up to the function as
to what to do with the arguments.

If neiter of the above rules match, the expression evaluetes to `Error`.

It's important to note, that in all of the cases above, the arguments of the
function are NOT evaluated by default. It's up to the function to evaluate it's
arguments, if it so desires.

The function call expression evaluates to the value returned by the function.

## Variable expression
Evaluates to the result of *getting* the variable.

## Built-in operators
- "=" operator: Equality. Both operands must have type `string`, or type
  `nativeobj`. Result has type `boolean`.
- "&", "|" operators: Logical *and* and *or*, respectively. First operand must
  have type `boolean`. Result has type `boolean`, or is the second argument.
  (Short circuiting.)
- "%" operator: The second operand must have type `list`. Tests whether the
  first operand compares equal to (with the same semantics as the "=" operator)
  any item of the list. Result has type `boolean`.
- "~" operator: Logical negation. Operand must be `boolean`. Result has type
  `boolean`.

# Built-in functions
Below we describe the core functions always present during evaluation. Other,
non-core native functions are implemented by `nativefn` values. (These values
can get into the evaluation from the special variable handling native
functions.)

Each built-in function returns `Error` if the number of arguments is not
exactly as specified.

## Function "let", 2 arguments.
The first argument must be a variable expression. The second argument is
evaluated. If it does not evaluate to a value of type `string` or `boolean`,
then `Error` is returned. Otherwise, the variable is *set* to the value.

Usage example:

`let($a, hello_world)`

## Function "let_fn", 2 arguments.
The first argument must be a variable expression. It is *set* to a value of
type `fn`. The second argument serves as the body of the function being
defined, and will be evaluated when the function is called, and the function
call expression will evaluate exactly to what it evaluated to.

Usage example:

`{ let_fn($a, print("hello world")); a() }`

## Function "apply", 2 arguments.
The first argument is evaluated. If it does not evaluate to a value of type
`list`, `Error` is returned. Otherwise, it evaluates the second expression for
each element (along with *setting* the variable $i to the current element
before each evaluation). Returns a value of type `list`, with it's elements
being the results of the evaluations.

Usage example:

`apply([ a, b, c ], print($i))`

## Function "print", any number of arguments.
Each argument is evaluated, the value immediately printed to an implementation
defined output stream. Values of type `string` are printed as-is. The printed
representation for other values is implementation defined.  Returns `Void`.

Usage example:

`print("hello world")`

## Function "startsw", 2 arguments.
Both arguments are evaluated. If both evaluate to a value with type `string`, a
value of type `boolean` is returned representing whether the first string
starts with the second one. Otherwise, `Error` is returned.

Usage example:

`startsw($a, h) & print("$a starts with the letter h")`
