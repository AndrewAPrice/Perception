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

function isBlankSpace(c) {
	return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

function isIdentifier(c) {
	return c.length > 1 || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		(c >= '0' && c <= '9');
}

function isFirstCharNum(str) {
	if (str.length == 0) {
		return false;
	}

	return str[0] >= '0' && str[0] <= '9';
}

function isNumber(str) {
	if (str.length == 0) {
		return false;
	}

	for (let i = 0; i < str.length; i++) {
		if (str[i] < '0' || str[i] > '9') {
			return false;
		}
	}

	return true;
}

// Creates a lexer, which is the thing that takes the raw string and turns it into
// tokens.
function createLexer(filename) {
	if (!fs.existsSync(filename)) {
		console.log('Permebuf file "' + filename + '" does not exist.');
		return false;
	}
	const fileContents = fs.readFileSync(filename, 'UTF-8');
	let index = 0;

	let currentFilePosition = {row: 0, col: 0};
	let filePositionOfNextToken = {row: 0, col: 0};
	let filePositionOfToken = {row: 0, col: 0};
	let nextToken = '';
	let nextChar = '';

	function peekChar() {
		return nextChar;
	}

	function incrementFilePosition(c) {
		if (c == '' || c == '\r') {
			return;
		}

		if (c == '\n') {
			currentFilePosition.row++;
			currentFilePosition.col = 0;
		} else {
			currentFilePosition.col++;
		}
	}

	function readChar() {
		let lastChar = nextChar;
		if (index >= fileContents.length) {
			nextChar = '';
		} else {
			nextChar = fileContents[index];
			index++;
		}

		incrementFilePosition(lastChar);
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

		filePositionOfToken = filePositionOfNextToken;
		filePositionOfNextToken = {row: currentFilePosition.row, col: currentFilePosition.col - 1};
		nextToken = c;

		if (isIdentifier(nextToken)) {
			// This is an identifier, so keep reading the rest of the identifier.
			while (isIdentifier(peekChar())) {
				nextToken += readChar();
			}
		}


		return lastToken;
	}

	// Read in this first token.
	readToken();

	function filePosition() {
		return filePositionOfToken;
	}

	function expectToken(expectToken) {
		const token = readToken();
		if (token == expectToken) {
			return true;
		}

		expectedTokens([expectToken], token);
		return false;
	}

	function expectedTokens(expectedTokens, token) {
		const pos = filePosition();

		let message = 'In file ' + filename + ' line ' + pos.row + ', column ' + pos.col + ' expected ';
		for (let i = 0; i < expectedTokens.length; i++) {
			if (i > 0) {
				message += ' or ';
			}
			message += expectedTokens[i];
		}
		console.log(message + ' but had ' + token + '.');

	}

	return {
		'peekToken': peekToken,
		'readToken': readToken,
		'filePosition': filePosition,
		'expectToken': expectToken,
		'expectedTokens': expectedTokens
	};
}

module.exports = {
	createLexer: createLexer,
	isBlankSpace: isBlankSpace,
	isIdentifier: isIdentifier,
	isFirstCharNum: isFirstCharNum,
	isNumber: isNumber
};