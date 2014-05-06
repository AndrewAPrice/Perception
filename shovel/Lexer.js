// lexical parser - parses a string (or file) containing Shovel source code into a series of tokens
// usage:
// var parser = require('./parser.js').parseFile('test.src');
//
// parser.peekToken() - returns the next token
// parser.nextToken() - read the next token and return it
// parser.getToken() - get the current token (same as the last result from nextToken)
// parser.getValue() - returns the value attached to the token
//
// a token is a literal if it is a STRING, CHAR, INTEGER, FLOAT, IDENTIFIER
// if it's a literal call parser.getValue() to get the value of it

var fs = require('fs');

var error = function(line, col, message) {
	console.log("Error on line " + (line + 1) + ", column " + (col + 1) + ": " + message);
	process.exit(-1);
};

exports.parseString = function(str) {
	var parser = {};

	var pos = 0;
	var strLength = str.length;
	var col = 0;
	var line = 0;

	var nextCharacter = function() {
		if(pos == strLength)
			return "";
		var c = str[pos];
		pos++;
		if(c == '\n') {
			col = 0;
			line++;
		} else
			col++;

		return c;
	};

	var backChar = function() {
		if(pos == 0 || pos == str.length) // can't go before 0, also was never incremented at the end
			return;
		pos--;
		col--;
		if(col < 0)
			line--;
	};

	var isNumber = function(c) {
		switch(c) {
			case '1': case '2': case '3': case '4': case '5':
			case '6': case '7': case '8': case '9': case '0':
			return true;
			default:
			return false;
		}
	};

	var isSymbol = function(c) {
		switch(c) {
			case '!': case '%': case '^': case '&': case '*': case '(': case ')':
			case '-': case '+': case '=': case '[': case ']': case '{': case '}':
			case '|': case '\\': case ':': case ';': case '"': case "'": case '/':
			case '?': case '<': case '>': case ',': case '.':
			case '\n': case '\r': case ' ': case '\t':
			return true;
			default:
			return false;
		}
	};

	var isKeyword = function(str) {
		switch(str) {
			case "new": case "delete": case "for": case "while": case "do": case "break": case "continue": case "return": case "goto": case "switch":
			case "function": case "signed": case "unsigned": case "float": case "boolean": case "float": case "default":
			return true;
			default:
			return false;
		}
	};

	// read the first token
	var readNextToken = function() {
		// set the current token to the one we peaked at next
		token = nextToken;
		realValue = nextRealValue;

		// get the next token
		var c = nextCharacter();
		nextRealValue = "";

		// skip any white spaces
		while(c == ' ' || c == '\n' || c == '\r' || c == '\t')
			c = nextCharacter();

		if(c == "") { // end of the file
			nextToken = "EOF";
			return;
		}

		// check if we're a string
		if(c == '"') {
			nextToken = "STRING";

			while(true) {
				c = nextCharacter();
				if(c == "")
					error(line, col, "The file ended before the string was closed.");

				if(c == '"')
					return; // reached the end of the string

				if(c == '\\') { // escape strings
					c = nextCharacter();
					if(c == "")
						error(line, col, "The file ended before the string was closed.");

					if(c == '0') c = '\0';
					else if(c == 'n') c = '\n';
					else if(c == 'r') c = '\r';
					else if(c == 't') c = '\t';
					// don't need to handle '\\' or '\"' because it's already in c
				}

				nextRealValue += c; // append character
			}
		}

		if(isNumber(c)) { // positive number (unsigned literal or float)
			nextToken = "INTEGER";
			
			while(isNumber(c) || c == '.') {
				nextRealValue += c; // append to number
				if(c == '.') { // a decimal indicates this is a float
					if(nextToken == "float") // already has a .
						error(line, col, "The number literal contains multiple decimal points.");
					nextToken = "float";
				}

				c = nextCharacter();
			}

			if(!isSymbol(c))
				error(line,col, "The number literal contains something that is not a digit!");

			backChar();
			return;
		}

		if(c == "'") { // character literal
			nextToken = "CHAR";

			while(true) {
				c = nextCharacter();
				if(c == "")
					error(line, col, "The file ended before the character was closed.");

				if(c == "'")
					return; // reached the end of the char

				if(nextRealValue != "") // this is a character literal, there can only be one character
					error(line, col, "The character literal contains more than one character.");

				if(c == '\\') { // escape strings
					c = nextCharacter();
					if(c == "")
						error(line, col, "The file ended before the character was closed.");

					if(c == '0') c = '\0';
					else if(c == 'n') c = '\n';
					else if(c == 'r') c = '\r';
					else if(c == 't') c = '\t';
					// don't need to handle '\\' or '\"' because it's already in c
				}

				nextRealValue = c; // set to character
			}
		}

		if(c == '!') { // ! !=
			c = nextCharacter();
			if(c == '=') {
				nextToken = "!=";
			} else {
				backChar();
				nextToken = '!';
			}
			return;
		}

		if(c == '%') { // % %=
			c = nextCharacter();
			if(c == '=') {
				nextToken = '%=';
			} else {
				backChar();
				nextToken = '%';
			}
			return;
		}

		if(c == '^') { // ^ ^^ ^=
			c = nextCharacter();
			if(c == '^')
				nextToken = '^^';
			else if(c == '=')
				nextToken = '^=';
			else {
				backChar();
				nextToken = '^';
			}
			return;
		}

		if(c == '&') { // & && &=
			c = nextCharacter();
			if(c == '&')
				nextToken = '&&';
			else if(c == '=')
				nextToken = '&=';
			else {
				backChar();
				nextToken = '&';
			}
			return;
		}

		if(c == '*') { // * *=
			c = nextCharacter();
			if(c == '=')
				nextToken = '*='
			else {
				backChar();
				nextToken = '*';
			}
			return;
		}

		if(c == '-') { // - -- -= negative number (signed literal or float)
			c = nextCharacter();
			if(c == '-')
				nextToken = '--';
			else if(c == '=')
				nextToken = '-=';/*
			else if(isNumber(c)) { // negative number
				nextToken = "signed";
				nextRealValue = '-';
				
				while(isNumber(c) || c == '.') {
					nextRealValue += c; // append to number
					if(c == '.') { // a decimal indicates this is a float
						if(nextToken == "float") // already has a .
							error(line, col, "The number literal contains multiple decimal points.");
						nextToken = "float";
					}

					c = nextCharacter();
				}
				backChar();
			}*/ else {
				backChar();
				nextToken = '-';
			}

			return;
		}

		if(c == '+') { // + += positive number (signed literal or float)
			c = nextCharacter();
			if(c == '+')
				nextToken = '++';
			else if(c == '=')
				nextToken = '+=';
			/* else if(isNumber(c)) { // positive number
				nextToken = "signed";

				while(isNumber(c) || c == '.') {
					nextRealValue += c; // append to number
					if(c == '.') { // a decimal indicates this is a float
						if(nextToken == "float") // already has a .
							error(line, col, "The number literal contains multiple decimal points.");
						nextToken = "float";
					}

					c = nextCharacter();
				}
				backChar();
			}*/ else {
				backChar();
				nextToken = '-';
			}

			return;
		}

		if(c == '=') { // = ==
			c = nextCharacter();
			if(c == '=')
				nextToken = '==';
			else {
				backChar();
				nextToken = '=';
			}
			return;
		}

		if(c == '|') { // | || |=
			c = nextCharacter();
			if(c == '|')
				nextToken = '||';
			else if(c == '=')
				nextToken = '|=';
			else {
				backChar();
				nextToken = '|';
			}
			return;
		}

		if(c == '/') { // / /= // /* 
			c = nextCharacter();
			if(c == '/') { // // comment
				do {
					c = nextCharacter();
				} while(c != '' && c != '\n' && c != '\r')

				readNextToken();
				return; // tail call
			} else if(c == '*') { // /* */ comment
				var prevC = '';
				c = nextCharacter(); // do one read first so /*/ doesn't cancel out the comment
				while(c != '' && !(c == '/' && prevC == '*')) {
					prevC = c;
					c = nextCharacter();
				}

				readNextToken();
				return;
			} else if(c == '=')
				nextToken = '/=';
			else {
				backChar();
				nextToken = '/';
			}
			return;
		}

		/*if(c == '[' || c == ']' || c == '\\' || c == ':' || c == ';' || c == '(' || c == ')' || c == '{' || c == '}' ||
			c == '?' || c == '<' || c == '>' || c == ',' || c == '.'*/
		if(isSymbol(c)) { // standalone tokens
			nextToken = c; // already on the buffer
			return;
		}

		// check if we're an identifier
		nextToken = "IDENTIFIER";

		// loop until we've finished reading symbols
		do {
			nextRealValue += c;
			c = nextCharacter();
		} while(!isSymbol(c));

		backChar();

		// check if the identifier is a keyword
		if(isKeyword(nextRealValue))
			nextToken = nextRealValue;

	};

	var token, nextToken; // the token read token (we keep one ahead for peaking)
	var realValue, nextRealValue; // the real value (for string literals and numbers)

	// peek the next token
	parser.peekToken = function() {
		return token;
	};
 
	// get the next token and make it the current token
	parser.nextToken = function() {
		readNextToken();

		return token;
	};

	// get the current token
	parser.getToken = function() {
		return token;
	};

	// gets the real value
	parser.getValue = function() {
		return realValue;
	};

	// gets the current column, row, position
	parser.getCol = function() { return col; };
	parser.getRow = function() { return row; };
	parser.getPos = function() { return pos; };

	// read the first token
	readNextToken();
	token = nextToken; realValue = nextRealValue;

	return parser;
};

exports.parseFile = function(filepath) {
	return exports.parseString(fs.readFileSync(filepath, 'utf8'));
};

var parser = exports.parseFile('test.src');
while(parser.peekToken() != 'EOF') {
	var token = parser.nextToken();
	if(token == "STRING" || token == "CHAR" || token == "INTEGER" || token == "FLOAT" || token == "IDENTIFIER")
		token += " " + parser.getValue();
	console.log(token);
}
