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

const fs = require('fs');
const {EOL} = require('os');
const {constructIncludeAndDefineParams} = require('./build_parameters');
const {forEachIfDefined} = require('./utils');

// Maybe generates/updates a .clang_complete file.
function maybeGenerateClangCompleteFile(
    packageDirectory, metadata, subDirectory, addLocalIncludes) {
  const clangCompletePath =
      packageDirectory + subDirectory + '/.clang_complete';
  if (fs.existsSync(clangCompletePath)) {
    const filestamp = fs.lstatSync(clangCompletePath).mtimeMs;
    if (filestamp > metadata.metadata_updated) {
      // The metadata hasn't been updated since this .clang_complete file
      // generated, so there's no need to regenerate it.
      return;
    }
  }

  const fileContents =
      ['-D PERCEPTION', '-ffreestanding', '-nostdlib', '-mno-red-zone '].join(
          EOL) +
      EOL +
      constructIncludeAndDefineParams(
          packageDirectory, metadata, subDirectory, addLocalIncludes, true)
          .join(EOL);

  fs.writeFileSync(clangCompletePath, fileContents);

  const gitIgnorePath = packageDirectory + subDirectory + '/.gitignore';
  if (!fs.existsSync(gitIgnorePath)) {
    fs.writeFileSync(gitIgnorePath, '.clang_complete');
  }
}

// Maybe generates/updates .clang_complete files for a project.
function maybeGenerateClangCompleteFilesForProject(packageDirectory, metadata) {
  if (fs.existsSync(packageDirectory + 'source')) {
    maybeGenerateClangCompleteFile(packageDirectory, metadata, 'source', true);
  }

  forEachIfDefined(metadata.my_include, headerDirectory => {
    maybeGenerateClangCompleteFile(
        packageDirectory, metadata, headerDirectory, true);
  });

  forEachIfDefined(metadata.my_public_include, headerDirectory => {
    maybeGenerateClangCompleteFile(
        packageDirectory, metadata, headerDirectory, false);
  });
}

module.exports = {
  maybeGenerateClangCompleteFilesForProject : maybeGenerateClangCompleteFilesForProject
};