// parses a string (or file) containing Shovel source code into an abstract syntax tree

var Lexer = require('./Lexer.js');

var error = function(lexer, message) {
	console.log('Error - lexer: ' + message);
};

// parse
var parse = function(lexer) {
	var functions = []; // array of functions, add to this as we parse

	// enter a new scope
	var enterScope = function() {};

	// leave a scope
	var leaveScope = function() {};

	// break_statement: 'break' identifier ';'
	var break_statement = function() {
		if(lexer.nextToken() !== 'break')
			error(lexer, "Expected 'break'.");

		if(lexer.nextToken() !== 'IDENTIFIER')
			error(lexer, "Expected an identifier after 'break'.");

		var destination = lexer.getValue();

		if(lexer.nextToken() !== ';')
			error(lexer, "Expected ';' after the break identifier.");

		return {
			operation: "break",
			destination: destination
		};
	};

	// continue_statement: 'continue' identifier ';'
	var continue_statement = function() {
		if(lexer.nextToken() !== 'continue')
			error(lexer, "Expected 'continue'.");

		if(lexer.nextToken() !== 'IDENTIFIER')
			error(lexer, "Expected an identifier after 'continue'.");

		var destination = lexer.getValue();

		if(lexer.nextToken() !== ';')
			error(lexer, "Expected ';' after the continue identifier.");

		return {
			operation: "continue",
			destination: destination
		};
	};

	// label_statement: '.' identifier ':' statement
	var label_statement = function() {
		if(lexer.nextToken() !== '.')
			error(lexer, "Expected '.'.");

		if(lexer.nextToken() !== 'IDENTIFIER')
			error(lexer, "Expected an identifier after '.'.");
		var label = lexer.getValue();

		if(lexer.nextToken() !== ':')
			error(lexer, "Expected ':' after the label name.");

		var stmt = statement;
		return {
			operation: "label",
			label: label
		};
	};

	// goto_statement: 'goto' identifier ';'
	var goto_statement = function() {
		if(lexer.nextToken() !== 'goto')
			error(lexer, "Expected 'goto'.");

		if(lexer.nextToken() !== 'IDENTIFIER')
			error(lexer, "Expected an identifier after 'goto'.");

		var destination = lexer.getValue();

		if(lexer.nextToken() !== ';')
			error(lexer, "Expected ';' after the goto identifier.");

		return {
			operation: "goto",
			destination: destination
		};
	};

	// return: 'return' [expression] ';'
	var return_statement = function() {
		if(lexer.nextToken() !== 'return')
			error(lexer, "Expected 'return'.");

		var value = null;
		if(lexer.peekToken() !== ';')
			value = expression();

		if(lexer.nextToken() !== ';')
			error(lexer, "Expected the return statement to end with ';'.");

		return {
			operation: "return",
			value: value
		};
	};

	// var_statement: 'var' identifier ['=' expression] {','} ';'
	var var_statement = function() {
		if(lexer.nextToken() !== 'var')
			error(lexer, "Expected 'var'.");

		var variables = [];

		do(		
			if(lexer.nextToken() !== 'IDENTIFIER')
				error(lexer, "Expected an identifier when declaring a variable.");

			var name = lexer.getValue();
			var value = null;

			if(lexer.peekToken() === '=') {
				// contains a value
				lexer.nextToken();
				value = expression();
			}
			variables.push({
				name: name,
				value: value
			});
		} while (lexer.nextToken() !== ',');

		if(lexer.getToken() != ')')
			error(lexer, "Expected the 'var' statement to end with ';'.");

		return {
			operation: 'var',
			variables: variables
		};
	};

	// while_statement: 'while' '(' expression ')' statement
	var while_statement = function() {
		if(lexer.nextToken() !== 'while')
			error(lexer, "Expected 'while'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected '(' after 'while'.");

		var cond = expression();

		if(lexer.nextToken() !== ')')
			error(lexer, "Expected ')' after the condition in the while.");

		var stmt = statement();

		return {
			operation: "while",
			condition: cond,
			statement: stmt
		};
	};

	// delete_statement: 'delete' lvalue ';'
	var delete_statement = function() {
		if(lexer.nextToken() !== "delete")
			error(lexer, "Expected 'delete'.");

		var obj = lvalue();
		return {
			operation: "delete",
			object: obj
		};
	};

	// do_statement: 'do' statement 'while' '(' expression ')' ';'
	var do_statement = function() {
		if(lexer.nextToken() !== "do")
			error(lexer, "Expected 'do'.");

		enterScope();
		var stmt = statement();
		leaveScope();

		if(lexer.nextToken() !== "while")
			error(lexer, "Expected 'while' as part of the do-while.");

		if(lexer.nextToken() !== "(")
			error(lexer, "Expected '(' after while as part of the do-while.");

		var cond = expression();

		if(lexer.nextToken() !== ")")
			error(lexer, "Expected ')' to close the while condition as part of the do-while.");

		if(lexer.nextToken() !== ";")
			error(lexer, "Expected ';' at the end of the do-while.");

		return {
			operation: "do",
			statement: stmt,
			condition: cond
		};
	};

	// foreach_statement: 'foreach' '(' ('var') identifier 'in' lvalue ')' statement
	var foreach_statement = function() {
		if(lexer.nextToken() !== "foreach")
			error(lexer, "Expected 'foreach'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected '(' after 'foreach'.");

		var iteratorName, declareIterator;

		enterScope();
		if(lexer.nextToken() === "var") {
			if(lexer.nextToken() !== 'IDENTIFIER')
				error(lexer, "Expected an identifier after 'var' at the start of the foreach.");

			declareIterator = true;
			iteratorName = lexer.getValue();
			// todo: declare it
		} else if(lexer.getToken() === "IDENTIFIER") {
			iteratorName = lexer.getValue();
			declareIterator = false;
			// todo: check if it exists
		} else
			error(lexer, "Expected 'var' or an identifier at the start of the foreach.");

		if(lexer.nextToken() != "in")
			error(lexer, "Expected 'in' after specifying the iterator for the foreach.");

		var obj = lvalue();
		leaveScope();

		var stmt = statement();

		return {
			operation: "foreach",
			iteratorName: iteratorName,
			declareIterator: declareIterator,
			object: obj,
			statement: stmt
		};
	};

	// for_statement: 'for' '(' (var_statement|expression_statement) expression_statement multi_expression ')' statement
	// multi_expression: [expression {',' expression}]
	var for_statement = function() {
		if(lexer.nextToken() !== "for")
			error(lexer, "Expected 'for'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected '(' after 'if'.");

		enterScope();

		var initializer = null;
		if(lexer.peekToken() === "var")
			initializer = var_statement();
		else
			initializer = expression_statement();
		
		var condition = expression_statement();

		var oneach = [];

		if(lexer.peekToken() !== ')') {
			// we have oneach expressions
			oneach.push(expression());

			while(lexer.getToken() === ',')
				oneach.push(expression());

			if(lexer.getToken() !== ')')
				error(lexer, "Expected ')' at the end of the for.");

		} else
			lexer.nextToken(); // jump over the )

		enterScope();
		var stmt = statement();
		leaveScope();
		leaveScope();

		return {
			operation: "for",
			initializer: initializer,
			condition: condition,
			oneach: oneach,
			body: stmt
		};
	};


	// if_statement: 'if' '(' expression ')' statement ['else' statement]
	var if_statement = function() {
		if(lexer.nextToken() !== "if")
			error(lexer, "Expected 'if'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected '(' after 'if'.");

		var cond = expression();

		if(lexer.nextToken() !== ')')
			error(lexer, "Expected ')' after the conditional of the if.");

		enterScope();
		var thenStatement = statement();
		leaveScope();

		var elseStatement = null;

		if(lexer.peekToken() === "else") {
			lexer.nextToken();
			elseStatement = statement();
		}

		return {
			operation: "if",
			thenStatement: thenStatement,
			elseStatement: elseStatement
		};
	};

	// switch_statement: 'switch' '(' expression ')' { switch_case }
	// switch_case: ('case' literal|'default') ':' statement
	var switch_statement = function() {
		if(lexer.nextToken() !== 'switch')
			error(lexer, "Expected 'switch'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected '(' after 'switch'.");

		var cond = expression();

		if(lexer.nextToken() !== ')')
			error(lexer, "Expected ')' to close the switch expression.");

		if(lexer.nextToken() !== '{')
			error(lexer, "Expected '{' as part of the switch statement.");

		var cases = [];
		var defaultCase = null;

		while(lexer.nextToken() !== "}") {
			if(lexer.getToken() == "case") {
				var val = literal();

				if(lexer.nextToken() !== ':')
					error(lexer, "Expected ':' after the case literal in the switch.");

				enterScope();
				var exp = expression();
				leaveScope();

				cases.push({value: val,
					expression: exp});

			} else if(lexer.getToken() == "default") {
				if(defaultCase !== null)
					error(lexer, "The default case is already defined for the switch.");

				if(lexer.nextToken() !== ':')
					error(lexer, "Expected ':' after 'default' in the switch.");

				enterScope();
				defaultCase = expression();
				leaveScope();
			} else
				error(lexer, "Expected 'case', 'default', or '}' in the body of the switch cases.");
		}

		return {
			operation: "switch",
			condition: cond,
			cases: cases,
			defaultCase: defaultCase
		};
	};

	// compound_statement: '{' { statements } '}'
	var compound_statement = function() {
		if(lexer.nextToken() !== '{')
			error(lexer, "Expected '{'.");

		var statements = [];
		enterScope();
		while(lexer.peekToken() !== '}') {
			statements.push(statement);
		}
		lexer.nextToken();
		leaveScope();

		return {
			operation: "compound_statement",
			statements: statements
		};
	};

	// function_expression: 'function' '(' [identifier {',' identifier}] ')'
	var function_expression = function() {
		if(lexer.nextToken() !== 'function')
			error(lexer, "Expected 'function'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected ")

		var parameters = [];
		if(lexer.peekToken() !== ')') {
			// we have parameters

			if(lexer.nextToken() !=== 'IDENTIFIER')
				error(lexer, "Expecting an indentifier as the function's parameter's name.");

			parameters.push(lexer.getValue());

			while(lexer.nextToken() === ',') {
				if(lexer.nextToken() !=== 'IDENTIFIER')
					error(lexer, "Expecting an indentifier as the function's parameter's name.");

				parameters.push(lexer.getValue());
			}

			if(lexer.getToken() !== ')')
				error(lexer, "Expecting a ',' or ')' at the end of the function parameters.");
			}
		}
		else
			lexer.nextToken(); // skip over the )


		enterScope();

		var expr = statement();

		leaveScope();

		// todo:
		// enter function
		// do something with parameters
		// push it
		// leave function
	};

	// object_field: (identifier | string_literal) ':' expression
	var object_field = function() {
		var name = lexer.nextToken();
		if(name !== "IDENTIFIER" && name !== "STRING")
			error(lexer, "Expected an identifier or a string as the name of the object field.");

		name = lexer.getValue();

		if(lexer.nextToken() !== ':')
			error(lexer, "Expected a ':' after the object field's name.");

		var exp = expression();

		return {
			name: name,
			expression: exp
		};
	};

	// object_expression: '{' [object_field {',' object_field}] '}'
	var object_expression = function() {
		if(lexer.nextToken() !== '{')
			error(lexer, "Expecting '{'.");

		var fields = [];

		if(lexer.peekToken() !== '}') {
			// we have fields
			fields.push(object_field());

			while(lexer.nextToken() === ',')
				values.push(object_field());

			if(lexer.getToken() !== '}')
				error(lexer, "Expecting a ',' or '}' at the end of an object literal.");
		}
		else
			lexer.nextToken(); // skip over the }

		return {
			operation: "object_literal",
			type: "object",
			fields: fields
		};
	};

	// array_expression: '[' [expression {',' expression}] ']'
	var array_expression = function() {
		if(lexer.nextToken() !== '[')
			error(lexer, "Expecting '['.");

		var values = [];

		if(lexer.peekToken() !== ']') {
			// we have values
			values.push(expression());

			while(lexer.nextToken() === ',')
				values.push(expression());

			if(lexer.getToken() !== ']')
				error(lexer, "Expecting a ',' or ']' at the end of an array literal.");
		}
		else
			lexer.nextToken(); // skip over the ]

		return {
			operation: "array_literal",
			type: "array",
			values: values
		};
	};

	// new_expression: 'new' ('<' expression '>' | '[' expression ']')
	var new_expression = function() {
		if(lexer.nextToken() !== 'new')
			error(lexer, "Expecting 'new'.");

		var t = lexer.nextToken();
		if(t === '<') {
			var size = expression();

			if(lexer.nextToken() !== '>')
				error(lexer, "Expecting '>' to close the 'new'.");

			return {
				operation: "new_buffer",
				size: size,
				type: "buffer"
			}
		} else if(t === '[') {
			var size = expression();

			if(lexer.nextToken() !== ']')
				error(lexer, "Expecting ']' to close the 'new'.");

			return {
				operation: "new_array",
				size: size,
				type: "array"
			}
		} else
			error(lexer, "Expecting '<' or '[' after 'new'.");
	};

	// require_expression: 'require' '(' expression ')'
	var require_expression = function() {
		if(lexer.nextToken() !== 'require')
			error(lexer, "Expecting 'require'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expecting '(' after 'require'.");

		var exp = expression();

		if(lexer.nextToken() !== ')')
			error(lexer, "Expecting ')' to close the 'require'.");

		return {
			operation: "require",
			path: exp
		};
	};

	// primary_expression: identifier|literal|function_expression|require_expression|'(' expression ')'|new_expression|array_expression|object_expression
	// literal: unsigned|float|string|true|false|null
	var primary_expression = function() {
		var t = lexer.peekToken();
		if(t === 'IDENTIFIER') {
			lexer.nextToken();
			// todo: check if variable exists
			return {
				operation: "variable_access",
				name: lexer.getValue()
			};
		} else if(t === 'UNSIGNED') {
			lexer.nextToken();
			return {
				operation: "literal",
				type: "unsigned",
				value: lexer.getValue()
			};
		} else if(t === 'FLOAT') {
			lexer.nextToken();
			return {
				operation: "literal",
				type: "float",
				value: lexer.getValue()
			};
		} else if(t === 'true') {
			lexer.nextToken();
			return {
				operation: "literal",
				type: "boolean",
				value: "true"
			};
		} else if(t === 'false') {
			lexer.nextToken();
			return {
				operation: "literal",
				type: "boolean",
				value: "false"
			};
		} else if(t === 'null') {
			lexer.nextToken();
			return {
				operation: "literal",
				type: "null",
				value: "null"
			};
		} else if(t === 'function')
			return function_expression();
		else if(t === 'require')
			return require_expression();
		else if(t === '(') {
			lexer.nextToken();
			var exp = expression();
			if(lexer.nextToken() !== ')')
				error(lexer, "Expecting to close the expression with ')'.");
			return exp;
		}
		else if(t === 'new')
			return new_expression();
		else if(t === '[')
			return array_expression();
		else if(t === '{')
			return object_expression();
		else
			error(lexer, "Unexpected token.");
	};

	// lvalue: primary_expression {('[' expression ']'|'.' identifier|'(' parameter_expression_list ')'|'<'('float'|'signed'|'unsigned')','unsigned',expression'>'}
	// parameter_expression_list = [expression {',' expression}]
	var lvalue = function() {
		var left = primary_expression();

		var t = lexer.peekToken();
		while(t === '[' || t === '.' || t === '<' || t === '(') {
			lexer.nextToken();

			if(t === '[') { // array access
				var exp = expression();
				if(lexer.nextToken() !== ']')
					error(lexer, "Expecting ']' to close an array access.");

				left = {
					operation: "array_access",
					array: left,
					element: expression
				};
			} else if(t === '.') { // property access
				var property = expression();
				if(lexer.nextToken() !== 'IDENTIFIER')
					error(lexer, "Expecting an identifier after '.' when refering to a property.");

				left = {
					operation: "property_access",
					object: left,
					identifier: lexer.getValue()
				};
			} else if(t === '(') { // function call
				var parameters = [];

				if(lexer.peekToken() !== ')') {
					// we have parameters

					parameters.push(expression());

					while(lexer.nextToken() === ',')
						parameters.push(expression());

					if(lexer.getToken() !== ')')
						error(lexer, "Expecting a ',' or ')' at the end of the function call parameters.");
				}
				else
					lexer.nextToken(); // skip over the )

				left = {
					operation: "function_call",
					_function: left,
					parameters: parameters
				};
			} else if(t === '<') { // buffer access
				var type = lexer.nextToken();
				if(type !== 'float' && type !== 'signed' && type === 'unsigned')
					error(lexer, "Buffer access can only be of types float, signed, or unsigned.");

				if(lexer.nextToken() !== ',')
					error(lexer, "Expecting ',' after the value type in the buffer access.");

				if(lexer.nextToken() !== 'UNSIGNED')
					error(lexer, "Expecting an buffer access size as an unsigned integer.");

				var size = lexer.getValue();
				if(size !== 8 && size !== 16 && size !== 32 && size !== 64)
					error(lexer, "Buffer access can only be of sizes 8, 16, 32, or 64-bits.");

				if(size === 8 && type === 'float')
					error(lexer, "8-bit float is not valid for buffer access.");

				if(lexer.nextToken() !== '>')
					error(lexer, "Expecting '>' to close a buffer access.");

				left = {
					operation: "buffer_access",
					buffer: left,
					type: type,
					size: size
				};
			} else
				error(lexer, "Internal compiler error in Parser.parse.lvalue().");

			t = lexer.peekToken();
		}

		return left;
	};

	// postfix_expression: lvalue ('++' | '--')
	var postfix_expression = function() {
		var left = lvalue();

		var t = lexer.peekToken();
		if(t === '++' || t === '--') {
			lexer.nextToken();
			return {
				operation: "postfix_expression",
				operator: t,
				expression: left;
			};
		} else
			return left;
	};

	// unary_expression: ('-'|'+'|'!'|'++'|'--') postfix_expression
	var unary_expression = function() {
		var t = lexer.peekToken();
		if(t === '-' || t === '+' || t === '!' || t === '+' || t === '-') {
			lexer.nextToken();
			var right = postfix_expression();

			return {
				operation: "unary_expression",
				operator: t,
				expression: right
			};
		} else
			return postfix_expression();
	};

	// cast_expression: unary_expression ['as' ('float'|'signed'|'unsigned'|'string')]
	var cast_expression = function() {
		var left = unary_expression();

		if(lexer.peekToken() === 'as') {
			lexer.nextToken();
			var t = lexer.nextToken();
			if(t !== 'float' && t !== 'signed' && t !== 'unsigned' && t !== 'string' && t !== "boolean")
				error(lexer, "Can only cast to float, signed, unsigned, string, and boolean.");

			return {
				operation: "cast",
				expression: left,
				type: t
			};
		}
	};

	// multiplicative_expression: cast_expression {('*'|'/'|'%') cast_expression}
	var multiplicative_expression = function() {
		var left = cast_expression();
		var t = lexer.peekToken();
		while(t === '*' || t === '/' || t === '%') {
			lexer.nextToken();
			var right = cast_expression();

			left = {
				operation: "math",
				left: left,
				operator: t,
				right: right
			};
		}
		return left;
	};

	// additive_expression: multiplicative_expression {('+' | '-') multiplicative_expression}
	var additive_expression = function() {
		var left = multiplicative_expression();

		var t = lexer.peekToken();
		while(t === '+' || t === '-') {
			lexer.nextToken();
			var right = multiplicative_expression();

			left = {
				operation: "math",
				left: left,
				operator: t,
				right: right
			};
		}
		return left;
	};

	// shift_expression: additive_expression {(<<'|'>>'|'<<<'|'>>>') additive_expression}
	var shift_expression = function() {
		var left = additive_expression();

		var t = lexer.peekToken();
		while(t === '<<' || t === '>>' || t === '<<<' || t === '>>>') {
			lexer.nextToken();
			var right = additive_expression();

			left = {
				operation: "math",
				left: left,
				operator: t,
				right: right
			};

			t = lexer.peekToken();
		}

		return left;
	};

	// binary_expression: shift_expression {('&'|'|'|'^') shift_expression}
	var binary_expression = function() {
		var left = shift_expression();
		var t = lexer.peekToken();

		while(t === '&' || t === '|' || t === '^') {
			lexer.nextToken();
			var right = shift_expression();

			left = {
				operation: "math",
				left: left,
				operator: t,
				right: right
			};

			t = lexer.peekToken();
		}

		return left;
	};

	// comparison_expression: binary_expression [('<'|'>'|'<='|'>='|'=='|'!=') comparison_expression]
	var comparison_expression = function() {
		var left = binary_expression();
		var t = lexer.peekToken();
		if(t === '<' || t === '>' || t === '<=' || t === '>=' || t === '==' || t === '!=') {
			lexer.nextToken();
			var right = comparison_expression();

			return {
				operation: "comparison",
				left: left,
				operator: t,
				right: right
			};
		} else
			return left;
	};

	// logical_expression: comparison_expression [('&&'|'||'|'^^') logical_expression]
	var logical_expression = function() {
		var left = comparison_expression();
		var t = lexer.peekToken();
		if(t === '&&' || t === '||' || t === '^^') {
			lexer.nextToken();

			var right = logical_expression();

			return {
				operation: "logical_expression",
				left: left,
				operator: t,
				right: right
			}

		} else
			return left;
	};

	// conditional_expression: logical_expression ('?' conditional_expression ':' conditional_expression)
	var conditional_expression = function() {
		var left = logical_expression();

		var t = lexer.peekToken();
		if(t === '?') {
			lexer.nextToken();
			var trueExp = conditional_expression();
			if(lexer.nextToken() !== ':')
				error(lexer, "Conditional '?:' was expecting ':'.")
			var falseExp = conditional_expression();

			return {
				operation: "inline_if",
				condition: left,
				trueExpression: trueExp,
				falseExpression: falseExp
			};
		} else
			return left;
	};

	// assignment_expression: [lvalue assignment_operator] conditional_expression
	// assignment_operator: '='|'*='|'/='|'%='|'+='|'-='|'<<='|'>>='|'<<<='|'>>>='|'&='|'|='|'^='
	var assignment_expression = function() {
		var left = conditional_expression();

		var t = lexer.peekToken();
		if(t === '=' || t === '*=' || t === '/=' || t === '%=' || t === '+=' || t === '-=' || t === '<<=' || t === '>>=' ||
			t === '<<<=' || t === '>>>=' || t === '&=' || t === '|=' || t === '^=') {
			if(left.type !=== 'lvalue')
				console.log("The value being assigned left of the '" + t + '" is not an lvalue.');

			lexer.nextToken();
			var right = conditional_expression();
			return {
				operation: "assignment",
				target: left,
				operator: t,
				value: expression
			}
		} else
			return left;
	};

	// expression: assignment_expression
	//  {',' assignment_expression}
	var expression = function() {
		return assignment_expression();
	};


	// expression_statement: ';' | expression ';'
	var expression_statement = function() {
		var t = lexer.peekToken();
		if(t === ';') {
			lexer.nextToken();
			return null;
		}

		var exp = expression();
		if(lexer.nextToken() !== ';')
			error("Expecting the expression to end with ';'.");

		return exp;
	};

	// statement: compound_statement | if_statement | delete_statement | for_statement | do_statement | while_statement | switch_statement
	//            goto_statement | return_statement | expression_statement | var_statement | continue_statement | break_statement | label_statement
	var statement = function() {
		var t = lexer.peekToken();
		if(t == '{')
			return compound_statement();
		else if(t === 'if')
			return if_statement();
		else if(t === 'delete')
			return delete_statement();
		else if(t === 'for')
			return for_statement();
		else if(t === 'do')
			return do_statement();
		else if(t === 'while')
			return while_statement();
		else if(t === 'switch')
			return switch_statement();
		else if(t === 'goto')
			return goto_statement();
		else if(t === 'return')
			return return_statement();
		else if(t === 'var')
			return var_statement();
		else if(t === 'continue')
			return continue_statement();
		else if(t === 'break')
			return break_statement();
		else if(t === '.')
			return label_statement();
		else
			return expression_statement();
	};


	// main: {statement} EOF
	var main = function() {
		var statements = [];
		if(lexer.peekToken() !== 'EOF') { // loop until end of the file
			var s = statement();
			if(s !== null)
				statements.push(s);
		}

		// add statements to first function
		console.log(statements);
	};

	// read the main/body
	main();

	return functions;
};

// parse a file
exports.parseFile = function(path) {
	return parse(Lexer.parseFile(path));
};

// parse a string
exports.parseString = function(str) {
	return parse(Lexer.parseString(str));
};