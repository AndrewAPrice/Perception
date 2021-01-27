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
const {PackageType} = require('./package_type');
const permebufParser = require('./permebuf_parser');
const permebufGenerator = require('./permebuf_generator');

function compilePermebufToCpp(localPath,
	packageName,
	packageType,
	dependencies) {
	const symbolTable = {};
	const symbolsToGenerate = [];
	const cppIncludeFiles = {};
	const cppIncludeLiteFiles = {};
	const importedFilesMap = {}

	if (!permebufParser.parseFile(localPath, packageName, packageType, importedFilesMap,
		symbolTable, symbolsToGenerate, cppIncludeFiles, cppIncludeLiteFiles, true)) {
		return false;
	}

	if (!permebufGenerator.generateCppSources(localPath, packageName, packageType,
		symbolTable, symbolsToGenerate, cppIncludeFiles, cppIncludeLiteFiles)) {
		return false;
	}

	// Convert the imported files map into an array.
	Object.keys(importedFilesMap).forEach((importedFile) => {
		dependencies.push(importedFile);
	});

	return true;
}

module.exports = {
	compilePermebufToCpp: compilePermebufToCpp
};
