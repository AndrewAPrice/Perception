// parses a string (or file) containing Shovel source code into an abstract syntax tree

var Lexer = require('./Lexer.js');

var showTokens = true;

var error = function(lexer, message) {
	if(showTokens)
		console.log('Error - lexer: ' + message + " (Current token: " + lexer.getToken() + " Next token: " + lexer.peekToken() + ")");
	else
		console.log('Error - lexer: ' + message);

	process.exit(-1);
};

// debugging mode, no optimizations, don't flatten the stack, output column numbers everywhere
var debug = false;

// parse
var parse = function(lexer) {
	var functions = []; // array of functions, add to this as we parse

	var currentFunction = -1;

	var currentScope = null;

	// enter a new scope
	var enterScope = function() {
		var parent = currentScope;
		currentScope = {
			parentScope: currentScope,
			variables: {}, // variables, the key is the variable name
			variableNames: [], // array of variable names, in the order in which they were added
			children: [],
			functionId: currentFunction // the function this scope is part of, used to determine if variables are defined in a closure
		};

		if(parent != null)
			parent.children.push(currentScope);
	};

	// leave a scope
	var leaveScope = function() {
		currentScope = currentScope.parentScope;
	};

	// create a parameter and add it to the local scope
	var declareVariable = function(name, parameter, type, literal) {
		if(currentScope.variables[name] !== undefined) {
			if(parameter)
				error(lexer, "A variable by the name of '" + name +"' is being defined multiple times in the same scope.");
			else
				error(lexer, "The name '" + name + "' is being used for multiple parameters.");
		}

		currentScope.variables[name] = {
			closure: false, // true if accessed by another function and should be made a closure
			constant: false, // false if it's assigned when it's not declared
			parameter: parameter, // true if this is a parameter and not a local variable
			// if a closure is also a parameter, it should be demoted to the child (with '.' prefixed because that's not a valid identifier character)
			// then copied when opened
			type: type, // the parameter's type
			literal: literal // if we are a literal
		};

		currentScope.variableNames.push(name);
	};

	// touch a variable - determines if a variable exists, if it is used for writing, if it is a closure
	var touchVariable = function(name, writing) {
		// walk backwards through the scopes until we find the variable
		var scope = currentScope;
		while(scope !== null) {
			var v = scope.variables[name];

			if(v !== undefined) {
				// we found a variable by that name
				if(writing) {
					// if we are assigning to it, remove any constant info attached to it
					v['type'] = '?';
					v.literal = null;
				}

				if(scope.functionId != currentFunction)
					v.closure = true; // this variable is a closure because we're accessing it outside of the function it was defined in

				return; // quit, because we found it
			}
			scope = scope.parentScope;
		}

		error(lexer, "No variable by the name of '" + name + "' has been declared.");
	};

	// enter a function
	var enterFunction = function(params) {
		// add default function
		functions.push({
			labels: {}, // for jumping to
			labelRefs: [], // that want to jump places
			rootScope: null, // the root scope
			parameters: [],
			previousFunction: currentFunction, // reference when leaving the function
			statements: null,
			functionId: functions.length
		});

		currentFunction = functions.length - 1;
		enterScope();

		functions[currentFunction].rootScope = currentScope;
		
		// add parameters to the current scope
		for(var i = 0; i < params.length; i++)
			declareVariable(params[i], true);

		// enter a new scope (hiding parameters in the parent scope)
		enterScope();
	};

	// leave a function,
	var leaveFunction = function() {
		var f = functions[currentFunction];

		// check label references
		for(var i = 0; i < f.labelRefs.length; i++) {
			if(f.labels[f.labelRefs[i].destination] === undefined)
				error(lexer, "The label '"+ f.labelRefs[i].destination + "' is undefined.");
		}

		currentScope = f.rootScope.parentScope; // leave back to the scope we we're in just before declaring the function

		currentFunction = functions[currentFunction].previousFunction;

		// scan the scope of this function
		var s = scanScope(f);
		f.numLocals = s.locals;
		f.numClosures = s.closures; 
	};

	// scan the scope - it figures out how many local/closure variables to allocate,
	// and what their variable number is
	var scanScope = function(f) {
		// temporary variables while scanning through our scopes
		// max closures
		var totalClosures = 0;
		// deepest local variable
		var deepestLocal = 0;

		var scanRecursive = function(scope, localDepth) {
			if(debug) // in debug compilation we'll give every local variable it's own scope
						// in release compilation we will shrink the local variables to only it's deepest size
						// so different scopes can use the same local variable stack
				localDepth = deepestLocal;

			for(var i = 0; i < scope.variableNames.length; i++) {
				var v = scope.variables[scope.variableNames[i]];
				if(v.closure) {
					v.stackNumber = totalClosures;
					totalClosures++;
				} else {
					v.stackNumber = localDepth;
					localDepth++;
				}
			}

			if(localDepth > deepestLocal)
				deepestLocal = localDepth; // new deepest

			for(var i = 0; i < scope.children.length; i++)
				if(scope.children[0].functionId == f.functionId)
				scanRecursive(scope.children[i], localDepth);
		};

		var scope = f.rootScope;

		// scan the parameters first (root scope)
		var closure = [];

		for(var i = 0; i < scope.variableNames.length; i++) {
			var v = scope.variables[scope.variableNames[i]];
			if(v.closure) {
				// this parameter is a closure, copy it to the child so other functions can access it
				v.stackNumber = totalClosures;

				scope.children[0].variables["." + scope.variables[i]] = {
					closure: true,
					constant: false,
					parameter: false,
					type: undefined,
					literal: undefined,
					stackNumber: totalClosures
				};

				// put a copy statement at the start of the statement
				f.statements.splice(0, 0, {
					operation: "copyParameter",
					local: deepestLocal,
					closure: totalClosures
				});

				totalClosures++;

				deepestLocal++; // parameter is also on stack list
			} else {
				v.stackNumber = deepestLocal;
				deepestLocal++;
			}

		};

		// scan children recursively
		scanRecursive(scope.children[0], deepestLocal);

		console.log("Deepest local: " + deepestLocal + " Closures: " + totalClosures);

		return {locals: deepestLocal, closures: totalClosures};
	};


	// break_statement: 'break' identifier ';'
	var break_statement = function() {
		if(lexer.nextToken() !== 'break')
			error(lexer, "Expected 'break'.");

		if(lexer.nextToken() !== 'IDENTIFIER')
			error(lexer, "Expected an identifier after 'break'.");

		var destination = lexer.getValue();

		if(lexer.nextToken() !== ';')
			error(lexer, "Expected ';' after the break identifier.");

		var _break = {
			operation: "break",
			destination: destination
		};

		functions[currentFunction].labelRefs.push(_break);

		return _break;
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

		var _continue = {
			operation: "continue",
			destination: destination
		};

		functions[currentFunction].labelRefs.push(_continue);
		return _continue;
	};

	// label_statement: '.' identifier ':' statement
	var label_statement = function() {
		if(lexer.nextToken() !== '.')
			error(lexer, "Expected '.'.");

		if(lexer.nextToken() !== 'IDENTIFIER')
			error(lexer, "Expected an identifier after '.'.");
		var l = lexer.getValue();

		if(lexer.nextToken() !== ':')
			error(lexer, "Expected ':' after the label name. ");

		var stmt = statement();

		var label = {
			operation: "label",
			label: l,
			statement: stmt
		};

		if(functions[currentFunction].labels[l] !== undefined)
			error(lexer, "The label '"+l+"' is defined multiple times!");

		functions[currentFunction].labels[l] = label;

		return label;
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

		var _goto = {
			operation: "goto",
			destination: destination
		};

		functions[currentFunction].labelRefs.push(_goto);

		return _goto;
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

		do {
			if(lexer.nextToken() !== 'IDENTIFIER')
				error(lexer, "Expected an identifier when declaring a variable. " + lexer.getToken());

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

			declareVariable(name, false, undefined, undefined);//type, literal);
		} while (lexer.nextToken() === ',');

		if(lexer.getToken() !== ';')
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

			declareVariable(iteratorName, false, null, null);
		} else if(lexer.getToken() === "IDENTIFIER") {
			iteratorName = lexer.getValue();
			declareIterator = false;

			// todo: check if it exists
			touchVariable(iteratorName, true);
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

			while(lexer.nextToken() === ',')
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
			statements.push(statement());
		}
		lexer.nextToken();
		leaveScope();

		return {
			operation: "compound_statement",
			statements: statements
		};
	};

	// function_expression: 'function' '(' [identifier {',' identifier}] ')' (statement | '{' statements '}')
	var function_expression = function() {
		if(lexer.nextToken() !== 'function')
			error(lexer, "Expected 'function'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected ")

		var parameters = [];
		if(lexer.peekToken() !== ')') {
			// we have parameters

			if(lexer.nextToken() !== 'IDENTIFIER')
				error(lexer, "Expecting an indentifier as the function's parameter's name.");

			parameters.push(lexer.getValue());

			while(lexer.nextToken() === ',') {
				if(lexer.nextToken() !== 'IDENTIFIER')
					error(lexer, "Expecting an indentifier as the function's parameter's name.");

				parameters.push(lexer.getValue());
			}

			if(lexer.getToken() !== ')')
				error(lexer, "Expecting a ',' or ')' at the end of the function parameters.");
		}
		else
			lexer.nextToken(); // skip over the )


		enterFunction(parameters);

		var stmts = []

		if(lexer.peekToken() === '{') {
			// block of statements
			lexer.nextToken();
			while(lexer.peekToken() !== '}') {
				stmts.push(statement());
			}
			lexer.nextToken();
		} else // just one statement
			stmts.push(statement());
		functions[currentFunction].statements = stmts;

		leaveFunction();
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

			var name = lexer.getValue();
			// check if variable exists
			touchVariable(name, true);
			return {
				operation: "variable_access",
				name: name
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
			error(lexer, "Unexpected token: " + t);
	};

	// lvalue: primary_expression {('[' expression ']'|'.' identifier|'(' parameter_expression_list ')'|'<|'('float'|'signed'|'unsigned')',UNSIGNED,expression'|>''}
	// parameter_expression_list = [expression {',' expression}]
	var lvalue = function() {
		var left = primary_expression();

		var t = lexer.peekToken();
		while(t === '[' || t === '.' || t === '<|' || t === '(') {
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
			} else if(t === '<|') { // buffer access
				var type = lexer.nextToken();
				if(type !== 'float' && type !== 'signed' && type === 'unsigned')
					error(lexer, "Buffer access can only be of types float, signed, or unsigned.");

				if(lexer.nextToken() !== ',')
					error(lexer, "Expecting ',' after the value type in the buffer access.");

				if(lexer.nextToken() !== 'UNSIGNED')
					error(lexer, "Expecting a buffer access size as an unsigned integer.");

				var size = lexer.getValue();
				if(size !== '8' && size !== '16' && size !== '32' && size !== '64')
					error(lexer, "Buffer access can only be of sizes 8, 16, 32, or 64-bits.");

				if(size === '8' && type === 'float')
					error(lexer, "8-bit float is not valid for buffer access.");

				if(lexer.nextToken() !== ',')
					error(lexer, "Expecting ',' after the value size in the buffer access.");

				var addr = expression();

				if(lexer.nextToken() !== '|>')
					error(lexer, "Expecting '|>' to close a buffer access.");

				left = {
					operation: "buffer_access",
					buffer: left,
					type: type,
					size: size,
					address: addr
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

			// touch variable
			if(left.operation == "variable_access")
				touchVariable(left['name'], true);

			return {
				operation: "postfix_expression",
				operator: t,
				expression: left
			};
		} else
			return left;
	};

	// unary_expression: ('-'|'+'|'!'|'++'|'--') postfix_expression
	var unary_expression = function() {
		var t = lexer.peekToken();
		if(t === '-' || t === '+' || t === '!' || t === '++' || t === '--') {
			lexer.nextToken();
			var right = postfix_expression();

			if(right.operation == "variable_access")
				touchVariable(right['name'], true);

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
		} else
			return left;
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
			if(left.operation !== 'variable_access' && left.operation !== 'array_access' && left.operation !== 'property_access' && left.operation !== 'buffer_access')
				error(lexer, "The value being assigned left of the '" + t + "' is not an assignable lvalue.");

			if(left.operation === 'variable_access')
				touchVariable(left.name, true);

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
			error(lexer, "Expecting the expression to end with ';'.");

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
		while(lexer.peekToken() !== 'EOF') { // loop until end of the file
			var s = statement();
			if(s !== null)
				statements.push(s);
		}

		functions[currentFunction].statements = statements;

		// add statements to first function
		//console.log(statements);
		//console.log(currentScope);
	};

	// read the main/body
	enterFunction(["exports"]);
	main();
	leaveFunction();

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

exports.parseFile('test.src');