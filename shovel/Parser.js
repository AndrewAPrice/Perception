// parses a string (or file) containing Shovel source code into an abstract syntax tree

var Lexer = require('./Lexer.js');
var fs = require('fs');

// check arguments
var invalidParams = false;
var inputFile = null;
var outputFile = null;
// debugging mode, no optimizations, don't flatten the stack, output column numbers everywhere
var debug = false;
// verbose mode?
var verbose = false;
// show tokens
var showTokens = false;

if(process.argv.length < 4)
	invalidParams = true;
else {
	for(var i = 2; i < process.argv.length - 2; i++) {
		switch(process.argv[i]) {
			case "-d": debug = true; break;
			case "-v": verbose = true; break;
			default: console.log("Unknown parameter " + process.argv[i] + "."); invalidParams = true; break;
		}
	}

	if(!invalidParams) {
		// last two parameters are input and output files
		inputFile = process.argv[process.argv.length - 2];
		outputFile = process.argv[process.argv.length - 1];
	}
}

if(invalidParams) {
	console.log("Usage is: node Assembler.js -d -v input output");
	console.log(" -d Optional parameter to include debugging information and skip optimizations. This will grow the file!");
	console.log(" -v Be verbose about each step!");
	console.log(" -t Show lexical tokens on errors.");
	console.log(" input - Path to the input Shovel source (must be 2nd to last parameter).");
	console.log(" output - Path to where to output the assembly code (must be last parameter).");
	process.exit(-1);
}

var showTokens = true;

var error = function(lexer, message) {
	if(showTokens)
		console.log('Error - parser: ' + message + " (Current token: " + lexer.getToken() + " Next token: " + lexer.peekToken() + ")");
	else
		console.log('Error - parser: ' + message);

	process.exit(-1);
};

// parse
var parse = function(lexer) {
	var functions = []; // array of functions, add to this as we parse

	var currentFunction = -1;

	var currentScope = null;

	var labelNum = 0; // increments as we make labels

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

		var v = {
			closure: false, // true if accessed by another function and should be made a closure
			constant: false, // false if it's assigned when it's not declared
			parameter: parameter, // true if this is a parameter and not a local variable
			// if a closure is also a parameter, it should be demoted to the child (with '.' prefixed because that's not a valid identifier character)
			// then copied when opened
			type: type, // the parameter's type
			literal: literal, // if we are a literal
			functionId: currentFunction
		};

		currentScope.variables[name] = v;

		currentScope.variableNames.push(name);

		return v;
	};

	// touch a variable - determines if a variable exists, if it is used for writing, if it is a closure
	var touchVariable = function(name, writing) {
		if(typeof name === "object") {
			// passing in an object
			if(writing) {
				// if we are assigning to it, remove any constant info attached to it
				name['type'] = '?';
				name.literal = null;
			}

			if(name.functionId != currentFunction)
				name.closure = true; // this variable is a closure because we're accessing it outside of the function it was defined in

		} else {
			// passing in a name
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

					return v; // quit, because we found it
				}
				scope = scope.parentScope;
			}

			error(lexer, "No variable by the name of '" + name + "' has been declared.");
		}
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
			functionId: functions.length,
			// if there's a .replacedBy property then we should use that property when compiling
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
			var node = f.labelRefs[i];
			var label = f.labels[node.destination];

			if(label === undefined)
				error(lexer, "The label '"+ node.destination + "' is undefined.");

			var type = node.operation;
			if(type == "continue") {
				if(label.continueLabel === undefined)
					error(lexer, "The label '" + node.destination + "' is not a control statement that can be continued.");
				node.operation = "goto";
				node.destination = label.continueLabel;
			} else if(type == "break") {
			 	if(label.breakLabel === undefined)
					error(lexer, "The label '" + node.destination + "' is not a control statement that can be broken.");

				node.operation = "goto";
				node.destination = label.breakLabel;
			} else
				// normal goto
				node.destination = label.label;
		}

		currentScope = f.rootScope.parentScope; // leave back to the scope we we're in just before declaring the function

		currentFunction = functions[currentFunction].previousFunction;

		// scan the scope of this function
		var s = scanScope(f);
		f.numLocals = s.locals;
		f.numClosures = s.closures; 

		if(verbose)
			console.log("Leaving function - locals: " + f.numLocals + ", closures: " + f.numClosures);
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


				v.replacedBy = scope.children[0].variables["." + scope.variables[i]] = {
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

		// console.log("Deepest local: " + deepestLocal + " Closures: " + totalClosures);

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
			label: labelNum,
			statement: stmt,
		};

		labelNum++;

		if(stmt.operation == "do" || stmt.operation == "for" || stmt.operation == "while" || stmt.operation == "foreach") {
			label.continueLabel = stmt.continueLabel;
			label.breakLabel = stmt.breakLabel;
		}

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

		var assignments = [];

		do {
			if(lexer.nextToken() !== 'IDENTIFIER')
				error(lexer, "Expected an identifier when declaring a variable. " + lexer.getToken());

			var name = lexer.getValue();
			var value = null;
			var v = declareVariable(name, false, undefined, undefined);//type, literal);

			if(lexer.peekToken() === '=') {
				// contains a value
				lexer.nextToken();
				value = expression();

			assignments.push({
				operation: "assignment",
				target: {
						operation: "variable_access",
						variable: v
					},
				operator: '=',
				value: value
			});

			}
		} while (lexer.nextToken() === ',');

		if(lexer.getToken() !== ';')
			error(lexer, "Expected the 'var' statement to end with ';'.");

		return {
			operation: 'var',
			assignments: assignments
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

		var bodyLabel = labelNum;
		labelNum++;
		var continueLabel = labelNum;
		labelNum++;
		var breakLabel = labelNum;
		labelNum++;

		return {
			operation: "while",
			condition: cond,
			statement: stmt,
			bodyLabel: bodyLabel,
			continueLabel: continueLabel,
			breakLabel: breakLabel
		};
	};

	// delete_statement: 'delete' lvalue ';'
	var delete_statement = function() {
		if(lexer.nextToken() !== "delete")
			error(lexer, "Expected 'delete'.");

		var obj = lvalue();
		if(lvalue.operation !== "array_access" && lvalue !== "property_access")
			error(lexer, "Delete only works on elements or properties elements.");
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

		var bodyLabel = labelNum;
		labelNum++;
		var continueLabel = labelNum;
		labelNum++;
		var breakLabel = labelNum;
		labelNum++;
		return {
			operation: "do",
			statement: stmt,
			condition: cond,
			bodyLabel: bodyLabel,
			continueLabel: continueLabel,
			breakLabel: breakLabel
		};
	};

	// foreach_statement: 'foreach' '(' ('var') identifier 'in' lvalue ')' statement
	var foreach_statement = function() {
		if(lexer.nextToken() !== "foreach")
			error(lexer, "Expected 'foreach'.");

		if(lexer.nextToken() !== '(')
			error(lexer, "Expected '(' after 'foreach'.");

		var publicIterator, declareIterator;

		enterScope();

		if(lexer.nextToken() === "var") {
			if(lexer.nextToken() !== 'IDENTIFIER')
				error(lexer, "Expected an identifier after 'var' at the start of the foreach.");

			declareIterator = true;

			publicIterator = declareVariable(lexer.getValue(), false, null, null);
		} else if(lexer.getToken() === "IDENTIFIER") {
			declareIterator = false;

			publicIterator = touchVariable(lexer.getValue(), true);
		} else
			error(lexer, "Expected 'var' or an identifier at the start of the foreach.");

		if(lexer.nextToken() != "in")
			error(lexer, "Expected 'in' after specifying the iterator for the foreach.");

		var obj = lvalue();
		leaveScope();

		var stmt = statement();
		var bodyLabel = labelNum;
		labelNum++;
		var continueLabel = labelNum;
		labelNum++;
		var breakLabel = labelNum;
		labelNum++;

		return {
			operation: "foreach",
			internalIterator: declareVariable(".iterator", false, null, null), // must generate an iterator behind the foreach loop
			internalObjCounter: declareVariable(".objCount", false, null, null),
			internalObj: declareVariable(".obj", false, null, null),
			publicIterator: publicIterator,
			declareIterator: declareIterator,
			object: obj,
			statement: stmt,
			bodyLabel: bodyLabel,
			continueLabel: continueLabel,
			breakLabel: breakLabel
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

		var continueLabel = labelNum;
		labelNum++;
		var breakLabel = labelNum;
		labelNum++;

		return {
			operation: "for",
			initializer: initializer,
			condition: condition,
			oneach: oneach,
			body: stmt,
			bodyLabel: bodyLabel,
			continueLabel: continueLabel,
			breakLabel: breakLabel
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

		var elseLabel = labelNum;
		labelNum++;
		var endLabel = labelNum;
		labelNum++;

		return {
			operation: "if",
			condition: cond,
			thenStatement: thenStatement,
			elseStatement: elseStatement,
			elseLabel: elseLabel,
			endLabel: endLabel
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
				var stmt = statement();
				leaveScope();
				var endLabel = labelNum;
				labelNum++;

				cases.push({value: val,
					statement: stmt,
					endLabel: endLabel});

			} else if(lexer.getToken() == "default") {
				if(defaultCase !== null)
					error(lexer, "The default case is already defined for the switch.");

				if(lexer.nextToken() !== ':')
					error(lexer, "Expected ':' after 'default' in the switch.");

				enterScope();
				defaultCase = statement();
				leaveScope();
			} else
				error(lexer, "Expected 'case', 'default', or '}' in the body of the switch cases.");
		}


		var endLabel = labelNum;
		labelNum++;

		return {
			operation: "switch",
			condition: cond,
			cases: cases,
			defaultCase: defaultCase,
			endLabel: endLabel
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
				error(lexer, "Expecting an identifier as the function's parameter's name.");

			parameters.push(lexer.getValue());

			while(lexer.nextToken() === ',') {
				if(lexer.nextToken() !== 'IDENTIFIER')
					error(lexer, "Expecting an identifier as the function's parameter's name.");

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

		var func = currentFunction;
		functions[currentFunction].statements = stmts;

		leaveFunction();

		return {
			operation: "function_literal",
			functionId: func
		};
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
			};
		} else if(t === '[') {
			var size = expression();

			if(lexer.nextToken() !== ']')
				error(lexer, "Expecting ']' to close the 'new'.");

			return {
				operation: "new_array",
				size: size,
				type: "array"
			};
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
			variable = touchVariable(name, true);
			return {
				operation: "variable_access",
				variable: variable
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
		} else if(t == 'STRING') {
			lexer.nextToken();
			return {
				operation: "literal",
				type: "string",
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
			if(left.operation === "variable_access")
				touchVariable(left.variable, true);
			else if(left.operation !== "array_access" && left.operation !== "property_access" && left.operation !== "buffer_access")
				error(lexer, "Postfix expressions can only operate on non-static lvalues.");

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
				touchVariable(right.variable, true);
			else if(left.operation !== "array_access" && left.operation !== "property_access" && left.operation !== "buffer_access")
				error(lexer, "Unary modifiers can only operate on non-static lvalues.");

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
			t = lexer.peekToken();
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

			var breakLabel = labelNum;
			labelNum++;

			return {
				operation: "logical_expression",
				left: left,
				operator: t,
				right: right,
				breakLabel: breakLabel
			};

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

		var elseLabel = labelNum;
		labelNum++;
		var endLabel = labelNum;
		labelNum++;

			return {
				operation: "inline_if",
				condition: left,
				trueExpression: trueExp,
				falseExpression: falseExp,
				elseLabel: elseLabel,
				endLabel: endLabel
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
				touchVariable(left.variable, true);

			lexer.nextToken();
			var right = conditional_expression();
			return {
				operation: "assignment",
				target: left,
				operator: t,
				value: right
			};
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

peepHoleOptimizations = function(instructions) {

};

// quotes and encodes a string
var encodeString = function(str) {
	var out = '"';

	for(var i = 0; i < str.length; i++) {
		var c = str[i];
		if(c == '\n')
			out += "\\n";
		else if(c == '\r')
			out += "\\r";
		else if(c =='\t')
			out += "\\t";
		else if(c == "\\")
			out += "\\\\";
		else if(c == '"')
			out += '\\"';
		else
			out += c;
	}

	out += '"';
	return out;
};

// compile a file
exports.compile = function(funcs) {
	var f = fs.createWriteStream(outputFile);

	// compile each function
	for(var i = 0; i < funcs.length; i++) {
		var thisFunc = funcs[i];

		var instructions = [];

		// get a closure variable's stack number
		var getClosureNumber = function(v) {
			var func = i;

			var toAdd = 0; // number to add to the closure variable

			while(func != v.functionId) { // loop until we get into that function
				toAdd += funcs[func].numClosures;
				func = funcs[func].previousFunction;
			}

			return toAdd + v.stackNumber;
		};



		// node - node to compile
		// expectReturn - if we are expecting something to be returned
		var compileNode = function(node, expectReturn) {
//			if(debug)
//				instructions.push("#" + node.operation);

			switch(node.operation) {
				// {operation: "copyParameter", local: deepestLocal, closure: totalClosures}
				case "copyParameter":
					instructions.push("Load " + node.local);
					instructions.push("StoreClosure " + node.closure);
					break;
				// {operation: "label",label: l, statement: stmt }
				case "label":
					instructions.push(".l" + node.label);
					compileNode(node.statement);
					break;
				// {operation: "goto", destination: destination }
				case "goto":
					instructions.push("Jump l" + node.destination);
					break;
				// {operation: "return",value: value }
				case "return":
					if(node.value !== null) {
						compileNode(node.value);
						instructions.push("Return");
					} else
						instructions.push("ReturnNull");
					break;
				// {operation: 'var',assignments: assignments };
				case "var":
					for(var j = 0; j < node.assignments.length; j++)
						compileNode(node.assignments[j]);
					break;
				// {operation: "while",condition: cond,statement: stmt, bodyLabel: bodyLabel, continueLabel: continueLabel, breakLabel: breakLabel }
				case "while":
					instructions.push(".l" + node.bodyLabel);
					instructions.push("Jump l" + node.continueLabel);
					compileNode(node.statement);

					instructions.push(".l" + node.continueLabel);
					compileNode(node.condition, true);
					instructions.push("JumpIfTrue l" + node.bodyLabel);

					instructions.push(".l" + node.breakLabel);
					break;
				// {operation: "delete",object: obj }
				case "delete":
					if(node.obj.operation == "array_access") {
						// {operation: "array_access", array: left, element: expression}
						compileNode(node.obj.element, true);
						compileNode(node.obj.array, true);
					} else if(node.obj.operation == "property_access") {
						// {operation: "property_access", object: left, identifier: lexer.getValue() };
						instructions.push("PushString " + encodeString(node.obj.identifier));
						compileNode(node.obj.object);
					} else {
						console.log("Internal error when compiling, delete expecting array_access or property_access, not '" + obj.operation + "'.");
						process.exit(-1);
					}
					instructions.push("DeleteElement");
					break;
				// {operation: "do", statement: stmt, condition: cond, bodyLabel: bodyLabel, continueLabel: continueLabel, breakLabel: breakLabel }
				case "do":
					instructions.push(".l" + node.bodyLabel);
					compileNode(node.statement);
					instructions.push(".l" + node.continueLabel);
					compileNode(node.condition, true);
					instructions.push("JumpIfTrue l" + node.bodyLabel);
					instructions.push(".l" + node.breakLabel);
					break;
				// {operation: "foreach", internalIterator: internalIterator, internalObjCounter: objCount, internalObj: obj, publicIterator: publicIterator, declareIterator: declareIterator, object: obj, statement: stmt, bodyLabel: bodyLabel, continueLabel: continueLabel, breakLabel: breakLabel }
				case "foreach":
					/*
					a foreach loop is essentially:
					.iterator = 0;
					.objCount = obj._items;
					.obj = obj;
					while(.iterator < obj._items) {
						publiciterator = obj._get(.iterator);

						// statements

						.iterator++;
					}

					compiles to:
					.iterator = 0;
					.obj = obj;
					.objCount = obj._items;
					jump .continueLabel
					publiciterator = items.obj_items
					.bodyLabel:
					statements

					
					.continueLabel
					.iterator++;
					if iterator < obj._items
					jumpiftrue .bodyLabel
					.breakLabel
					*/

					// .iterator = 0
					instructions.push("PushUnsigned 0");
					instructions.push("Store " + node.internalIterator.stackNumber);
					
					instructions.push('PushString "_items"');
					// .obj = obj, keep copy of .obj on stack
					compileNode(node.obj, true);
					instructions.push("Grab 0");
					instructions.push("Store " + node.internalObj.stackNumber);
					// .objCount obj._items
					instructions.push("LoadElement");
					instructions.push("Store " + node.internalObjCounter.stackNumber);

					instructions.push("Jump l" + node.continueLabel);

					instructions.push(".l" + node.bodyLabel);
					// publiciterator = obj._get(.objCount)
					instructions.push("Load " + node.internalIterator.stackNumber);
					instructions.push('PushString "_get"');
					instructions.push("Load " + node.internalObj.stackNumber);
					instructions.push("LoadElement");
					instructions.push("CallFunction 1");

					// save the public iterator to whatever variable they specified
					while(node.publicIterator.replaceBy !== undefined)
						node.publicIterator = node.publicIterator.replaceBy;
					if(node.publicIterator.closure)
						instructions.push("StoreClosure " + getClosureNumber(node.publicIterator));
					else
						// local variable
						instructions.push("Store " + node.publicIterator.stackNumber);

					instructions.push("Store " + node.publicIterator.stackNumber);

					compileNode(node.statement, true);

					// continue:

					instructions.push(".l" + node.continueLabel);


					//	.iterator++;
					instructions.push("Load " + node.internalIterator.stackNumber);
					instructions.push("Increment");
					instructions.push("Grab 0");
					instructions.push("Save " + node.internalIterator.stackNumber);

					//	if iterator < obj._items
					instructions.push("Load " + node.internalObjCounter.stackNumber);
					instructions.push("LessThan");
					//	jumpiftrue .bodyLabel.
					instructions.push("JumpIfTrue l" + node.bodyLabel);

					instructions.push(".l" + node.breakLabel);

					// args, func 0


					break;
				// {operation: "for", initializer: initializer, condition: condition, oneach: oneach, body: stmt, bodyLabel: bodyLabel, continueLabel: continueLabel, breakLabel: breakLabel }
				case "for":
					compileNode(node.initializer);

					instructions.push(".l" + node.bodyLabel);
					compileNode(node.condition, true);
					instructions.push("JumpIfFalse l" + node.breakLabel);

					compileNode(node.stmt);

					instructions.push(".l" + node.continueLabel);
					for(var j = 0; j < node.oneach.length; j++)
						compileNode(node.oneach[j]);
					instructions.push("Jump l" + node.bodyLabel);

					compileNode.push(".l" + node.breakLabel);
					break;
				// {operation: "if", condition: cond, thenStatement: thenStatement, elseStatement: elseStatement, elseLabel: elseLabel, endLabel: endLabel  }
				case "if":
					compileNode(node.condition, true);
					if(node.elseStatement === null) {
						instructions.push("JumpIfFalse l" + node.endLabel);
						compileNode(node.thenStatement);
						instructions.push(".l" + node.endLabel);
					} else {
						instructions.push("JumpIfFalse l" + node.elseLabel);
						compileNode(node.thenStatement);
						instructions.push("Jump l" + node.endLabel);

						instructions.push(".l" + node.elseLabel);
						compileNode(node.elseStatement);
						instructions.push(".l" + node.endLabel);
					}

					break;
				// {operation: "switch", condition: cond, cases: cases, defaultCase: defaultCase, endLabel: endLabel }
				case "switch":
					compileNode(node.cond, true);

					// keep the condition on the stack as we go through each case
					for(var j = 0; j < node.cases.length; j++) {
						// {value: val, statement: stmt, endLabel: endLabel});
						var c = node.c[j];

						// duplicate the condition, so if we fail we can save it for the next case statement
						instructions.push("Grab 0");
						compileNode(c.value, true);
						// jump to the next case if false
						instructions.push("JumpIfFalse l" + c.endLabel);

						instructions.push("Pop"); // pop our duplicate condition if case is true, because we don't need it anymore (and beside, keeping
												  // values on the stack between statements can break gotos!)
						compileNode(c.statement);

						if(j != node.cases.length - 1 || node.defaultCase !== null) {
							// jump out of the switch statement, we can ignore this if we don't have a default case and we're the last case
							instructions.push("Jump l" + node.endLabel);
						}
					}

					if(node.defaultCase !== null)
						compileNode(node.defaultCase);

					instructions.push(".l" + endLabel);

					break;
				// {operation: "compound_statement", statements: statements }
				case "compound_statement":
					for(var j = 0; j < node.statements.length; j++)
						compileNode(node.statements[j]);
					break;
				// { operation: "function_literal", functionId: func }
				case "function_literal":
					instructions.push("PushFunction f" + node.functionId);
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "object_literal", type: "object", fields: fields }
				case "object_literal":
					instructions.push("NewObject");

					// add each item to the array
					for(var j = 0; j < node.fields.length; j++) {
						// { name: name, expression: exp }
						compileNode(node.fields[j].expression, true);
						instructions.push("PushString " + encodeString(node.fields[j].name));
						// stack is: 0 - index, 1 - value, 2 - object, duplicate object to the front
						instructions.push("Grab 2");
						instructions.push("SaveElement");
					}
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "array_literal", type: "array", values: values }
				case "array_literal":
					instructions.push("PushUnsignedInteger " + node.values.length);
					instructions.push("NewArray");

					// add each item to the array
					for(var j = 0; j < node.values.length; j++) {
						compileNode(node.values[j], true);
						instructions.push("PushUnsignedInteger " + j);
						// stack is: 0 - index, 1 - value, 2 - array, duplicate array to the front
						instructions.push("Grab 2");
						instructions.push("SaveElement");
					}
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "new_buffer", size: size, type: "buffer" }
				case "new_buffer":
					compileNode(node.size, true);
					instructions.push("NewBuffer");
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "new_array", size: size, type: "array" }
				case "new_array":
					compileNode(node.size, true);
					instructions.push("NewArray");
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "require", path: exp }
				case "require":
					compileNode(node.path, true);
					instructions.push("Require");
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "variable_access", variable: variable }
				case "variable_access":
					while(node.variable.replaceBy !== undefined)
						node.variable = node.variable.replaceBy;

					if(node.variable.closure) 
						instructions.push("LoadClosure " + getClosureNumber(node.variable));
					else
						// local variable
						instructions.push("Load " + node.variable.stackNumber);

					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "literal", type: "unsigned|float|string|boolean|null", value: lexer.getValue() }
				case "literal":
					switch(node.type) {
						case "float":
							instructions.push("PushFloat " + node.value);
							break;
						case "signed":
							instructions.push("PushInteger " + node.value);
							break;
						case "unsigned":
							instructions.push("PushUnsignedInteger " + node.value);
							break;
						case "string":
							instructions.push("PushString " + encodeString(node.value));
							break;
						case "boolean":
							if(node.value == "true")
								instructions.push("PushTrue");
							else
								instructions.push("PushFalse");
							break;
						case "null":
							instructions.push("PushNull");
							break;
						default:
							console.log("Internal error when compiling, don't know how to handle literal type '" + node.type +"'.");
							process.exit(-1);
							break;
					}

					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "array_access", array: left, element: expression}
				case "array_access":
					compileNode(node.element, true);
					compileNode(node.array, true);
					instructions.push("LoadElement");
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "property_access", object: left, identifier: lexer.getValue() };
				case "property_access":
					instructions.push("PushString " + encodeString(node.identifier));
					compileNode(node.object, true);
					instructions.push("LoadElement");
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "function_call", _function: left, parameters: parameters };
				case "function_call":
					// push the functions onto the stack in reverse order
					for(var j = node.parameters.length - 1; j >= 0; j--)
						compileNode(node.parameters[j], true);

					compileNode(node._function, true);

					if(expectReturn)
						instructions.push("CallFunction " + node.parameters.length);
					else
						instructions.push("CallFunctionNoReturn " + node.parameters.length);
					break;
				// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
				case "buffer_access":
					compileNode(node.address, true);
					compileNode(node.buffer, true);
					instructions.push("LoadBuffer" + node.type + "<" + node.size + ">");
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "postfix_expression", operator: "++|--", expression: left}
				case "postfix_expression":
					// grab the value
					var node2 = node.expression;

					switch(node2.operation) {
						// {operation: "variable_access", variable: variable }
						case "variable_access":
							while(node2.variable.replaceBy !== undefined)
								node2.variable = node2.variable.replaceBy;

							if(node2.variable.closure)
								instructions.push("LoadClosure " + getClosureNumber(node2.variable));
							else
								// local variable
								instructions.push("Load " + node2.variable.stackNumber);
							break;
						// {operation: "array_access", array: left, element: expression}
						case "array_access":
							compileNode(node2.element, true);
							compileNode(node2.array, true);
							// save element/array for when we write back
							instructions.push("Grab 1");
							instructions.push("Grab 1");
							
							instructions.push("LoadElement");
							break;
						// {operation: "property_access", object: left, identifier: lexer.getValue() };
						case "property_access":
							instructions.push("PushString " + encodeString(node2.identifier));
							compileNode(node2.object, true);
							// save string/object for when we write back
							instructions.push("Grab 1");
							instructions.push("Grab 1");

							instructions.push("LoadElement");
							break;
						// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
						case "buffer_access":
							compileNode(node2.address, true);
							compileNode(node2.buffer, true);
							// save address/buffer for when we write back
							instructions.push("Grab 1");
							instructions.push("Grab 1");
									
							instructions.push("LoadBuffer" + node2.type + "<" + node.size + ">");
							break;
						default:
							console.log("Internal error when compiling, postfix modifier on non-static lvalue '" + node2.operator +"'.");
							break;
					}

					if(expectReturn) // duplicate it before we modify it, so we return the origional value
						instructions.push("Grab 0");

					if(node.operator == "++")
						instructions.push("Increment");
					else if(node.operator == "--")
						instructions.push("Decrement");
					else {
						console.log("Internal error when compiling, don't know how to handle postfix operator '" + node.operator +"'.");
						process.exit(-1);
					}

					if(expectReturn) {
						// we expect to return
						if(node2.operation !== "variable_access") {
							// we want to shuffle stack
							// 		FROM	TO
							// 3	Element	OldVal
							// 2	Object	Value
							// 1	OldVal	Element
							// 0	Value	Object

							instructions.push("Swap 2 0");
							// 0 object, 1 oldval, 2 value, 3 element
							instructions.push("Swap 1, 3");
							// 0 object, 1 element, 2 value, 3 oldval
						}

					} else {
						// don't care about returning it
						if(node2.operation !== "variable_access") {
							// we want to shuffle stack
							// 		FROM	TO
							// 2	Element	Value
							// 1	Object	Element
							// 0	Value	Object

						instructions.push("Swap 2 0");
						// 0 element, 1 object, 2 value
						instructions.push("Swap 0 1");
						// 0 object, 1 element, 2 value
						}
					}
							
					// save the value back
					switch(node2.operation) {
						// {operation: "variable_access", variable: variable }
						case "variable_access":
							while(node2.variable.replaceBy !== undefined)
								node2.variable = node2.variable.replaceBy;
								if(node2.variable.closure)
									instructions.push("StoreClosure " + getClosureNumber(node2.variable));
								else
									// local variable
									instructions.push("Store " + node2.variable.stackNumber);
							break;
						// {operation: "array_access", array: left, element: expression}
						// {operation: "property_access", object: left, identifier: lexer.getValue() };
						case "array_access":
						case "property_access":
							instructions.push("StoreElement");
							instructions.push("StoreElement");
							break;
						// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
						case "buffer_access":
							instructions.push("StoreBuffer" + node2.type + "<" + node.size + ">");
							break;
						default:
							console.log("Internal error when compiling, postfix modifier on non-static lvalue '" + node2.operator +"'.");
							break;
					}
					break;
				// {operation: "unary_expression", operator: "-|+|!|++|--", expression: right}
				case "unary_expression":
					switch(node.operator) {
						case "-":
							compileNode(node.right, true);
							instructions.push("Invert");
							if(!expectReturn)
								instructions.push("Pop");
							break;
						case "+":
							compileNode(node.right, true);
							if(!expectReturn)
								instructions.push("Pop");
							break;
						case "++":
						case "--":
							// grab the value
							var node2 = node.expression;

							switch(node2.operation) {
								// {operation: "variable_access", variable: variable }
								case "variable_access":
									while(node2.variable.replaceBy !== undefined)
										node2.variable = node2.variable.replaceBy;

									if(node2.variable.closure)
										instructions.push("LoadClosure " + getClosureNumber(node2.variable));
									else
										// local variable
										instructions.push("Load " + node2.variable.stackNumber);
									break;
								// {operation: "array_access", array: left, element: expression}
								case "array_access":
									compileNode(node2.element, true);
									compileNode(node2.array, true);

									// save element/array for when we write back
									instructions.push("Grab 1");
									instructions.push("Grab 1");
									
									instructions.push("LoadElement");
									break;
								// {operation: "property_access", object: left, identifier: lexer.getValue() };
								case "property_access":
									instructions.push("PushString " + encodeString(node2.identifier));
									compileNode(node2.object, true);
									// save string/object for when we write back
									instructions.push("Grab 1");
									instructions.push("Grab 1");

									instructions.push("LoadElement");
									break;
								// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
								case "buffer_access":
									compileNode(node2.address, true);
									compileNode(node2.buffer, true);
									// save address/buffer for when we write back
									instructions.push("Grab 1");
									instructions.push("Grab 1");
									
									instructions.push("LoadBuffer" + node2.type + "<" + node.size + ">");
									break;
								default:
									console.log("Internal error when compiling, unary modifier on non-static lvalue '" + node2.operator +"'.");
									break;
							}

							if(node.operator == "++")
								instructions.push("Increment");
							else
								instructions.push("Decrement");

							if(expectReturn) {
								// we expect to return
								if(node2.operation === "variable_access")
									instructions.push("Grab 0");
								else {
									// we want to shuffle stack
									// 		FROM	TO
									// 3	----	Value
									// 2	Element	Value
									// 1	Object	Element
									// 0	Value	Object

									instructions.push("Grab 0");
									// 0 value, 1 value, 2 object, 3 element
									instructions.push("Swap 2 0");
									// 0 object, 1 value, 2 value, 3 element
									instructions.push("Swap 1, 3");
									// 0 object, 2 element, 3 value, 4 value
								}

							} else {
								// don't care about returning it
								if(node2.operation !== "variable_access") {
									// we want to shuffle stack
									// 		FROM	TO
									// 2	Element	Value
									// 1	Object	Element
									// 0	Value	Object

								instructions.push("Swap 2 0");
								// 0 element, 1 object, 2 value
								instructions.push("Swap 0 1");
								// 0 object, 1 element, 2 value
							}
							}
							
							// save the value back
							switch(node2.operation) {
								// {operation: "variable_access", variable: variable }
								case "variable_access":
									while(node2.variable.replaceBy !== undefined)
										node2.variable = node2.variable.replaceBy;

									if(node2.variable.closure)
										instructions.push("StoreClosure " + getClosureNumber(node2.variable));
									else
										// local variable
										instructions.push("Store " + node2.variable.stackNumber);
									break;
								// {operation: "array_access", array: left, element: expression}
								// {operation: "property_access", object: left, identifier: lexer.getValue() };
								case "array_access":
								case "property_access":
									instructions.push("StoreElement");
									instructions.push("StoreElement");
									break;
								// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
								case "buffer_access":
									instructions.push("StoreBuffer" + node2.type + "<" + node.size + ">");
									break;
								default:
									console.log("Internal error when compiling, unary modifier on non-static lvalue '" + node2.operator +"'.");
									break;
							}
							break;
						default:
							console.log("Internal error when compiling, don't know how to handle unary operator '" + node.operator +"'.");
							process.exit(-1);
							break;
					}
					break;
				// {operation: "cast", expression: left, type: "float|signed|unsigned|string|boolean"}
				case "cast":
					compileNode(node.left, true);
					switch(node.type) {
						case "float":
							instructions.push("ToFloat");
							break;
						case "signed":
							instructions.push("ToInteger");
							break;
						case "unsigned":
							instructions.push("ToUnsignedInteger");
							break;
						case "string":
							instructions.push("ToString");
							break;
						case "boolean":
							instructions.push("IsTrue");
							break;
						default:
							console.log("Internal error when compiling, don't know how to handle cast type '" + node.type +"'.");
							process.exit(-1);
							break;
					}
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "math", left: left, operator: "*|/|%|+|-|<<|>>|<<<|>>>|&| | |^", right: right }
				case "math":
					compileNode(node.left, true);
					compileNode(node.right, true);
					switch(node.operator) {
						case "*":
							instructions.push("Multiply");
							break;
						case "/":
							instructions.push("Divide");
							break;
						case "%":
							instructions.push("Modulo");
							break;
						case "+":
							instructions.push("Add");
							break;
						case "-":
							instructions.push("Subtract");
							break;
						case "<<":
							instructions.push("ShiftLeft");
							break;
						case ">>":
							instructions.push("ShiftRight");
							break;
						case "<<<":
							instructions.push("RotateLeft");
							break;
						case ">>>":
							instructions.push("RotateRight");
							break;
						case "&":
							instructions.push("And");
							break;
						case "|":
							instructions.push("Or");
							break;
						case "^":
							instructions.push("Xor");
							break;
						default:
							console.log("Internal error when compiling, don't know how to handle math operator '" + node.operator +"'.");
							process.exit(-1);
							break;
					}
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation; 'comparison", left: left, operator: "<|>|<=|>=|==|!=", right: right }
				case "comparison":
					compileNode(node.left, true);
					compileNode(node.right, true);
					switch(node.operator) {
						case "<":
							instructions.push("LessThan");
							break;
						case ">":
							instructions.push("GreaterThan");
							break;
						case "<=":
							instructions.push("LessThanOrEquals");
							break;
						case ">=":
							instructions.push("GreaterThanOrEquals");
							break;
						case "==":
							instructions.push("Equals");
							break;
						case "!=":
							instructions.push("NotEquals");
							break;
						default:
							console.log("Internal error when compiling, don't know how to handle comparison operator '" + node.operator +"'.");
							process.exit(-1);
							break;
					}
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "logical_expression", left: left, operator: "&&| || |^^", right: right, breakLabel: breakLabel }
				case "logical_expression":
					switch(node.operator) {
						case "&&":
							compilerNode(node.left, true);
							instructions.push("Grab 0");
							instructions.push("JumpIfFalse .l" + node.breakLabel);
							compilerNode(node.right, true);
							instructions.push(".l" + node.breakLabel);
						break;
						case "||":
							compilerNode(node.left, true);
							instructions.push("Grab 0");
							instructions.push("JumpIfTrue .l" + node.breakLabel);
							compilerNode(node.right, true);
							instructions.push(".l" + node.breakLabel);
						break;
						case "^^":
							compilerNode(node.left, true);
							compilerNode(node.right, true);
							instructions.push("Xor");
						break;
						default:
							console.log("Internal error when compiling, don't know how to handle logical expression operator '" + node.operator +"'.");
							process.exit(-1);
							break;
					}
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "inline_if", condition: left, trueExpression: trueExp, falseExpression: falseExp, elseLabel: elseLabel, endLabel: endLabel }
				case "inline_if":
					compileNode(node.condition, true);
					instructions.push("JumpIfFalse l" + node.elseLabel);
					compileNode(node.trueExpression, true);
					instructions.push("Jump l" + node.endLabel);
					instructions.push(".l" + node.elseLabel);
					compileNode(node.falseExpression, true);
					instructions.push(".l" + node.endLabel);
					if(!expectReturn)
						instructions.push("Pop");
					break;
				// {operation: "assignment", target: left, operator: "=|*=|/=|%=|+=|-=|<<=|>>=|<<<=|>>>=|&=| |= |^=", value: expression }
				case "assignment":
					if(node.operator == '=') {
						compileNode(node.value, true);

						if(expectReturn)
							instructions.push("Grab 0");
						var t = node.target;

						// just save the expression
						switch(t.operation) {
								// {operation: "variable_access", variable: variable }
								case "variable_access":
									while(t.variable.replaceBy !== undefined)
										t.variable = t.variable.replaceBy;

									if(t.variable.closure)
										instructions.push("StoreClosure " + getClosureNumber(t.variable));
									else
										// local variable
										instructions.push("Store " + t.variable.stackNumber);
									break;
								// {operation: "array_access", array: left, element: expression}
								case "array_access":
									compileNode(t.element, true);
									compileNode(t.array, true);

									// save element/array for when we write back
									//instructions.push("Grab 1");
									//instructions.push("Grab 1");
									
									instructions.push("StoreElement");
									break;
								// {operation: "property_access", object: left, identifier: lexer.getValue() };
								case "property_access":
									instructions.push("PushString " + encodeString(t.identifier));
									compileNode(t.object, true);
									// save string/object for when we write back
									//instructions.push("Grab 1");
									//instructions.push("Grab 1");

									instructions.push("StoreElement");
									break;
								// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
								case "buffer_access":
									compileNode(t.address, true);
									compileNode(t.buffer, true);
									// save address/buffer for when we write back
									//instructions.push("Grab 1");
									//instructions.push("Grab 1");
									
									instructions.push("StoreBuffer" + t.type + "<" + node.size + ">");
									break;
								default:
									console.log("Internal error when compiling, assignment on non-static lvalue '" + t.operator +"'.");
									break;
							}
					} else { /////// an assignment other than =
						// grab the value
						var node2 = node.expression;

						switch(node2.operation) {
							// {operation: "variable_access", variable: variable }
							case "variable_access":
								while(node2.variable.replaceBy !== undefined)
									node2.variable = node2.variable.replaceBy;

								if(node2.variable.closure)
									instructions.push("LoadClosure " + getClosureNumber(node2.variable));
								else
									// local variable
									instructions.push("Load " + node2.variable.stackNumber);
								break;
							// {operation: "array_access", array: left, element: expression}
							case "array_access":
								compileNode(node2.element, true);
								compileNode(node2.array, true);

								// save element/array for when we write back
								instructions.push("Grab 1");
								instructions.push("Grab 1");
									
								instructions.push("LoadElement");
								break;
							// {operation: "property_access", object: left, identifier: lexer.getValue() };
							case "property_access":
								instructions.push("PushString " + encodeString(node2.identifier));
								compileNode(node2.object, true);
								// save string/object for when we write back
								instructions.push("Grab 1");
								instructions.push("Grab 1");

								instructions.push("LoadElement");
								break;
							// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
							case "buffer_access":
								compileNode(node2.address, true);
								compileNode(node2.buffer, true);
								// save address/buffer for when we write back
								instructions.push("Grab 1");
								instructions.push("Grab 1");
									
								instructions.push("LoadBuffer" + node2.type + "<" + node.size + ">");
								break;
							default:
								console.log("Internal error when compiling, unary modifier on non-static lvalue '" + node2.operator +"'.");
								break;
						}

						// get the new value
						compileNode(node.value, true);

						switch(node.operator) {
							case "*=":
								instructions.push("Multiply");
								break;
							case "/=":
								instructions.push("Divide");
								break;
							case "%=":
								instructions.push("Modulo");
								break;
							case "+=":
								instructions.push("Add");
								break;
							case "-=":
								instructions.push("Subtract");
								break;
							case "<<=":
								instructions.push("ShiftLeft");
								break;
							case ">>=":
								instructions.push("ShiftRight");
								break;
							case "<<<=":
								instructions.push("RotateLeft");
								break;
							case ">>>=":
								instructions.push("RotateRight");
								break;
							case "&=":
								instructions.push("And");
								break;
							case "|=":
								instructions.push("Or");
								break;
							case "^=":
								instructions.push("Xor");
								break;
							default:
								console.log("Internal error when compiling, unknown assignment operator '" + node.operator +"'.");
								break;
						}

						if(expectReturn) {
							// we expect to return
							if(node2.operation === "variable_access")
								instructions.push("Grab 0");
							else {
								// we want to shuffle stack
								// 		FROM	TO
								// 3	----	Value
								// 2	Element	Value
								// 1	Object	Element
								// 0	Value	Object

								instructions.push("Grab 0");
								// 0 value, 1 value, 2 object, 3 element
								instructions.push("Swap 2 0");
								// 0 object, 1 value, 2 value, 3 element
								instructions.push("Swap 1, 3");
								// 0 object, 2 element, 3 value, 4 value
							}

						} else {
							// don't care about returning it
							if(node2.operation !== "variable_access") {
								// we want to shuffle stack
								// 		FROM	TO
								// 2	Element	Value
								// 1	Object	Element
								// 0	Value	Object

							instructions.push("Swap 2 0");
							// 0 element, 1 object, 2 value
							instructions.push("Swap 0 1");
							// 0 object, 1 element, 2 value
							}
						}
							
						// save the value back
						switch(node2.operation) {
							// {operation: "variable_access", variable: variable }
							case "variable_access":
								while(node2.variable.replaceBy !== undefined)
									node2.variable = node2.variable.replaceBy;

								if(node2.variable.closure)
									instructions.push("StoreClosure " + getClosureNumber(node2.variable));
								else
									// local variable
									instructions.push("Store " + node2.variable.stackNumber);
								break;
							// {operation: "array_access", array: left, element: expression}
							// {operation: "property_access", object: left, identifier: lexer.getValue() };
							case "array_access":
							case "property_access":
								instructions.push("StoreElement");
								instructions.push("StoreElement");
								break;
							// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
							case "buffer_access":
								instructions.push("StoreBuffer" + node2.type + "<" + node.size + ">");
								break;
							default:
								console.log("Internal error when compiling, unary modifier on non-static lvalue '" + node2.operator +"'.");
								break;
						}
						// {operation: "variable_access", variable: variable }
						// {operation: "array_access", array: left, element: expression}
						// {operation: "property_access", object: left, identifier: lexer.getValue() };
						// {operation: "buffer_access", buffer: left, type: type, size: size, address: addr}
					}
					break;
				case "nop": // a removed instruction
					break;
				default:
					console.log("Internal error when compiling, don't know how to handle '" + node.operation +"'.");
					process.exit(-1);
					break;
			};
		};


		f.write("Function f" + i + "\n");
		f.write("-locals " + thisFunc.numLocals + "\n");
		f.write("-closures " + thisFunc.numClosures + "\n");

		for(var j = 0; j < thisFunc.statements.length; j++)
			compileNode(thisFunc.statements[j]);

		if(!debug) {
			if(verbose)
				console.log("Performing peep hole optimizations");
			peepHoleOptimizations();
		}

		if(verbose)
			console.log("Outputing assembly to file");
		for(var j = 0; j < instructions.length; j++)
			f.write(instructions[j] + "\n");
	}
};

// parse a file
exports.parseFile = function(path) {
	return parse(Lexer.parseFile(path));
};

// parse a string
exports.parseString = function(str) {
	return parse(Lexer.parseString(str));
};

if(verbose)
	console.log("Reading in source file.");

var funcs = exports.parseFile(inputFile);

if(verbose)
	console.log("Compiling functions.");

exports.compile(funcs);
