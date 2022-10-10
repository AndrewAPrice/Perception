
// Copyright 2022 Google LLC
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

const path = require('path');
const {escapePath, forEachIfDefined} = require('./utils');

function constructIncludeAndDefineParams(
    packageDirectory, metadata, subDirectory, addLocaleIncludes,
    resolveFullPath) {
  const params = [];
  let alreadyIncludedThisDir = false;
  const thisDir = packageDirectory + subDirectory;

  function addInclude(includeDir) {
    if (includeDir == thisDir) {
      alreadyIncludedThisDir = true;
    }
    if (resolveFullPath) {
      includeDir = path.resolve(includeDir);
    } else {
      includeDir =  escapePath(includeDir);
    }
    params.push('-isystem ' + includeDir);
  }

  // Set up all the of our include directories.
  if (addLocaleIncludes) {
    forEachIfDefined(metadata.include, addInclude);
  }
  forEachIfDefined(metadata.public_include, addInclude);

  if (!alreadyIncludedThisDir) {
    addInclude(thisDir);
  }

  // Set up all of our defines.
  const defines = {};
  const definesToRemove = [];

  function addDefine(define) {
    if (define.startsWith('-')) {
      definesToRemove.push(define.substr(1));
    } else {
      defines[define] = true;
    }
  }

  forEachIfDefined(metadata.public_define, addDefine);
  forEachIfDefined(metadata.define, addDefine);

  definesToRemove.forEach((define) => {
    delete defines[define];
  });

  Object.keys(defines).forEach((define) => {
    params.push('-D' + define);
  });

  return params;
}

module.exports = {
  constructIncludeAndDefineParams : constructIncludeAndDefineParams
};
