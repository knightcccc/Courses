# C1语法分析文档

## 1. 分析与设计

### 目标

主要要做的事有两个

1. 根据C1文法补充完成`C1Parser.g4`文件。

基本上根据文法逐条添加。

2. 根据C1文法(而不是C规范)添加正确测试案例

C1文法对C有简化，有些复杂的内容没有

## 2. 具体设计与难点

### 语法文件的填写

按照文法EBNF逐条填写即可。

1. 我没有添加其他语法符号，所以需要把`BType`等展开
2. 词法符号都是大写字母开头，容易写错
3. 词法记号流最终都是以`EOF`结束，所以在`compilationUnit`最后要加一个`EOF`
4. 实验文件给出的`exp`与文法EBNF描述的并不相符。考虑到左递归情形下的优先级，略作修改。`lval`与`number`优先级调换应该没啥问题

非常容易出错的地方是

- 在C1文法描述中的`'['...']'`与`[...]`是完全不同的，多次看错

### 测试案例

#### 案例的写法

根据文法特点，重点测试3类案例。即函数、条件分支与循环、表达式。

需要体现的内容是文法描述的关于变量声明初始化、函数声明、表达式的优先级。

需要注意的是

- 函数没有返回值，统统为void

- 函数没有参数列表，仅仅有一个空的括号

比如

```c
void main()
{
    int a=1;
    int b=2;
    int c=a+b*a;
}
```



#### 测试方法

在`grammer`目录下

1. `make`编译工具
2. `make exampleParser`测试案例，终端输出
3. `make exampleGParser`测试案例，图像输出。共6个依次输出，谨慎使用
4. `make testParser ` 手动测试
5. `make clean`清空

