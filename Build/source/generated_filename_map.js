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

// A map of the generated source file back to the original file, so if there's
// an error building the generated file, it's better to show the original file
// that is broken.
const generatedFilenameMap = {};

// Returns the filename we should display, respecting generated files that
// should remap back to the original file.
function getDisplayFilename(filename) {
  const remappedFileName = generatedFilenameMap[filename];
  return remappedFileName ? remappedFileName : filename;
}

module.exports = {
  generatedFilenameMap : generatedFilenameMap,
  getDisplayFilename : getDisplayFilename
};