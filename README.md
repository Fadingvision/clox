# clox

### pratt-parsing 的核心算法

1. You can assign each operator token a precedence, or binding power in Pratt's terminology.

2. You have a recursive function that parses expressions, consuming tokens to the right, until it reaches an operator of precedence less than or equal to the previous operator -- or just less than if it's a right-associative operator.

3. In Pratt parsing, tokens can be used in the null and/or left position, based on whether they take an expression on the left or not (nud or led in Pratt's terminology). Examples of left operators are infix +, postfix ++, or the pseudo-infix a[0] (with operator [). Examples of null operators are unary minus -, and grouping parentheses (.

### GC 算法和策略

#### mark-sweep 标记清除法

标记对象：三色标记法

三色标记法是图遍历算法的一种常用辅助方法，在寻找环，拓扑排序方面都有使用。

白色：所有还未经遍历的对象
灰色：正在经历遍历的对象
黑色：已经完成遍历的对象

在垃圾回收中：所有的根对象都默认标记为灰色对象，将所有的灰色对象放入一个灰色数组中，
依次进行遍历，将每个灰色对象中可以引用到的其他对象标记为灰色，然后放入灰色数组中，
然后将该对象本身标记为黑色(为灰色又不在灰色数组中的对象自动视为黑色)，从灰色数组中移除。
直到灰色对象数组被清空为止，则剩下的白色对象就是未被引用的对象，也就是垃圾回收所需要释放的对象

在完成垃圾回收之后，需要将所有的黑色对象重新标记为白色对象，以便下一次垃圾回收周期使用。

#### GC 运行策略

1. 吞吐量

2. 延迟

## refs

[Pratt Parsing and Precedence Climbing](http://www.oilshell.org/blog/2017/03/31.html)
[pratt-parsing](https://dev.to/jrop/pratt-parsing)
[pratt-parsers-expression-parsing-made-easy](http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/)
