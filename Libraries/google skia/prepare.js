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
const child_process = require('child_process');
const path = require('path');
const process = require('process');

const git_repository = 'https://github.com/google/skia.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading google/skia');
	// Grab it from github.
	const command = 'git clone ' + git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading google/skia: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update google/skia');
	// Try to update it.
	const command = 'git pull ' + git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating google/skia: ' + exp);
		process.exit(1);
	}
}


// Keep track of third party files we copied from our last build, so we can remove any
// that no longer exist.
let third_party_files_from_last_run = {};
if (fs.existsSync('third_party_files.json')) {
	third_party_files_from_last_run = JSON.parse(fs.readFileSync('third_party_files.json'));
}
let third_party_files = {};

function createDirectoryIfItDoesntExist(dirPath) {
	if (fs.existsSync(dirPath)) {
		return;
	}
	// Make sure parent exists.
	const parentPath = path.dirname(dirPath);
	createDirectoryIfItDoesntExist(parentPath);
	fs.mkdirSync(dirPath);
}

function removeDirectoryIfEmpty(dirPath) {
	if (fs.readdirSync(dirPath).length >= 1) {
		// There are files in this directory.
		return;
	}

	fs.rmdirSync(dirPath);
	const parentPath = path.dirname(dirPath);
	removeDirectoryIfEmpty(parentPath);
}

const filesToIgnore = {};

function copyFile(fromPath, toPath, fromFileStats) {
	if (third_party_files_from_last_run[toPath]) {
		delete third_party_files_from_last_run[toPath];
	}
	third_party_files[toPath] = true;
	createDirectoryIfItDoesntExist(path.dirname(toPath));

	if (fs.existsSync(toPath)) {
		const fromUpdateTime = fromFileStats.mtimeMs;
		const toUpdateTime = fs.lstatSync(toPath).mtimeMs;

		if (fromUpdateTime <= toUpdateTime) {
			// File hasn't changed.
			return;
		}
	}

	console.log('Copying ' + toPath);
	fs.copyFileSync(fromPath, toPath);
}


function copyFilesInDirectory(from, to, extension) {
	const filesInDirectory = fs.readdirSync(from);
	for (let i = 0; i < filesInDirectory.length; i++) {
		const entryName = filesInDirectory[i];
		const fromPath = from + '/' + entryName;
		const toPath = to + '/' + entryName;

		if (filesToIgnore[toPath]) {
			continue;
		}

		const fileStats = fs.lstatSync(fromPath);
		const fileExtension = path.extname(entryName);
		if (!fileStats.isDirectory() &&
			(extension && extension == fileExtension) ||
			(!extension && (fileExtension == '.cpp' || fileExtension == '.h'))) {
			copyFile(fromPath, toPath, fileStats);
		}
	}
}

function replaceInFile(filename, needle, replaceWith) {
	let fileContents = fs.readFileSync(filename, 'utf8');
	fileContents = fileContents.split(needle).join(replaceWith);
	fs.writeFileSync(filename, fileContents);
}

filesToIgnore['source/modules/audioplayer/SkAudioPlayer_oboe.cpp'] = true;
filesToIgnore['source/modules/audioplayer/SkAudioPlayer_sfml.cpp'] = true;
filesToIgnore['source/modules/skshaper/src/SkShaper_coretext.cpp'] = true;
filesToIgnore['source/src/ports/SkDebug_android.cpp'] = true;
filesToIgnore['source/src/ports/SkDebug_win.cpp'] = true;
filesToIgnore['source/src/ports/SkFontHost_win.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_android.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_android_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_android_parser.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_android_parser.h'] = true;
filesToIgnore['source/src/ports/SkFontMgr_custom_directory_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_custom_embedded.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_custom_embedded_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_custom_empty.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_custom_empty_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_fuchsia.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_fuchsia.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_mac_ct.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_mac_ct_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_win_dw.cpp'] = true;
filesToIgnore['source/src/ports/SkFontMgr_win_dw_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkFontConfigInterface_direct.cpp'] = true;
filesToIgnore['source/src/ports/SkFontConfigInterface_direct.h'] = true;
filesToIgnore['source/src/ports/SkFontConfigInterface_direct_factory.cpp'] = true;
filesToIgnore['source/src/ports/skFontMgr_fontconfig.cpp'] = true;
filesToIgnore['source/src/ports/skFontMgr_fontconfig_factory.cpp'] = true;
filesToIgnore['source/src/ports/SkImageEncoder_CG.cpp'] = true;
filesToIgnore['source/src/ports/SkImageEncoder_NDK.cpp'] = true;
filesToIgnore['source/src/ports/SkImageEncoder_WIC.cpp'] = true;
filesToIgnore['source/src/ports/SkImageGeneratorCG.cpp'] = true;
filesToIgnore['source/src/ports/SkImageGeneratorNDK.cpp'] = true;
filesToIgnore['source/src/ports/SkImageGeneratorWIC.cpp'] = true;
filesToIgnore['source/src/ports/SkMemory_mozalloc.cpp'] = true;
filesToIgnore['source/src/ports/SkNDKConversions.cpp'] = true;
filesToIgnore['source/src/ports/SkNDKConversions.h'] = true;
filesToIgnore['source/src/ports/SkOSFile_ios.h'] = true;
filesToIgnore['source/src/ports/SkOSFile_win.cpp'] = true;
filesToIgnore['source/src/ports/SkOSLibrary.h'] = true;
filesToIgnore['source/src/ports/SkOSLibrary_posix.cpp'] = true;
filesToIgnore['source/src/ports/SkOSLibrary_win.cpp'] = true;
filesToIgnore['source/src/ports/SkRemotableFontMgr_win_dw.cpp'] = true;
filesToIgnore['source/src/ports/SkScalerContext_mac_ct.cpp'] = true;
filesToIgnore['source/src/ports/SkScalerContext_mac_ct.h'] = true;
filesToIgnore['source/src/ports/SkScalerContext_win_dw.cpp'] = true;
filesToIgnore['source/src/ports/SkScalerContext_win_dw.h'] = true;
filesToIgnore['source/src/ports/SkTypeface_mac_ct.cpp'] = true;
filesToIgnore['source/src/ports/SkTypeface_mac_ct.h'] = true;
filesToIgnore['source/src/ports/SkTypeface_win_dw.cpp'] = true;
filesToIgnore['source/src/ports/SkTypeface_win_dw.h'] = true;

// Requires png.h:
filesToIgnore['source/src/codec/SkIcoCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkIcoCodec.h'] = true;
filesToIgnore['source/src/codec/SkPngCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkPngCodec.h'] = true;


// Requires dng_area_task.h in Adobe DNG SDK:
filesToIgnore['source/src/codec/SkRawCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkRawCodec.h'] = true;

// Requires wuffs-v0.3.c:
filesToIgnore['source/src/codec/SkWuffsCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkWuffsCodec.h'] = true;

// Requires webp/decode.h.h:
filesToIgnore['source/src/codec/SkWebpCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkWebpCodec.h'] = true;

// Requires avif/avif.h:
filesToIgnore['source/src/codec/SkAvifCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkAvifCodec.h'] = true;

// Requires jxl/codestream_header.h:
filesToIgnore['source/src/codec/SkJpegxlCodec.cpp'] = true;
filesToIgnore['source/src/codec/SkJpegxlCodec.h'] = true;

copyFilesInDirectory('third_party/include/android', 'public/include/android');
copyFilesInDirectory('third_party/include/codec', 'public/include/codec');
copyFilesInDirectory('third_party/include/config', 'public/include/config');
copyFilesInDirectory('third_party/include/core', 'public/include/core');
copyFilesInDirectory('third_party/include/docs', 'public/include/docs');
copyFilesInDirectory('third_party/include/effects', 'public/include/effects');
copyFilesInDirectory('third_party/include/encode', 'public/include/encode');
copyFilesInDirectory('third_party/include/gpu', 'public/include/gpu');
copyFilesInDirectory('third_party/include/gpu/gl', 'public/include/gpu/gl');
copyFilesInDirectory('third_party/include/gpu/mock', 'public/include/gpu/mock');
copyFilesInDirectory('third_party/include/pathops', 'public/include/pathops');
copyFilesInDirectory('third_party/include/ports', 'public/include/ports');
copyFilesInDirectory('third_party/include/private', 'public/include/private');
copyFilesInDirectory('third_party/include/private/gpu/ganesh', 'public/include/private/gpu/ganesh');
copyFilesInDirectory('third_party/include/private/chromium', 'public/include/private/chromium');
copyFilesInDirectory('third_party/include/sksl', 'public/include/sksl');
copyFilesInDirectory('third_party/include/svg', 'public/include/svg');
copyFilesInDirectory('third_party/include/utils', 'public/include/utils');
copyFilesInDirectory('third_party/src/codec', 'source/src/codec');
copyFilesInDirectory('third_party/src/core', 'source/src/core');
copyFilesInDirectory('third_party/src/effects', 'source/src/effects');
copyFilesInDirectory('third_party/src/effects/imagefilters', 'source/src/effects/imagefilters');
copyFilesInDirectory('third_party/src/fonts', 'source/src/fonts');
copyFilesInDirectory('third_party/src/gpu', 'source/src/gpu');
copyFilesInDirectory('third_party/src/gpu/ganesh', 'source/src/gpu/ganesh');
copyFilesInDirectory('third_party/src/gpu/ganesh/effects', 'source/src/gpu/ganesh/effects');
copyFilesInDirectory('third_party/src/gpu/ganesh/geometry', 'source/src/gpu/ganesh/geometry');
copyFilesInDirectory('third_party/src/gpu/ganesh/gradients', 'source/src/gpu/ganesh/gradients');
copyFilesInDirectory('third_party/src/gpu/ganesh/glsl', 'source/src/gpu/ganesh/glsl');
copyFilesInDirectory('third_party/src/gpu/ganesh/mock', 'source/src/gpu/ganesh/mock');
copyFilesInDirectory('third_party/src/gpu/ganesh/mtl', 'source/src/gpu/ganesh/mtl');
copyFilesInDirectory('third_party/src/gpu/ganesh/ops', 'source/src/gpu/ganesh/ops');
copyFilesInDirectory('third_party/src/gpu/ganesh/tessellate', 'source/src/gpu/ganesh/tessellate');
copyFilesInDirectory('third_party/src/gpu/ganesh/text', 'source/src/gpu/ganesh/text');
copyFilesInDirectory('third_party/src/gpu/tessellate', 'source/src/gpu/tessellate');
copyFilesInDirectory('third_party/src/image', 'source/src/image');
copyFilesInDirectory('third_party/src/images', 'source/src/images');
copyFilesInDirectory('third_party/src/lazy', 'source/src/lazy');
copyFilesInDirectory('third_party/src/opts', 'source/src/opts');
copyFilesInDirectory('third_party/src/pathops', 'source/src/pathops');
copyFilesInDirectory('third_party/src/pdf', 'source/src/pdf');
copyFilesInDirectory('third_party/src/ports', 'source/src/ports');
copyFilesInDirectory('third_party/src/sfnt', 'source/src/sfnt');
copyFilesInDirectory('third_party/src/shaders', 'source/src/shaders');
copyFilesInDirectory('third_party/src/shaders/gradients', 'source/src/shaders/gradients');
copyFilesInDirectory('third_party/src/sksl', 'source/src/sksl');
copyFilesInDirectory('third_party/src/sksl/analysis', 'source/src/sksl/analysis');
copyFilesInDirectory('third_party/src/sksl/codegen', 'source/src/sksl/codegen');
copyFilesInDirectory('third_party/src/sksl/dsl', 'source/src/sksl/dsl');
copyFilesInDirectory('third_party/src/sksl/dsl/priv', 'source/src/sksl/dsl/priv');
copyFilesInDirectory('third_party/src/sksl/generated', 'source/src/sksl/generated', '.sksl');
copyFilesInDirectory('third_party/src/sksl/ir', 'source/src/sksl/ir');
copyFilesInDirectory('third_party/src/sksl/lex', 'source/src/sksl/lex');
copyFilesInDirectory('third_party/src/sksl/tracing', 'source/src/sksl/tracing');
copyFilesInDirectory('third_party/src/sksl/transform', 'source/src/sksl/transform');
copyFilesInDirectory('third_party/src/svg', 'source/src/svg');
copyFilesInDirectory('third_party/src/text', 'source/src/text');
copyFilesInDirectory('third_party/src/text/gpu', 'source/src/text/gpu');
copyFilesInDirectory('third_party/src/utils', 'source/src/utils');
copyFilesInDirectory('third_party/src/xml', 'source/src/xml');
copyFilesInDirectory('third_party/src/xps', 'source/src/xps');


copyFilesInDirectory('third_party/modules/audioplayer', 'source/modules/audioplayer', '.cpp');
copyFilesInDirectory('third_party/modules/audioplayer', 'public/modules/audioplayer', '.h');
copyFilesInDirectory('third_party/modules/particles/src', 'source/modules/particles/src', '.cpp');
copyFilesInDirectory('third_party/modules/particles/include', 'public/modules/particles/include', '.h');
copyFilesInDirectory('third_party/modules/skcms', 'public/modules/skcms', '.h');
copyFilesInDirectory('third_party/modules/skcms', 'source/modules/skcms', '.cc');
copyFilesInDirectory('third_party/modules/skcms/src', 'public/modules/skcms/src', '.h');
copyFilesInDirectory('third_party/modules/skcms', 'public/modules/skcms', '.h');
copyFilesInDirectory('third_party/modules/skottie/include', 'public/modules/skottie/include', '.h');
copyFilesInDirectory('third_party/modules/skottie/src', 'public/modules/skottie/src');
copyFilesInDirectory('third_party/modules/skottie/src/animator', 'public/modules/skottie/src/animator');
copyFilesInDirectory('third_party/modules/skottie/src/effects', 'public/modules/skottie/src/effects');
copyFilesInDirectory('third_party/modules/skottie/src/layers', 'public/modules/skottie/src/layers');
copyFilesInDirectory('third_party/modules/skottie/src/layers/shapelayer', 'public/modules/skottie/src/layers/shapelayer');
copyFilesInDirectory('third_party/modules/skottie/src/text', 'public/modules/skottie/src/text');
copyFilesInDirectory('third_party/modules/skottie/utils', 'source/modules/skottie/utils', '.cpp');
copyFilesInDirectory('third_party/modules/skottie/utils', 'public/modules/skottie/utils', '.h');
copyFilesInDirectory('third_party/modules/skparagraph/include', 'public/modules/skparagraph/include', '.h');
copyFilesInDirectory('third_party/modules/skparagraph', 'source/modules/skparagraph');
copyFilesInDirectory('third_party/modules/skresources/include', 'public/modules/skresources/include', '.h');
copyFilesInDirectory('third_party/modules/skresources/src', 'source/modules/skresources/src', '.cpp');
copyFilesInDirectory('third_party/modules/sksg/include', 'public/modules/sksg/include', '.h');
copyFilesInDirectory('third_party/modules/sksg/src', 'source/modules/sksg/src');
copyFilesInDirectory('third_party/modules/skshaper/include', 'public/modules/skshaper/include', '.h');
copyFilesInDirectory('third_party/modules/skshaper/src', 'source/modules/skshaper/src');
copyFilesInDirectory('third_party/modules/skunicode/include', 'public/modules/skunicode/include', '.h');
copyFilesInDirectory('third_party/modules/skunicode/src', 'source/modules/skunicode/src');
copyFilesInDirectory('third_party/modules/svg/include', 'public/modules/svg/include', '.h');
copyFilesInDirectory('third_party/modules/svg/src', 'source/modules/svg/src');


replaceInFile('source/modules/skcms/skcms.cc',
	'#include "skcms',
	'#include "modules/skcms/skcms');
replaceInFile('source/modules/skcms/skcms.cc',
	'#include "src/Transform_inl.h"',
	'#include "modules/skcms/src/Transform_inl.h"');
replaceInFile('public/modules/skcms/skcms_internal.h',
	'#include "skcms',
	'#include "modules/skcms/skcms');
replaceInFile('public/include/core/SkTypes.h',
		'#if !defined(SK_BUILD_FOR_ANDROID) && !defined(SK_BUILD_FOR_IOS) && !defined(SK_BUILD_FOR_WIN) && \\',
		'#if 0 && !defined(SK_BUILD_FOR_ANDROID) && !defined(SK_BUILD_FOR_IOS) && !defined(SK_BUILD_FOR_WIN) && \\');


// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));



