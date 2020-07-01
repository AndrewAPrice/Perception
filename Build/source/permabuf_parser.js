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

const {PackageType, getPackageTypeDirectoryName} = require('./package_type');
const permabufLexer = require('./permabuf_lexer');
const ClassType = require('./permabuf_class_types');
const {rootDirectory} = require('./root_directory');

function parseFile(localPath, packageName, packageType, importedFiles,
		symbolTable, symbolsToGenerate, cppIncludeFiles, fileBeingCompiled) {
	const filename = rootDirectory + getPackageTypeDirectoryName(packageType) + '/' + packageName +
		'/permabuf/' + localPath;

	importedFiles[filename] = true;

	const lexer = permabufLexer.createLexer(filename);
	if (!lexer) {
		return false;
	}

	cppIncludeFiles['permabuf/' + getPackageTypeDirectoryName(packageType) + '/' + packageName + '/' + localPath +
		'.lite.h'] = true;

	// The current namespace we're in. If set, it ends in '.'.
	let namespace = '';

	function parsePath() {
		let path = false;

		let lastTokenWasIdentifier = false;
		let pathPart = '';

		function addPathPart() {
			if (pathPart.length == 0) {
				return;
			}

			if (!path) {
				path = [];
			}

			path.push(pathPart);
		}

		while (true) {
			let token = lexer.readToken();
			if (permabufLexer.isIdentifier(token)) {
				if (lastTokenWasIdentifier) {
					// Hack around the lexer that if we have two identifiers in a row
					// it's likely a space in the path.
					pathPart += ' ';	
				}
				pathPart += token;
				lastTokenWasIdentifier = true;
			} else if (token == '/' || token == '\\') {
				addPathPart();
				parsePath = '';
				// Token is a deliminator.
				readyForIdentifier = true;
				lastTokenWasIdentifier = false;
			} else if (token == '.' || token == '_') {
				// Token is a valid filename character.
				parsePath += token
				lastTokenWasIdentifier = false;
			} else {
				addPathPart();
				return path;
			}
		}
	}

	function parseImport() {
		if (!lexer.expectToken('import')) {
			return false;
		}

		let importedLocalPath = '';
		let subPackageName = packageName;
		let subPackageType = packageType;

		switch (lexer.peekToken()) {
			case '<':
				// Import file from another library.
				lexer.readToken();
				path = parsePath();
				if (!path || !lexer.expectToken('>') || !lexer.expectToken(';')) {
					return false;
				}

				subPackageName = path[0];
				importedLocalPath = path.splice(1).join('/');
				subPackageType = PackageType.LIBRARY;
			case '"':
				// Import file from this package.
				lexer.readToken();
				path = parsePath();
				if (!path || !lexer.expectToken('"') || !lexer.expectToken(';')) {
					return false;
				}

				importedLocalPath = path.join('/');
				break;
			default:
				lexer.expectToken(['<', '"'], lexer.readToken());
				return false;
		}

		const path = rootDirectory + getPackageTypeDirectoryName(subPackageType) + '/' +
			subPackageName + '/permabuf/' + localPath;

		if (importedFiles[path]) {
			// File has already been imported. Recursive imports are allowed.
			return true;
		}

		return parseFile(importedLocalPath, subPackageName, subPackageType, importedFiles,
			symbolTable, symbolsToGenerate, cppIncludeFiles, false);
	}

	function parseNamespace() {
		if (!lexer.expectToken('namespace')) {
			return false;
		}

		namespace == '';
		while (permabufLexer.isIdentifier(lexer.peekToken()) || lexer.peekToken() == '.') {
			const token = lexer.readToken();
			if (token == '.') {
				// Ignore beginning '.'.
				if (namespace.length == 0) {
					continue;
				}

				// Ignore multiple '.' in a row.
				if (namespace[namespace.length - 1] == '.') {
					continue;
				}
			}

			namespace += token;
		}

		if (namespace.length > 0 && namespace[namespace.length - 1] != '.') {
			namespace += '.';
		}

		return lexer.expectToken(';');
	}

	function convertToFullName(parent, identifier) {
		if (parent == null) {
			if (namespace.length > 0) {
				return namespace + identifier;
			} else {
				return identifier;
			}
		} else {
			return parent.fullName + '.' + identifier;
		}
	}

	function convertToCppClassName(parent, identifier) {
		if (parent == null) {
			return identifier;
		} else {
			return parent.cppClassName + '__' + identifier;
		}
	}

	function parseType() {
		// Valid characters are identifiers, ., <, >.
		let type = '';
		while (true) {
			let token = lexer.peekToken();
			if (token == '.' || token == '<' || token == '>') {
				type += token;
			} else if (permabufLexer.isIdentifier(token)) {
				if (permabufLexer.isFirstCharNum(token)) {
					token = '_' + token;
				}
				type += token;
			} else {
				return type.length == 0 ? false : type;
			}

			lexer.readToken();
		}
	}

	function parseMessageField(thisMessage) {
		const fieldName = lexer.readToken();
		if (!permabufLexer.isIdentifier(fieldName)) {
			lexer.expectedTokens(['identifier for the message field name'], messageName);
			return false;
		}

		if (!lexer.expectToken(':')) {
			return false;
		}

		const type = parseType();
		if (!type) {
			lexer.expectedTokens(['type'], lexer.readToken());
			return false;
		}

		if (!lexer.expectToken('=')) {
			return false;
		}

		const fieldNumber = lexer.readToken();
		if (!permabufLexer.isNumber(fieldNumber)) {
			lexer.expectedTokens(['field number'], fieldNumber);
			return false;
		}

		if (!lexer.expectToken(';')) {
			return false;
		}

		if (fileBeingCompiled) {
			if (permabufLexer.isFirstCharNum(fieldName)) {
				fieldName = '_' + fieldName;
			}

			if (thisMessage.fields[fieldName]) {
				lexer.expectedTokens(['a unique message field name'], fieldName);
				return false;
			}

			thisMessage.fields[fieldName] = {
				'name': fieldName,
				'number': parseInt(fieldNumber),
				'type': type
			};
		}

		return true;
	}

	function parseEnumField(thisEnum) {
		const fieldName = lexer.readToken();

		if (!permabufLexer.isIdentifier(fieldName)) {
			lexer.expectedTokens(['identifier for the enum field name'], messageName);
			return false;
		}

		if (!lexer.expectToken('=')) {
			return false;
		}

		const fieldNumber = lexer.readToken();
		if (!permabufLexer.isNumber(fieldNumber)) {
			lexer.expectedTokens(['field number'], fieldNumber);
			return false;
		}

		if (!lexer.expectToken(';')) {
			return false;
		}

		if (fileBeingCompiled) {
			if (permabufLexer.isFirstCharNum(fieldName)) {
				fieldName = '_' + fieldName;
			}

			if (thisEnum.fields[fieldName]) {
				lexer.expectedTokens(['a unique enum field name'], fieldName);
				return false;
			}

			thisEnum.fields[fieldName] = {
				'name': fieldName,
				'number': parseInt(fieldNumber)
			};
		}

		return true;
	}

	function parseReserve(thisClass) {
		if (thisClass.classType != ClassType.MESSAGE) {
			console.log('\'reserve\' can only be used in messages, and ' +
				thisClass.fullName + ' in ' + thisClass.fileName + ' is not a message.');
			return false;
		}

		if (!lexer.expectToken('reserve')) {
			return false;
		}

		const type = parseType();
		if (!type) {
			lexer.expectedTokens(['type'], lexer.readToken());
			return false;
		}

		if (!lexer.expectToken('=')) {
			return false;
		}

		let fieldNumber = lexer.readToken();
		if (!permabufLexer.isNumber(fieldNumber)) {
			lexer.expectToken(['field number'], fieldNumber);
			return false;
		}

		if (fileBeingCompiled) {
			thisClass.reservedFields.push({
				'name': 'reserve',
				'type': type,
				'number': parseInt(fieldNumber)
			});
		}

		while (true) {
			const token = lexer.readToken();
			switch (token) {
				case ';':
					return true;
				case ',':
					fieldNumber = lexer.readToken();
					if (!permabufLexer.isNumber(fieldNumber)) {
						lexer.expectToken(['field number'], fieldNumber);
						return false;
					}
					if (fileBeingCompiled) {
						thisClass.reservedFields.push({
							'name': 'reserve',
							'type': type,
							'number': parseInt(fieldNumber)
						});
					}
					break;
				default:
					lexer.expectedTokens([',', ';'], token);
					return false;
			}
		}
	}

	function parseClass(parent) {
		let classType;
		const classTypeStr = lexer.readToken();
		switch (classTypeStr) {
			case 'message':
				classType = ClassType.MESSAGE;
				break;
			case 'enum':
				classType = ClassType.ENUM;
				break;
			case 'oneof':
				classType = ClassType.ONEOF;
				break;
			default:
				lexer.expectedTokens(['message', 'enum', 'oneof'], classTypeStr);
				return false;
		}

		const className = lexer.readToken();

		if (!permabufLexer.isIdentifier(className)) {
			lexer.expectedTokens(['identifier for the name'], className);
			return false;
		}
		if (permabufLexer.isFirstCharNum(className)) {
			className = '_' + className;
		}

		if (!lexer.expectToken('{')) {
			return false;
		}

		const fullClassName = convertToFullName(parent, className);
		const thisClass = {
			'name': className,
			'fullName': fullClassName,
			'classType': classType,
			'fileName': filename,
			'namespace': namespace,
			'cppClassName': convertToCppClassName(parent, className),
			'fields': {},
			'reservedFields': [],
			'children': []
		};
		if (symbolTable[fullClassName]) {
			lexer.expectedTokens(['a unique name'], className);
			console.log(fullClassName + ' is already defined in ' + symbolTable[fullClassName].fileName);
			return false;
		}
		symbolTable[fullClassName] = thisClass;

		if (parent) {
			parent.children.push(thisClass);
		}

		if (fileBeingCompiled) {
			symbolsToGenerate.push(fullClassName);
		}

		while (true) {
			switch (lexer.peekToken()) {
				case 'message':
				case 'enum':
				case 'oneof':
					if (!parseClass(thisClass)) {
						return false;
					}
					break;
				case '}':
					lexer.readToken();
					return true;
				case 'reserve':
					if (!parseReserve(thisClass)) {
						return false;
					}
					break;
				default:
					switch (classType) {
						case ClassType.MESSAGE:
							if (!parseMessageField(thisClass)) {
								return false;
							}
							break;
						case ClassType.ENUM:
							if (!parseEnumField(thisClass)) {
								return false;
							}
							break;
						case ClassType.ONEOF:
							// During parsing, enum and message fields are the same.
							if (!parseMessageField(thisClass)) {
								return false;
							}
							break;
						default:
							return false;
					}
					break;
			}
		}
	}

	let hasEntries = false;

	// Loop over the top level file.
	while (true) {
		switch (lexer.peekToken()) {
			case 'import':
				if (!parseImport()) {
					return false;
				}
				break;
			case 'namespace':
				if (namespace != '') {
					console.log('\'namespace\' in ' + filename +
						' at row ' + lexer.getPosition().row + ' column ' +
						lexer.getPosition().col +
						' is already defined. A permabuf file can only belong to one namespace.');
					return false;
					
				}
				if (hasEntries) {
					console.log('\'namespace\' in ' + filename +
						' at row ' + lexer.getPosition().row + ' column ' +
						lexer.getPosition().col +
						' must be defined before any messages, enums, or oneofs.');
					return false;
				}
				if (!parseNamespace()) {
					return false;
				}
				break;
			case 'message':
			case 'enum':
			case 'oneof':
				if (!parseClass(null)) {
					return false;
				}
				hasEntries = true;
				break;
			case '':
				// Reach the end of the file.
				return true;
			default:
				lexer.expectToken(['enum', 'import', 'message', 'namespace', 'oneof', 'end of file'],
					lexer.readToken());
				return false;
		}
	}
}

module.exports = {
	parseFile: parseFile
};
