# clox

### pratt-parsing的核心算法

1. You can assign each operator token a precedence, or binding power in Pratt's terminology.

2. You have a recursive function that parses expressions, consuming tokens to the right, until it reaches an operator of precedence less than or equal to the previous operator -- or just less than if it's a right-associative operator.

3. In Pratt parsing, tokens can be used in the null and/or left position, based on whether they take an expression on the left or not (nud or led in Pratt's terminology). Examples of left operators are infix +, postfix ++, or the pseudo-infix a[0] (with operator [). Examples of null operators are unary minus -, and grouping parentheses (.

## refs

[Pratt Parsing and Precedence Climbing](http://www.oilshell.org/blog/2017/03/31.html)
[pratt-parsing](https://dev.to/jrop/pratt-parsing)
[pratt-parsers-expression-parsing-made-easy](http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/)