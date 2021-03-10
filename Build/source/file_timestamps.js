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

// Cache of timestamps, so we don't have to lstat a file multiple times during
// a session.
let lastModifiedTimestampByFile = {};

// Gets the timestamp of when a file was last modified.
function getFileLastModifiedTimestamp(file) {
	const cachedTimestamp = lastModifiedTimestampByFile[file];
	if (cachedTimestamp != undefined) {
		return cachedTimestamp;
	}

	const timestamp = fs.existsSync(file) ? fs.lstatSync(file).mtimeMs : Number.MAX_VALUE;
	lastModifiedTimestampByFile[file] = timestamp;
	return timestamp;
}

function forgetFileLastModifiedTimestamp(file) {
	lastModifiedTimestampByFile[file] = undefined;;
}

module.exports = {
	forgetFileLastModifiedTimestamp, forgetFileLastModifiedTimestamp,
	getFileLastModifiedTimestamp: getFileLastModifiedTimestamp
};
