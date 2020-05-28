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

const {rootDirectory} = require('./root_directory');
const {PackageType} = require('./package_type');

// Gets the directory for a package type and package name.
function getPackageDirectory(packageType, packageName) {
	switch (packageType) {
		case PackageType.APPLICATION:
		return rootDirectory + 'Applications/' + packageName + '/';
		case PackageType.LIBRARY:
		return rootDirectory + 'Libraries/' + packageName + '/';
		case PackageType.KERNEL:
		return rootDirectory + 'Kernel/';
		default:
			return '';
	}
}

module.exports = {
	getPackageDirectory: getPackageDirectory
};
