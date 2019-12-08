# clox

### pratt-parsing 的核心算法

1. 为每一个运算符分配一个优先级，或者用 pratt 的术语叫做**binding power**。

2. 用一个递归的函数来解析表达式，依次消费 token, 直到遇见一个操作符的优先级小于或等于之前的操作符优先级，或者紧紧是小于之前的优先级（如果这是一个右结合的表达式）.

3. 在 pratt-parsing 的算法中，token 可以用在不同的位置，有不同的含义，例如前缀操作符：-, (，中缀表达式：+, -, \*, /，后缀表达式：++，--;

## refs

[Pratt Parsing and Precedence Climbing](http://www.oilshell.org/blog/2017/03/31.html)
[pratt-parsing](https://dev.to/jrop/pratt-parsing)
[pratt-parsers-expression-parsing-made-easy](http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/)
