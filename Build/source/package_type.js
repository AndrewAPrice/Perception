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

// Type of package we are building.
const PackageType = {
  APPLICATION: 0,
  LIBRARY: 1,
  KERNEL: 2
};

function getPackageTypeDirectoryName(packageType) {
  switch (packageType) {
    case PackageType.APPLICATION:
      return 'Applications';
    case PackageType.LIBRARY:
      return 'Libraries';
    case PackageType.KERNEL:
      return 'Kernel';
    default:
      return undefined;
  }
};

function getNameOfPackage(packageType, packageName) {
  switch (packageType) {
    case PackageType.APPLICATION:
      return 'Application ' + packageName;
    case PackageType.LIBRARY:
      return 'Library ' + packageName;
    case PackageType.KERNEL:
      return 'Kernel';
    default:
      return undefined;
  }
}

function getNonCapitalizedNameOfPackage(packageType, packageName) {
  switch (packageType) {
    case PackageType.APPLICATION:
      return 'application ' + packageName;
    case PackageType.LIBRARY:
      return 'library ' + packageName;
    case PackageType.KERNEL:
      return 'kernel';
    default:
      return undefined;
  }
}

function maybePrefixPackageName(packageType, packageName) {
  switch (packageType) {
    case PackageType.LIBRARY:
    default:
      return packageName;
    case PackageType.APPLICATION:
      return '<application>' + packageName;
    case PackageType.KERNEL:
      return '<kernel>';
  }
}

module.exports = {
  PackageType: PackageType,
  getPackageTypeDirectoryName: getPackageTypeDirectoryName,
  getNameOfPackage: getNameOfPackage,
  getNonCapitalizedNameOfPackage: getNonCapitalizedNameOfPackage,
  maybePrefixPackageName: maybePrefixPackageName
};