# Grammar
This file explains the grammar of the Javascript language subset supported by Shovel.

## Literals
There are multiple kinds of literals. They are indicated by their uppercase.

- STRING - A string begins and ends with a double quote, and may contain any characters. Strings may contain escape characters beginning with \ followed by a character, which may be: " - a double quote, \ - a slash, n - a new line, t - a tab space, r - a return feed, 0 - a zero terminator.
- UNSIGNED - An unsigned integer contains the digits 0 to 9. An unsigned integer may also be a character, in which case it begins and ends with a single quote, and the UTF-8 representation of character between the quotes is used. The same escape characters that are acceptable in a string are also acceptable as characters.
- FLOAT - A decimal number contains the digits 0 to 9 and includes a decimal point. Floating point numbers must start with a number, not a decimal point.
- IDENTIFIER - An identifier begins with a non-digit character (excluding the special characters), and may include further any characters (digits and non-digits), excluding the following special characters: !%^&*()-+=[]{}|\:;"'/?<>,.
- EOF - The end of the file.

White spaces are ignored unless they are part of a string.

## Comments
There are two styles of comments:
- Line comments. They begin with // and everything until the following new line is ignored.
- Block comments. They begin with /* and everything until the following */ is ignored.

## Rules
Below are a list of grammar rules for the Shovel language.

Each line contains a grammar rule, the value to the left of the colon is the rule's name, and the rule is defined on the right of the colon.

Symbols are in quotes, references to other rules are in lower case, and literals are in uppercase.

Square brackets indicates an optional subexpression.
Curly brackets indicates a subexpression that may occur zero or more times.
Pipes indiciate a choice between the subexpressions on either side.
Curved brackets groups together subexpressions, usually when working with pipes.

The entry rule the whole file is 'main'.

````
- break_statement: 'break' IDENTIFIER ';'
- continue_statement: 'continue' IDENTIFIER ';'
- label_statement: '.' IDENTIFIER ':' statement
- goto_statement: 'goto' IDENTIFIER ';'
- return: 'return' [expression] ';'
- var_statement: 'var' IDENTIFIER ['=' expression] {','} ';'
- while_statement: 'while' '(' expression ')' statement
- delete_statement: 'delete' lvalue ';'
- do_statement: 'do' statement 'while' '(' expression ')' ';'
- foreach_statement: 'foreach' '(' ('var') IDENTIFIER 'in' lvalue ')' statement
- for_statement: 'for' '(' (var_statement|expression_statement) expression_statement multi_expression ')' statement
- multi_expression: [expression {',' expression}]
- if_statement: 'if' '(' expression ')' statement ['else' statement]
- switch_statement: 'switch' '(' expression ')' { switch_case }
- switch_case: ('case' literal|'default') ':' statement
- compound_statement: '{' { statements } '}'
- function_expression: 'function' '(' [IDENTIFIER {',' IDENTIFIER}] ')'
- object_field: (IDENTIFIER | STRING) ':' expression
- object_expression: '{' [object_field {',' object_field}] '}'
- array_expression: '[' [expression {',' expression}] ']'
- new_expression: 'new' ('<' expression '>' | '[' expression ']')
- require_expression: 'require' '(' expression ')'
- primary_expression: IDENTIFIER|literal|function_expression|require_expression|'(' expression ')'|new_expression|array_expression|object_expression
- literal: UNSIGNED|FLOAT|STRING|true|false|null
- lvalue: primary_expression {('[' expression ']'|'.' IDENTIFIER|'(' parameter_expression_list ')'|'\'('float'|'signed'|'unsigned')',UNSIGNED,expression'\'}
- parameter_expression_list = [expression {',' expression}]
- postfix_expression: lvalue ('++' | '--')
- unary_expression: ('-'|'+'|'!'|'++'|'--') postfix_expression
- cast_expression: unary_expression ['as' ('float'|'signed'|'unsigned'|'string')]
- multiplicative_expression: cast_expression {('*'|'/'|'%') cast_expression}
- additive_expression: multiplicative_expression {('+' | '-') multiplicative_expression}
- shift_expression: additive_expression {(<<'|'>>'|'<<<'|'>>>') additive_expression}
- binary_expression: shift_expression {('&'|'|'|'^') shift_expression}
- comparison_expression: binary_expression [('<'|'>'|'<='|'>='|'=='|'!=') comparison_expression]
- logical_expression: comparison_expression [('&&'|'||'|'^^') logical_expression]
- conditional_expression: logical_expression ('?' conditional_expression ':' conditional_expression)
- assignment_expression: [lvalue assignment_operator] conditional_expression
- assignment_operator: '='|'*='|'/='|'%='|'+='|'-='|'<<='|'>>='|'<<<='|'>>>='|'&='|'|='|'^='
- expression: assignment_expression
- expression_statement: ';' | expression ';'
- statement: compound_statement | if_statement | delete_statement | for_statement | do_statement | while_statement | switch_statement | goto_statement | return_statement | expression_statement | var_statement | continue_statement | break_statement | label_statement
- main: {statement} EOF
````
