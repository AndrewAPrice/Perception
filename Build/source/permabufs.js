// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

const fs = require('fs');

// Creates a lexer, which is the thing that takes the raw string and turns it into
// tokens.
function createLexer(filename) {
	const fileContents = fs.readFileSync(filename, 'UTF-8');
	let index = 0;
	let nextToken = '';
	let nextChar = '';

	function peekChar() {
		return nextChar;
	}

	function readChar() {
		let lastChar = nextChar;
		if (index >= fileContents.length) {
			nextChar = '';
		} else {
			nextChar = fileContents[index];
			index++;
		}
		return lastChar;
	}

	// Read in the first char.
	readChar();

	function peekToken() {
		return nextToken;
	}

	function readToken() {
		const lastToken = nextToken;

		// Read in the next token.
		nextToken = '';

		// Skip over blank spaces and comments.
		let c = readChar();
		while (isBlankSpace(c) || c == '/') {
			if (c == '/') {
				// Could be a comment.
				if (peekChar() == '/') {
					// Line comment.
					function isNewlineOrEOF(c) {
						return c == '\n' || c == '\r' || c == '';
					}
					while (!isNewlineOrEOF(readChar())) {}

				} else if (peekChar() == '*') {
					// Block comment.

					// Jump over *.
					readChar(); 

					function isClosingOrEOF(c, next) {
						return (c == '*' && next == '/') || c == '';
					}
					while (!isClosingOrEOF(readChar(), peekChar())) {}

					// Jump over /.
					readChar();
				} else {
					// Exit the while loop because we have a '/'.
					break;
				}
			}
			c = readChar();
		}

		nextToken = c;

		if (isIdentifierCharacter(nextToken)) {
			// This is an identifier, so keep reading the rest of the identifier.
			while (isIdentifierCharacter(peekChar())) {
				nextToken += readChar();
			}
		}

		return lastToken;
	}

	// Read in this first token.
	readToken();

	return {
		peekToken: peekToken,
		readToken: readToken
	};

}

function isBlankSpace(c) {
	return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

function isIdentifierCharacter(c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		(c >= '0' && c <= '9');
}

function compile(filename, destination, libraries, fileDependencies) {
	const symbolTable = {};
	const symbolsToCompile = [];

	function compileFile(filename, fileBeingCompiled) {
		const lexer = createLexer(filename);
		while (true) {
			let token = lexer.readToken();
			if (token == '') {
				return;
			}

			console.log(token);
		}
	}

	compileFile(filename, true);
}

module.exports = {
	compile: compile
};
