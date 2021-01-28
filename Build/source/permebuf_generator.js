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
const path = require('path');
const {getPackageTypeDirectoryName} = require('./package_type');
const ClassType = require('./permebuf_class_types');
const {rootDirectory} = require('./root_directory');

function makeSureDirectoryExists(dir) {
	if (fs.existsSync(dir)) {
		return;
	}

	fs.mkdirSync(dir, {recursive: true});
}

function writeFile(contents, filePath) {
	makeSureDirectoryExists(path.dirname(filePath));

	if (fs.existsSync(filePath)) {
		fs.unlinkSync(filePath);
	}
	fs.writeFileSync(filePath, contents);
}

// Type of type.
const FieldType = {
	BOOLEAN: 0,
	ARRAY: 1,
	LIST: 2,
	ENUM: 3,
	MESSAGE: 4,
	MINIMESSAGE: 5,
	ONE_OF: 6,
	STRING: 7,
	BYTES: 8,
	NUMBER: 9,
	SERIVCE: 10
};

// Maximum size of a mini-message, in bytes.
const MAX_MINI_MESSAGE_SIZE = 32;

// Maximum number of a service function.
const MAX_SERVICE_FUNCTION_NUMBER = Math.pow(2, 61) - 1;

function getFullCppName(field) {
	// Convert the namespace to C++ namespace.
	let fullName = '::permebuf::';
	field.namespace.split('.').forEach((namespacePart) => {
		if (namespacePart.length > 0) {
			fullName += namespacePart + '::';
		}
	});
	return fullName + field.cppClassName;
}

function convertArrayOrListToCppTypeSuffix(typeInformation) {
	let typePrefix = '';
	switch (typeInformation.type) {
		case FieldType.ARRAY:
			typename = 'ArrayOf';
			break;
		case FieldType.LIST:
			typeName = 'ListOf';
			break;
		default:
			return false;
	}

	switch (typeInformation.subtype.type) {
		case FieldType.BOOLEAN:
			return typeName + 'Booleans';
		case FieldType.ARRAY:
			return typeName + '<::Permebuf' + convertArrayOrListToCppTypeSuffix(typeInformation.subtype) + '>';
		case FieldType.LIST:
			return typeName + '<::Permebuf' + convertArrayOrListToCppTypeSuffix(typeInformation.subtype) + '>';
		case FieldType.ENUM:
			return typeName + 'Enums<' + typeInformation.subtype.cppClassName + '>';
		case FieldType.MESSAGE:
			return typeName + '<' + typeInformation.subtype.cppClassName + '_Ref>';
		case FieldType.ONE_OF:
			return typeName + 'Oneofs<' + typeInformation.subtype.cppClassName + '_Ref>';
		case FieldType.STRING:
			return typeName + 'Strings';
		case FieldType.BYTES:
			return typeName + 'Bytes';
		case FieldType.NUMBER:
			return typeName + 'Numbers<' + typeInformation.subtype.cppClassName + '>';
		default:
			return false;
	}
}

function generateCppSources(localPath, packageName, packageType, symbolTable, symbolsToGenerate, cppIncludeFiles, cppIncludeLiteFiles) {
	const generatedFilePath = 'permebuf/' + getPackageTypeDirectoryName(packageType) + '/' + packageName + '/' + localPath;
	let headerCpp = '#pragma once\n\n';
	Object.keys(cppIncludeLiteFiles).forEach((includeFile) => {
		headerCpp += '#include "' + includeFile + '"\n';
	})
	let liteHeaderCpp = `#pragma once
#include "permebuf.h"
#include <optional>
#include <stddef.h>
#include <stdint.h>
`;
	let sourceCpp = 
`#include "perception/messages.h"
#include "perception/services.h"
`;
	Object.keys(cppIncludeFiles).forEach((includeFile) => {
		sourceCpp += '#include "' + includeFile + '"\n';
	})

	let namespaceLevels = 0;

	function getTypeInformation(thisClass, field, subtype) {
		const typeStr = subtype != undefined ? subtype : field.type;

		// Handle in-built types.
		switch (typeStr) {
			case 'bytes':
				return {
					'type': FieldType.BYTES,
					'sizeInBytes': 0,
					'sizeInPointers': 1
				};
			case 'string':
				return {
					'type': FieldType.STRING,
					'sizeInBytes': 0,
					'sizeInPointers': 1
				};
			case 'bool':
				return {
					'type': FieldType.BOOLEAN,
					'sizeInBytes': 0,
					'sizeInPointers': 0
				};
			case 'uint8':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'uint8_t',
					'sizeInBytes': 1,
					'sizeInPointers': 0
				};
			case 'int8':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'int8_t',
					'sizeInBytes': 1,
					'sizeInPointers': 0
				};
			case 'uint16':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'uint16_t',
					'sizeInBytes': 2,
					'sizeInPointers': 0
				};
			case 'int16':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'int16_t',
					'sizeInBytes': 2,
					'sizeInPointers': 0
				};
			case 'uint32':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'uint32_t',
					'sizeInBytes': 4,
					'sizeInPointers': 0
				};
			case 'int32':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'int32_t',
					'sizeInBytes': 4,
					'sizeInPointers': 0
				};
			case 'uint64':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'int64_t',
					'sizeInBytes': 8,
					'sizeInPointers': 0
				};
			case 'int64':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'uint64_t',
					'sizeInBytes': 8,
					'sizeInPointers': 0
				};
			case 'float32':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'float',
					'sizeInBytes': 4,
					'sizeInPointers': 0
				};
			case 'float64':
				return {
					'type': FieldType.NUMBER,
					'cppClassName': 'double',
					'sizeInBytes': 8,
					'sizeInPointers': 0
				};
			case 'pointer':
				if (field.name != 'reserve') {
					console.log('Field ' + field.name + '/' + field.number + ' in ' +
						thisClass.fullName + ' has type pointer, but this is only valid for reserved fields.');
					return false;
				}
				return {
					'type': FieldType.MESSAGE,
					'sizeInBytes': 0,
					'sizeInPointers': 1
				};
			case 'enum':
				if (field.name != 'reserve') {
					console.log('Field ' + field.name + '/' + field.number + ' in ' +
						thisClass.fullName + ' has type enum, but this is only valid for reserved fields.');
					return false;
				}
				return {
					'type': FieldType.ENUM,
					'sizeInBytes': 2,
					'sizeInPointers': 0
				};
			case 'oneof':
				if (field.name != 'reserve') {
					console.log('Field ' + field.name + '/' + field.number + ' in ' +
						thisClass.fullName + ' has type oneof, but this is only valid for reserved fields.');
					return false;
				}
				return {
					'type': FieldType.ONEOF,
					'sizeInBytes': 2,
					'sizeInPointers': 1
				};
		}

		if (field.name == 'reserve') {
			console.log('Reserved field ' + field.number + ' in ' + thisClass.fullName + ' has type ' +
				field.type + ' but should be simplified to pointer/enum/oneof.');
			return false;
		}

		// Handle lists and arrays.
		if (typeStr.startsWith('list<') && typeStr.endsWith('>')) {
			const subType = getTypeInformation(thisClass, field, typeStr.slice(5, -1));
			if (!subType) {
				return false;
			}
			return {
				'type': FieldType.LIST,
				'subtype' : subType,
				'sizeInBytes': 0,
				'sizeInPointers': 1
			};
		} else if (typeStr.startsWith('array<') && typeStr.endsWith('>')) {
			const subType = getTypeInformation(thisClass, field, typeStr.slice(6, -1));
			if (!subType) {
				return false;
			}
			return {
				'type': FieldType.ARRAY,
				'subtype' : subType,
				'sizeInBytes': 0,
				'sizeInPointers': 1
			};
		} else if (typeStr.startsWith('bytes<') && typeStr.endsWith('>')) {
			return {
				'type': FieldType.BYTES,
				'subtype' : subType,
				'sizeInBytes': parseInt(typeStr.slice(6, -1)),
				'sizeInPointers': 0
			};
		}

		// Must be either an enum, oneof, or message.

		function getTypeInformationForFullTypeName(fullTypeName) {
			const symbol = symbolTable[fullTypeName];
			if (!symbol) {
				return false;
			}

			switch (symbol.classType) {
				case ClassType.MESSAGE:
					return {
						'type': FieldType.MESSAGE,
						'cppClassName': getFullCppName(symbol),
						'sizeInBytes': 0,
						'sizeInPointers': 1
					};
				case ClassType.ENUM:
					return {
						'type': FieldType.ENUM,
						'cppClassName': getFullCppName(symbol),
						'sizeInBytes': 2,
						'sizeInPointers': 0
					};
				case ClassType.ONEOF:
					return {
						'type': FieldType.ONEOF,
						'cppClassName': getFullCppName(symbol),
						'sizeInBytes': 2,
						'sizeInPointers': 1
					};
				case ClassType.MINIMESSAGE:
					return {
						'type': FieldType.MINIMESSAGE,
						'cppClassName': getFullCppName(symbol),
						'sizeInBytes': MAX_MINI_MESSAGE_SIZE,
						'sizeInPointers': 0
					};
				case ClassType.SERVICE:
					return {
						'type': FieldType.SERVICE,
						'cppClassName': getFullCppName(symbol),
						'sizeInBytes': 8 * 2,
						'sizeInPointers': 0
					};
				default:
					return false;
			}
		}

		// Types starting with . are absolute.
		if (typeStr.startsWith('.')) {
			const typeInformation = getTypeInformationForFullTypeName(typeStr.slice(1));
			if (!typeInformation) {
				console.log('Field ' + field.name + '/' + field.number + ' in ' +
						thisClass.fullName + ' has an unknown type: ' + field.type);
			}
			return typeInformation;
		} else {
			// Walk up the parent classes and namespaces to see what the type refers to.
			let namePrefix = thisClass.fullName;

			while (true) {
				const typeInformation = getTypeInformationForFullTypeName(namePrefix.length > 0 ?
					namePrefix + '.' + typeStr : typeStr);
				if (typeInformation) {
					return typeInformation;
				}

				if (namePrefix == '') {
					console.log('Field ' + field.name + '/' + field.number + ' in ' +
						thisClass.fullName + ' has an unknown type: ' + field.type);
					return false;
				}

				const lastDotIndex = namePrefix.lastIndexOf('.');
				if (lastDotIndex >= 0) {
					namePrefix = namePrefix.slice(0, lastDotIndex);
				} else {
					namePrefix = '';
				}
				
			} while (namePrefix.length > 0);
		}
	}

	function generateEnum(thisEnum) {
		liteHeaderCpp += '\nenum class ' + thisEnum.cppClassName + ' : unsigned short {\n' +
			'\tUnknown = 0';

		const usedFieldNumbers = {0: true};
		const usedFieldNames = {'Unknown': true};

		const enumFieldKeys = Object.keys(thisEnum.fields);

		for (let i = 0; i < enumFieldKeys.length; i++) {
			const enumField = thisEnum.fields[enumFieldKeys[i]];

			if (usedFieldNumbers[enumField.number]) {
				console.log('Field ' + enumField.name + ' in ' + thisEnum.fullName +
					' is using already used value of ' + enumField.number);
				return false;
			}
			if (enumField.number >= 65536) {
				console.log('Field ' + enumField.name + ' in ' + thisEnum.fullName +
					' has a value above the maximum allowed of 65535.');
				return false;
			}
			usedFieldNumbers[enumField.number] = true;

			if (usedFieldNames[enumField.name]) {
				console.log('Enum ' + thisEnum.fullName + ' has multiple fields labelled ' +
					enumField.name);
				return false;
			}
			usedFieldNames[enumField.number] = true;

			liteHeaderCpp += ',\n\t' + enumField.name + ' = ' + enumField.number;
		}

		liteHeaderCpp += '\n};\n';
		return true;
	}

	function generateMessage(thisMessage) {
		liteHeaderCpp += '\nclass ' + thisMessage.cppClassName + ';\n' +
			'\nclass ' + thisMessage.cppClassName + '_Ref {\n' +
			'\tpublic:\n' +
			'\t\t' + thisMessage.cppClassName + '_Ref(::PermebufBase* base, size_t offset);\n' +
			'\t\t' + thisMessage.cppClassName + '* operator->();\n' +
			'\t\tconst ' + thisMessage.cppClassName + '* operator->() const;\n' +
			'\t\tstatic size_t GetSizeInBytes(::PermebufBase* base);\n\n' +
			'\t\tsize_t Address() const;\n' +
			'\tprivate:\n' +
			'\t\t::PermebufBase* buffer_;\n' +
			'\t\tsize_t offset_;\n' +
			'\t\tsize_t size_;\n' +
			'};\n';

		headerCpp += '\nclass ' + thisMessage.cppClassName + ' {\n' +
			'\tpublic:\n' +
			'\t\ttypedef ' + thisMessage.cppClassName + '_Ref Ref;\n';

		let sizeInPointers = 0;
		let sizeInBytes = 0;

		// Pretend all 8 bits are used, so the next boolean we encounter will allocate a new
		// byte.
		let bitfieldByteOffset = -1;
		let bitfieldPointerOffset = -1;
		let bitfieldBit = 7;

		// Construct a map of fields by number, so we can loop through them in order.
		const fieldsByNumber = {0: true};
		let maxFieldNumber = 0;
		const fieldNames = Object.keys(thisMessage.fields);
		for (let i = 0; i < fieldNames.length; i++) {
			const field = thisMessage.fields[fieldNames[i]];
			if (fieldsByNumber[field.number]) {
				console.log('Field ' + field.name + ' has a field number of ' + field.number +
					' but message ' + thisMessage.fullName + ' already has a field with that number.');
				return false;
			}
			fieldsByNumber[field.number] = field;
			if (field.number > maxFieldNumber) {
				maxFieldNumber = field.number;
			}
		}

		for (let i = 0; i < thisMessage.reservedFields.length; i++) {
			const field = thisMessage.reservedFields[i];
			if (fieldsByNumber[field.number]) {
				console.log('Message ' + thisMessage.fullName + ' is trying to reserve the field ' +
					'number ' + field.number + ' but the field number is already used.');
				return false;
			}
			fieldsByNumber[field.number] = field;
			if (field.number > maxFieldNumber) {
				maxFieldNumber = field.number;
			}
		}

		// Loop through the fields.
		for (let i = 1; i <= maxFieldNumber; i++) {
			if (!fieldsByNumber[i]) {
				console.log('Message ' + thisMessage.fullName + ' has a gap in the field numbers. ' +
					'We are missing field ' + i + ', and the highest field number is ' + maxFieldNumber +
					'. You can keep backwards compatibility by reserving a no longer used field.');
				return false;
			}
			const field = fieldsByNumber[i];
			const typeInformation = getTypeInformation(thisMessage, field);
			if (!typeInformation) {
				return false;
			}

			if (typeInformation.type == FieldType.BOOLEAN) {
				// Booleans sizes are handle specially, because they are grouped together into
				// bit field bytes.
				bitfieldBit++;
				if (bitfieldBit == 8) {
					bitfieldBit = 0;
					bitfieldByteOffset = sizeInBytes;
					bitfieldPointerOffset = sizeInPointers;

					sizeInBytes++;
				}
			}

			// Only generate code for fields that are not reserved.
			if (field.name != 'reserve') {
				switch (typeInformation.type) {
					case FieldType.BOOLEAN:
						headerCpp += '\t\tbool Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(bool value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\nbool ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (bitfieldPointerOffset > 0) {
							sourceCpp += '('+ bitfieldPointerOffset + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += bitfieldByteOffset + ';\n' +
							'\tif (address_offset + 1 > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn static_cast<bool>(';
						if (bitfieldBit == 0) {
							sourceCpp += 'buffer_->Read1Byte(offset_ + address_offset)';
						} else {
							sourceCpp += '(buffer_->Read1Byte(offset_ + address_offset) >> ' + bitfieldBit + ')';
						}
						sourceCpp += ' & 1);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(bool value) {\n' +
							'\tsize_t address_offset = ';
						if (bitfieldPointerOffset > 0) {
							sourceCpp += '('+ bitfieldPointerOffset + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += bitfieldByteOffset + ';\n' +
							'\tif (address_offset + 1 > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tuint8_t current_byte = buffer_->Read1Byte(offset_ + address_offset);\n' +
							'\tif (value) {\n' +
							'\t\tcurrent_byte |= ' + (1 << bitfieldBit) + ';\n' +
							'\t} else {\n' +
							'\t\tcurrent_byte &= ~' + (1 << bitfieldBit) + ';\n' +
							'\t}\n' +
							'\tbuffer_->Write1Byte(offset_ + address_offset, current_byte);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tSet' + field.name + '(false);\n' +
							'}\n';
						break;
					case FieldType.ARRAY:
					case FieldType.LIST:
						const cppType = convertArrayOrListToCppTypeSuffix(typeInformation);
						if (!cppType) {
							console.log('Something happened parsing type ' + field.type + ' for field ' + field.name + '/' +
								field.number + ' in message ' + thisMessage.fullName + '.');
							return false;
						}
						const fullCppType = '::Permebuf' + cppType;
						headerCpp += '\t\tconst ' + fullCppType + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(' + fullCppType + ' value);\n' +
							'\t\t' + fullCppType + ' Mutable' + field.name + '();\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\nconst ' + fullCppType + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + fullCppType + '(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + fullCppType + '(buffer_, buffer_->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(' + fullCppType + ' value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, value.Address());\n' +
							'}\n' +
							'\n' + fullCppType + ' ' + thisMessage.cppClassName + '::Mutable' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + fullCppType + '(buffer_, 0);\n' +
							'\t}\n' +
							'\tsize_t message_address = buffer_->ReadPointer(offset_ + address_offset);\n' +
							'\tif (message_address > 0) {\n' +
							'\t\treturn ' + fullCppType + '(buffer_, message_address);\n' +
							'\t}\n' +
							'\tauto object = buffer_->Allocate' + cppType + '();\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, object.Address());\n' +
							'\treturn object;\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->ReadPointer(offset_ + address_offset) != 0;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, 0);\n' +
							'}\n';
						break;
					case FieldType.ENUM:
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 1 > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '::Unknown;\n' +
							'\t}\n' +
							'\treturn static_cast<' +typeInformation.cppClassName + '>(buffer_->Read2Bytes(offset_ + address_offset));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 1 > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->Write2Bytes(offset_ + address_offset, static_cast<uint16_t>(value));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tSet' + field.name + '(' + typeInformation.cppClassName + '::Unknown);\n' +
							'}\n';
						break;
					case FieldType.MESSAGE:
						headerCpp += '\t\tconst ' + typeInformation.cppClassName + '_Ref Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(' + typeInformation.cppClassName + '_Ref value);\n' +
							'\t\t' + typeInformation.cppClassName + '_Ref Mutable' + field.name + '();\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\nconst ' + typeInformation.cppClassName + '_Ref ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '_Ref(buffer_, buffer_->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(' + typeInformation.cppClassName + '_Ref value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, value.Address());\n' +
							'}\n' +
							'\n' + typeInformation.cppClassName + '_Ref ' + thisMessage.cppClassName + '::Mutable' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref(buffer_, 0);\n' +
							'\t}\n' +
							'\tsize_t message_address = buffer_->ReadPointer(offset_ + address_offset);\n' +
							'\tif (message_address > 0) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref(buffer_, message_address);\n' +
							'\t}\n' +
							'\tauto message = buffer_->AllocateMessage<' + typeInformation.cppClassName + '_Ref>();\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, message.Address());\n' +
							'\treturn message;\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->ReadPointer(offset_ + address_offset) != 0;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, 0);\n' +
							'}\n';
						break;
					case FieldType.ONE_OF:
						headerCpp += '\t\tconst ' + typeInformation.cppClassName + '_Ref Get' + field.name + '() const;\n' +
							'\t\t' + typeInformation.cppClassName + '_Ref Mutable' + field.name + '();\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';
						sourceCpp += '\nconst ' + typeInformation.cppClassName + '_Ref ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 2 + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '_Ref(buffer, buffer->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\n' + typeInformation.cppClassName + '_Ref ' + thisMessage.cppClassName + '::Mutable' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 2 + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '_Ref(buffer, buffer->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 2 + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->Read2Bytes(offset_ + address_offset) != 0;\n' +
							'}\n' +
							'\void ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 2 + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->Write2Bytes(offset_ + address_offset, 0);\n' +
							'}\n';
						break;
					case FieldType.STRING:
						headerCpp += '\t\t::PermebufString Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(::PermebufString& value);\n' +
							'\t\tvoid Set' + field.name + '(std::string_view value);\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n::PermebufString ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ::PermebufString(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ::PermebufString(buffer_, buffer_->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(::PermebufString& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, value.Address());\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(std::string_view value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tsize_t string_address = buffer_->AllocateString(value).Address();\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, string_address);\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->ReadPointer(offset_ + address_offset) != 0;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, 0);\n' +
							'}\n';
						break;
					case FieldType.BYTES:
						if (typeInformation.sizeInPointers == 0) {
							console.log('Bytes with fixed sizes aren\'t supported in messages. ' +
							field.type + ' is being used for field ' + field.name + '/' + field.number +
							' in message ' + thisMessage.fullName + '.');
							return false;
						}
						headerCpp += '\t\t::PermebufBytes Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(::PermebufBytes& value);\n' +
							'\t\tvoid Set' + field.name + '(const void* ptr, size_t size);\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n::PermebufBytes ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ::PermebufBytes(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ::PermebufBytes(buffer_, buffer_->ReadPointer(offset_ + address_offset);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(::PermebufBytes& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, value.Address());\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const void* ptr, size_t size) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tsize_t bytes_address = buffer_->AllocateBytes(ptr, size).Address();\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, bytes_address);\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->ReadPointer(offset_ + address_offset) != 0;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->WritePointer(offset_ + address_offset, 0) != 0;\n' +
							'}\n';
						break;
					case FieldType.NUMBER:
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(' + typeInformation.cppClassName + ' value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn static_cast<' + typeInformation.cppClassName + '>(0);\n' +
							'\t}\n' +
							'\tauto temp_value = buffer_->Read' + typeInformation.sizeInBytes +
								(typeInformation.sizeInBytes == 1 ? 'Byte' : 'Bytes') + '(offset_ + address_offset);\n' +
							'\treturn *reinterpret_cast<' + typeInformation.cppClassName + '*>(&temp_value);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(' + typeInformation.cppClassName + ' value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->Write' + typeInformation.sizeInBytes + (typeInformation.sizeInBytes == 1 ? 'Byte' : 'Bytes') +
								'(offset_ + address_offset, *reinterpret_cast<uint' + (typeInformation.sizeInBytes * 8) + '_t*>(&value));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tSet' + field.name + '(0);\n' +
							'}\n';
						break;
					case FieldType.MINIMESSAGE:
						headerCpp += '\t\t' + typeInformation.cppClassName + '_Ref Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '_Ref& value);\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '_Ref::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref + ();\n' +
							'\t}\n' +
							'\t' + typeInformation.cppClassName + '_Ref message;\n'+
							'\tmessage.Deserialize(buffer_->Read8Bytes(address_offset),\n'+
							'\t\tbuffer_->Read8Bytes(address_offset + 1), buffer_->Read8Bytes(address_offset + 2),\n' +
							'\t\tbuffer_->Read8Bytes(address_offset + 3));\n' +
							'\treturn message;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '_Ref& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tsize_t a, b, c, d;\n' +
							'\tvalue.Serialize(a, b, c, d);\n' +
							'\tbuffer_>Write8Bytes(address_offset, a);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 1, b);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 2, c);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 3, d);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tsize_t a, b, c, d;\n' +
							'\tvalue.Serialize(a, b, c, d);\n' +
							'\tbuffer_>Write8Bytes(address_offset, a);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 1, b);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 2, c);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 3, d);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_>Write8Bytes(address_offset, 0);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 1, 0);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 2, 0);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 3, 0);\n' +
							'}\n';
						break;
					case FieldType.SERVICE:
						headerCpp += '\t\t' + typeInformation.cppClassName + '_Ref Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '_Ref& value);\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '_Ref::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '_Ref + ();\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '_Ref(\n'+
							'\t\tbuffer_->Read8Bytes(address_offset), buffer_->Read8Bytes(address_offset + 1));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '_Ref& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_>Write8Bytes(address_offset, value.ProcessId());\n' +
							'\tbuffer_>Write8Bytes(address_offset + 1, value.MessageId());\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_>Write8Bytes(address_offset, value.ProcessId());\n' +
							'\tbuffer_>Write8Bytes(address_offset + 1, value.MessageId());\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '_Ref::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->Read8Bytes(address_offset) != 0;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_>Write8Bytes(address_offset, 0);\n' +
							'\tbuffer_>Write8Bytes(address_offset + 1, 0);\n' +
							'}\n';
						break;
					default:
						return false;
				}
			}

			sizeInBytes += typeInformation.sizeInBytes;
			sizeInPointers += typeInformation.sizeInPointers;
		}

		if (thisMessage.children.length > 0) {
			headerCpp += '\n';
			for (let i = 0; i < thisMessage.children.length; i++) {
				const child = thisMessage.children[i];
				headerCpp += '\t\ttypedef ' + child.cppClassName + ' ' + child.name + ';\n';
			}
		}

		headerCpp += '\n\tprivate:\n' +
			'\t\t::PermebufBase* buffer_;\n' +
			'\t\tsize_t offset_;\n' +
			'\t\tsize_t size_;\n' +
			'};\n';

		sourceCpp += '\n' + thisMessage.cppClassName + '_Ref::' + thisMessage.cppClassName + '_Ref(\n' +
			'\t::PermebufBase* buffer, size_t offset) :\n' +
			'\tbuffer_(buffer) {\n' +
			'\tif (offset == 0) {\n' +
			'\t\toffset_ = 0;\n' +
			'\t\tbuffer_ = 0;\n' +
			'\t} else {\n' +
			'\t\tsize_t bytes;\n' +
			'\t\tsize_ = buffer_->ReadVariableLengthNumber(offset, bytes);\n' +
			'\t\toffset_ = offset + bytes;\n' +
			'\t}\n' +
			'}\n' +
			'\n' + thisMessage.cppClassName + '* ' + thisMessage.cppClassName + '_Ref::operator->() {\n' +
			'\treturn reinterpret_cast<' + thisMessage.cppClassName + '*>(this);\n' +
			'}\n' +
			'\nconst ' + thisMessage.cppClassName + '* ' + thisMessage.cppClassName + '_Ref::operator->() const {\n' +
			'\treturn reinterpret_cast<const ' + thisMessage.cppClassName + '*>(this);\n' +
			'}\n' +
			'\nsize_t ' + thisMessage.cppClassName + '_Ref::GetSizeInBytes(::PermebufBase* buffer) {\n';

		if (sizeInPointers > 0) {
			sourceCpp += '\treturn (' + sizeInPointers + ' << static_cast<size_t>(buffer->GetAddressSize())) + ' + sizeInBytes + ';\n';
		} else {
			sourceCpp += '\treturn ' + sizeInBytes + ';\n';
		}
		sourceCpp += '}\n' +
			'\nsize_t ' + thisMessage.cppClassName + '_Ref::Address() const {\n' +
			'\treturn offset_;\n' +
			'}\n';

		return true;
	}

	function generateMiniMessage(thisMessage) {
		liteHeaderCpp += '\nclass ' + thisMessage.cppClassName + ';\n' +
			'\nclass ' + thisMessage.cppClassName + '_Ref : public ::PermebufMiniMessage {\n' +
			'\tpublic:\n' +
			'\t\t' + thisMessage.cppClassName + '_Ref();\n' +
			'\t\t' + thisMessage.cppClassName + '_Ref(const ' + thisMessage.cppClassName + '& other);\n' +
			'\t\t' + thisMessage.cppClassName + '_Ref(const ' + thisMessage.cppClassName + '_Ref& other);\n' +
			'\t\t' + thisMessage.cppClassName + '_Ref(void* data);\n' +
			'\t\t' + thisMessage.cppClassName + '* operator->();\n' +
			'\t\tconst ' + thisMessage.cppClassName + '* operator->() const;\n' +
			'};\n';

		headerCpp += '\nclass ' + thisMessage.cppClassName + ' : public PermebufMiniMessage {\n' +
			'\tpublic:\n';

		// Size of this message, in bytes.
		let sizeInBytes = 0;

		// Pretend all 8 bits are used, so the next boolean we encounter will allocate a new
		// byte.
		let bitfieldByteOffset = -1;
		let bitfieldPointerOffset = -1;
		let bitfieldBit = 7;

		// Construct a map of fields by number, so we can loop through them in order.
		const fieldsByNumber = {0: true};
		let maxFieldNumber = 0;
		const fieldNames = Object.keys(thisMessage.fields);
		for (let i = 0; i < fieldNames.length; i++) {
			const field = thisMessage.fields[fieldNames[i]];
			if (fieldsByNumber[field.number]) {
				console.log('Field ' + field.name + ' has a field number of ' + field.number +
					' but mini-message ' + thisMessage.fullName + ' already has a field with that number.');
				return false;
			}
			fieldsByNumber[field.number] = field;
			if (field.number > maxFieldNumber) {
				maxFieldNumber = field.number;
			}
		}

		for (let i = 0; i < thisMessage.reservedFields.length; i++) {
			const field = thisMessage.reservedFields[i];
			if (fieldsByNumber[field.number]) {
				console.log('Mini-message ' + thisMessage.fullName + ' is trying to reserve the field ' +
					'number ' + field.number + ' but the field number is already used.');
				return false;
			}
			fieldsByNumber[field.number] = field;
			if (field.number > maxFieldNumber) {
				maxFieldNumber = field.number;
			}
		}

		// Loop through the fields.
		for (let i = 1; i <= maxFieldNumber; i++) {
			if (!fieldsByNumber[i]) {
				console.log('Mini-message ' + thisMessage.fullName + ' has a gap in the field numbers. ' +
					'We are missing field ' + i + ', and the highest field number is ' + maxFieldNumber +
					'. You can keep backwards compatibility by reserving a no longer used field.');
				return false;
			}
			const field = fieldsByNumber[i];
			const typeInformation = getTypeInformation(thisMessage, field);
			if (!typeInformation) {
				return false;
			}

			if (typeInformation.type == FieldType.BOOLEAN) {
				// Booleans sizes are handle specially, because they are grouped together into
				// bit field bytes.
				bitfieldBit++;
				if (bitfieldBit == 8) {
					bitfieldBit = 0;
					bitfieldByteOffset = sizeInBytes;
					sizeInBytes++;
				}
			}

			// Only generate code for fields that are not reserved.
			if (field.name != 'reserve') {
				switch (typeInformation.type) {
					case FieldType.BOOLEAN:
						headerCpp += '\t\tbool Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(bool value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\nbool ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ' + bitfieldByteOffset + ';\n' +
							'\treturn static_cast<bool>(';
						if (bitfieldBit == 0) {
							sourceCpp += 'bytes[address_offset]';
						} else {
							sourceCpp += '(bytes[address_offset] >> ' + bitfieldBit + ')';
						}
						sourceCpp += ' & 1);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(bool value) {\n' +
							'\tsize_t address_offset = ' + bitfieldByteOffset + ';\n' +
							'\tif (value) {\n' +
							'\t\tbytes[address_offset] |= ' + (1 << bitfieldBit) + ';\n' +
							'\t} else {\n' +
							'\t\tbytes[address_offset] &= ~' + (1 << bitfieldBit) + ';\n' +
							'\t}\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tSet' + field.name + '(false);\n' +
							'}\n';
						break;
					case FieldType.ARRAY:
					case FieldType.LIST:
					case FieldType.MESSAGE:
					case FieldType.ONE_OF:
					case FieldType.STRING:
					case FieldType.MINIMESSAGE:
						console.log('Arrays, lists, messages, one-ofs, strings, and mini-messages aren\'t supported in mini-messages. ' +
							field.type + ' is being used for field ' + field.name + '/' + field.number +
							' in message ' + thisMessage.fullName + '.');
						return false;
					case FieldType.ENUM:
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\treturn *reinterpret_cast<const ' + typeInformation.cppClassName + '*>(&bytes[address_offset]);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '& value) {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\t*reinterpret_cast<' + typeInformation.cppClassName + '*>(&bytes[address_offset]) = value;;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tSet' + field.name + '(' + typeInformation.cppClassName + '::Unknown);\n' +
							'}\n';
						break;
					case FieldType.BYTES:
						if (typeInformation.sizeInPointers == 1) {
							console.log('Bytes are only supported in mini-messages if they have a fixed size. ' +
							field.type + ' is being used for field ' + field.name + '/' + field.number +
							' in message ' + thisMessage.fullName + '.');
							return false;
						}
						headerCpp += '\t\tconst void* Raw' + field.name + '() const;\n' +
							'\t\tvoid* Raw' + field.name + '();\n' +
							'\t\tsize_t SizeOf ' + field.name + '() const { return ' + typeInformation.sizeInBytes + '; }\n';

						sourceCpp += '\nconst void* ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\treturn &bytes[address_offset];\n' +
							'}\n' +
							'\nvoid* ' + thisMessage.cppClassName + '::Get' + field.name + '() {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\treturn &bytes[address_offset];\n' +
							'}\n';
						break;
					case FieldType.NUMBER:
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(' + typeInformation.cppClassName + ' value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\tauto temp_value = *(uint' + (typeInformation.sizeInBytes * 8) + '_t*)(&bytes[address_offset]);\n' +
							'\treturn *reinterpret_cast<' + typeInformation.cppClassName + '*>(&temp_value);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(' + typeInformation.cppClassName + ' value) {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\t*(uint' + (typeInformation.sizeInBytes * 8) + '_t*)(&bytes[address_offset]) = ' +
								'*reinterpret_cast<uint' + (typeInformation.sizeInBytes * 8) + '_t*>(&value);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tSet' + field.name + '(0);\n' +
							'}\n';
						break;
					case FieldType.SERVICE:
						headerCpp += '\t\t' + typeInformation.cppClassName + '_Ref Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '_Ref& value);\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + '_Ref ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\treturn ' + typeInformation.cppClassName + '_Ref(*(::perception::ProcessId*)&bytes['+sizeInBytes+'],*(::perception::MessageId*)&bytes['+(sizeInBytes+8)+']);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '_Ref& value) {\n' +
							'\t*(::perception::ProcessId*)(&bytes[' + sizeInBytes + ']) = value.ProcessId();\n' +
							'\t*(::perception::MessageId*)(&bytes[' + (sizeInBytes + 8) + ']) = value.MessageId();\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '& value) {\n' +
							'\t*(::perception::ProcessId*)(&bytes[' + sizeInBytes + ']) = value.ProcessId();\n' +
							'\t*(::perception::MessageId*)(&bytes[' + (sizeInBytes + 8) + ']) = value.MessageId();\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\treturn *(::perception::ProcessId*)(&bytes[' + sizeInBytes + ']) != 0;\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Clear' + field.name + '() {\n' +
							'\tsize_t address_offset = ' + sizeInBytes + ';\n' +
							'\t*(::perception::ProcessId*)(&bytes[' + sizeInBytes + ']) = 0;\n' +
							'\t*(::perception::MessageId*)(&bytes[' + (sizeInBytes + 8) + ']) = 0;\n' +
							'}\n';
						break;
					default:
						return false;
				}
			}

			sizeInBytes += typeInformation.sizeInBytes;

			// Make sure our mini-message can fit within the size constraints.
			if (sizeInBytes > MAX_MINI_MESSAGE_SIZE) {
				console.log('Mini-message ' + thisMessage.fullName + ' is ' + sizeInBytes + ' and the ' +
					'maximum allowed size of a mini-message is ' + MAX_MINI_MESSAGE_SIZE + '.');
				return false;

			}
		}

		if (thisMessage.children.length > 0) {
			headerCpp += '\n';
			for (let i = 0; i < thisMessage.children.length; i++) {
				const child = thisMessage.children[i];
				headerCpp += '\t\ttypedef ' + child.cppClassName + ' ' + child.name + ';\n';
			}
		}

		headerCpp += '};\n';

		sourceCpp += '\n' + thisMessage.cppClassName + '_Ref::' + thisMessage.cppClassName + '_Ref() {}\n' +
			'\n' + thisMessage.cppClassName + '_Ref::' + thisMessage.cppClassName + '_Ref(\n' +
				'\tconst '+ thisMessage.cppClassName + '& other) {\n' +
			'\tother.Serialize(words[0], words[1], words[2], words[3]);'+
			'\t}\n' +
			'\n' + thisMessage.cppClassName + '_Ref::' + thisMessage.cppClassName + '_Ref(\n' +
				'\tconst '+ thisMessage.cppClassName + '_Ref& other) {\n' +
			'\tother.Serialize(words[0], words[1], words[2], words[3]);'+
			'\t}' +
			'\n' + thisMessage.cppClassName + '* ' + thisMessage.cppClassName + '_Ref::operator->() {\n' +
			'\treturn reinterpret_cast<' + thisMessage.cppClassName + '*>(this);\n' +
			'}\n' +
			'\nconst ' + thisMessage.cppClassName + '* ' + thisMessage.cppClassName + '_Ref::operator->() const {\n' +
			'\treturn reinterpret_cast<const ' + thisMessage.cppClassName + '*>(this);\n' +
			'}\n';

		return true;
	}

	function generateOneof(thisOneof) {
		console.log('permebuf_generator.js:generateOneof is not implemented');
		return false;
	}

	function generateService(thisService) {
		console.log(thisService);

		liteHeaderCpp += `
class ${thisService.cppClassName};
class ${thisService.cppClassName}_Server;

class ${thisService.cppClassName}_Ref : public PermebufService {
	public:
		${thisService.cppClassName}_Ref();
		${thisService.cppClassName}_Ref(::perception::ProcessId process_id, ::perception::MessageId message_id);
		${thisService.cppClassName}_Ref(const ${thisService.cppClassName}& other);
		${thisService.cppClassName}_Ref(const ${thisService.cppClassName}_Ref& other);
		${thisService.cppClassName}_Ref(const ${thisService.cppClassName}_Server& other);
		${thisService.cppClassName}* operator->();
		const ${thisService.cppClassName}* operator->() const;
};
`;

		sourceCpp += `
${thisService.cppClassName}_Ref::${thisService.cppClassName}_Ref() :
	::PermebufService(0, 0) {}

${thisService.cppClassName}_Ref::${thisService.cppClassName}_Ref(
	::perception::ProcessId process_id, ::perception::MessageId message_id) :
	::PermebufService(process_id, message_id) {}

${thisService.cppClassName}_Ref::${thisService.cppClassName}_Ref(
	const ${thisService.cppClassName}& other) :
	::PermebufService(other.ProcessId(), other.MessageId()) {}

${thisService.cppClassName}_Ref::${thisService.cppClassName}_Ref(
	const ${thisService.cppClassName}_Ref& other) :
	::PermebufService(other.ProcessId(), other.MessageId()) {}

${thisService.cppClassName}_Ref::${thisService.cppClassName}_Ref(
	const ${thisService.cppClassName}_Server& other) :
	::PermebufService(other.ProcessId(), other.MessageId()) {}

${thisService.cppClassName}* ${thisService.cppClassName}_Ref::operator->() {
	return reinterpret_cast<${thisService.cppClassName}*>(this);
}

const ${thisService.cppClassName}* ${thisService.cppClassName}_Ref::operator->() const {
	return reinterpret_cast<const ${thisService.cppClassName}*>(this);
}
`;

		headerCpp += `
class ${thisService.cppClassName} : public PermebufService {
	public:
		${thisService.cppClassName}();
		${thisService.cppClassName}(::perception::ProcessId process_id, ::perception::MessageId message_id);
		${thisService.cppClassName}(const ${thisService.cppClassName}& other);
		${thisService.cppClassName}(const ${thisService.cppClassName}_Ref& other);
		${thisService.cppClassName}(const ${thisService.cppClassName}_Server& other);

		static std::optional<${thisService.cppClassName}> FindFirstInstance();

		static void ForEachInstance(const std::function<void(${thisService.cppClassName})>& on_each_instance);

		static ::perception::MessageId NotifyOnEachNewInstance(const std::function<void(${thisService.cppClassName})>& on_each_instance);
		static void StopNotifyingOnEachNewInstance(::perception::MessageId message_id);

		::perception::MessageId NotifyOnDisappearance(const std::function<void(${thisService.cppClassName})>& on_disappearance);
		void StopNotifyingOnDisappearance(::perception::MessageId message_id);
`;

sourceCpp += `
${thisService.cppClassName}::${thisService.cppClassName}() :
	::PermebufService(0, 0) {}

${thisService.cppClassName}::${thisService.cppClassName}(
	::perception::ProcessId process_id, ::perception::MessageId message_id) :
	::PermebufService(process_id, message_id) {}

${thisService.cppClassName}::${thisService.cppClassName}(
	const ${thisService.cppClassName}& other) :
	::PermebufService(other.ProcessId(), other.MessageId()) {}

${thisService.cppClassName}::${thisService.cppClassName}(
	const ${thisService.cppClassName}_Ref& other) :
	::PermebufService(other.ProcessId(), other.MessageId()) {}

std::optional<${thisService.cppClassName}> ${thisService.cppClassName}::FindFirstInstance() {
	::perception::ProcessId process_id;
	::perception::MessageId message_id;
	if (::perception::FindFirstInstanceOfService(${thisService.cppClassName}_Server::Name(),
		process_id, message_id)) {
		return std::optional<${thisService.cppClassName}>{process_id, message_id};
	} else {
		return std::nullopt;
	}
}

void ${thisService.cppClassName}::ForEachInstance(
	const std::function<void(${thisService.cppClassName})>& on_each_instance) {
		::perception::ForEachInstanceOfService(${thisService.cppClassName}_Server::Name(),
	[on_each_instance](::perception::ProcessId process_id,
		::perception::MessageId message_id) {
			on_each_instance(${thisService.cppClassName}(process_id, message_id));
		});
}

::perception::MessageId ${thisService.cppClassName}::NotifyOnEachNewInstance(
	const std::function<void(${thisService.cppClassName})>& on_each_instance) {
	return ::perception::NotifyOnEachNewServiceInstance(
		${thisService.cppClassName}_Server::Name(),
		[on_each_instance] (::perception::ProcessId process_id,
			::perception::MessageId message_id) {
		on_each_instance(${thisService.cppClassName}(process_id, message_id));
		});
}

void ${thisService.cppClassName}::StopNotifyingOnEachNewInstance(
	::perception::MessageId message_id) {
	::perception::StopNotifyingOnEachService(message_id);
}

::perception::MessageId ${thisService.cppClassName}::NotifyOnDisappearance(
	const std::function<void(${thisService.cppClassName})>& on_disappearance) {
	return ::perception::NotifyWhenServiceDisappears(
		process_id_, message_id_, on_disappearance);
}

void ${thisService.cppClassName}::StopNotifyingOnDisappearance(
	::perception::MessageId message_id) {
	::perception::StopNotifyWhenServiceDisappears(message_id);
}
`;

		let serverHeaderCpp = `
class ${thisService.cppClassName}_Server : public ::PermebufServer {
	public:
		${thisService.cppClassName}_Server();
		virtual ~${thisService.cppClassName}_Server();

		static std::string_view Name();

	protected:
		virtual bool DelegateMessage(::perception::ProcessId sender,
		size_t metadata, size_t param_1, size_t param_2,
		size_t param_3, size_t param_4, size_t param_5);

`;
		let serverSourceCpp = `
${thisService.cppClassName}_Server::${thisService.cppClassName}_Server() :
	::PermebufServer(Name()) {}

${thisService.cppClassName}_Server::~${thisService.cppClassName}_Server() {}

std::string_view ${thisService.cppClassName}_Server::Name() {
	return "${thisService.fullName}";
}

`;

	let serverDelegator = `
bool ${thisService.cppClassName}_Server::DelegateMessage(
	::perception::ProcessId sender,
	size_t metadata, size_t param_1, size_t param_2,
	size_t param_3, size_t param_4, size_t param_5) {
	switch (GetFunctionNumberFromMetadata(metadata)) {`;

		// Size of this message, in bytes.
		let sizeInBytes = 0;

		// Pretend all 8 bits are used, so the next boolean we encounter will allocate a new
		// byte.
		let bitfieldByteOffset = -1;
		let bitfieldPointerOffset = -1;
		let bitfieldBit = 7;

		// Construct a map of fields by number, so we can check if it's used.
		const fieldsByNumber = {};
		const fieldNames = Object.keys(thisService.fields);
		for (let i = 0; i < fieldNames.length; i++) {
			const field = thisService.fields[fieldNames[i]];
			if (fieldsByNumber[field.number]) {
				console.log('Function ' + field.name + ' has a function number of ' + field.number +
					' but service ' + thisService.fullName + ' already has a function with that number.');
				return false;
			}
			fieldsByNumber[field.number] = field;
			if (field.number > MAX_SERVICE_FUNCTION_NUMBER) {
				console.log('Function ' + field.name + ' has a function number of ' + field.number +
					' in service ' + thisService.fullName + ', but the maximum allowed function number is ' +
					MAX_SERVICE_FUNCTION_NUMBER + '.');
				return false;
			}

			const requestType = getTypeInformation(thisService, field, field.requestType);
			if (!requestType) {
				return false;
			}
			let responseType = null;
			if (field.responseType != null) {
				responseType = getTypeInformation(thisService, field, field.responseType);
				if (!responseType) {
					return false;
				}
			}

			// TODO: Generate the server.

			if (field.isStream) {
				console.log('TODO: Implement code generation for streams in ' +
					'permebuf_generation.js:generateService.');
				return false;
			}

			serverDelegator += `
		case ${field.number}:
			`;

			switch (requestType.type) {
				case FieldType.MINIMESSAGE:
					if (responseType == null) {
						headerCpp +=
`		::perception::Status Send${field.name}(const ${requestType.cppClassName}& request) const;
`;
						sourceCpp += `
::perception::Status ${thisService.cppClassName}::Send${field.name}(
	const ${requestType.cppClassName}& request) const {
		return SendMiniMessage<${requestType.cppClassName}>(${field.number}, request);
}
`;
						serverDelegator += `return ProcessMiniMessage<${requestType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					const ${requestType.cppClassName}& request) {
						Handle${field.name}(sender, request);
					});
`;
						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request);
`;
						serverSourceCpp += `
void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request) {}
`;
					} else if (responseType.type == FieldType.MINIMESSAGE) {
						headerCpp +=
`		StatusOr<${responseType.cppClassName}> Call${field.name}(
			const ${requestType.cppClassName}& request) const;
		void Call${field.name}(const ${requestType.cppClassName}& request,
			const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response);
`;
						sourceCpp += `
StatusOr<${responseType.cppClassName}> ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request) const {
		return SendMiniMessageAndWaitForMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, request);
}
void ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request,
	const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response) const {
		return SendMiniMessageAndNotifyOnMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, request, on_response);
}
`;
serverDelegator += `return ProcessMiniMessageForMiniMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					const ${requestType.cppClassName}& request,
					::PermebufMiniMessageReplier<${responseType.cppClassName}> replier) {
						Handle${field.name}(sender, request, replier);
					});
`;

						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request,
			::PermebufMiniMessageReplier<${responseType.cppClassName}> replier);
`;
						serverSourceCpp += `
		void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request,
			::PermebufMiniMessageReplier<${responseType.cppClassName}> replier)) {
				replier->ReplyWithStatus(::perception::Status::UNIMPLEMENTED);
			}
`;
					} else if (responseType.type == FieldType.MESSAGE) {
						headerCpp +=
`		StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>> Call${field.name}(
			const ${requestType.cppClassName}& request) const;
		void Call${field.name}(const ${requestType.cppClassName}& request,
			const std::function<void(StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>>)>& on_response);
`;
						sourceCpp += `
StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>> ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request) const {
		return SendMiniMessageAndWaitForMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, request);
}
void ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request,
	const std::function<void(StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>>)>& on_response) const {
		return SendMiniMessageAndNotifyOnMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, request, on_response);
}
`;
serverDelegator += `return ProcessMiniMessageForMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					const ${requestType.cppClassName}& request,
					::PermebufMessageReplier<${responseType.cppClassName}> replier) {
						Handle${field.name}(sender, request, replier);
					});
`;
						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request,
			::PermebufMessageReplier<${responseType.cppClassName}> replier);
`;
						serverSourceCpp += `
		void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request,
			::PermebufMessageReplier<${responseType.cppClassName}> replier)) {
				replier->ReplyWithStatus(::perception::Status::UNIMPLEMENTED);
			}
`;
					} else {
						console.log('Function ' + field.name + '/' + field.number + ' in service ' +
							thisService.fullName +
							' has a response type ' + field.responseType +
							' that is neither a message nor a mini-message.');
						return false;
					}
					break;
				case FieldType.MESSAGE:
					if (responseType == null) {
						headerCpp +=
`		::perception::Status Send${field.name}(std::unique_ptr<Permebuf<${requestType.cppClassName}>> request) const;
`;
						sourceCpp += `
::perception::Status ${thisService.cppClassName}::Send${field.name}(
	std::unique_ptr<Permebuf<${requestType.cppClassName}>> request) const {
		return SendMessage<${requestType.cppClassName}>(${field.number}, std::move(request));
}
`;
serverDelegator += `return ProcessMessage<${requestType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					std::unique_ptr<Permebuf<${requestType.cppClassName}>> request) {
						Handle${field.name}(sender, std::move(request));
					});
`;

						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request);
`;
						serverSourceCpp += `
		void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request)) {}
`;
					} else if (responseType.type == FieldType.MINIMESSAGE) {
						headerCpp +=
`		StatusOr<${responseType.cppClassName}> Call${field.name}(
			std::unique_ptr<Permebuf<${requestType.cppClassName}>>& request) const;
		void Call${field.name}(std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
			const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response);
`;
						sourceCpp += `
StatusOr<${responseType.cppClassName}> ${thisService.cppClassName}::Call${field.name}(
	std::unique_ptr<Permebuf<${requestType.cppClassName}>> request) const {
		return SendMessageAndWaitForMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, std::move(request));
}
void ${thisService.cppClassName}::Call${field.name}(
	std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
	const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response) const {
		return SendMessageAndNotifyOnMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, std::move(request), on_response;
}
`;
serverDelegator += `return ProcessMessageForMiniMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
					::PermebufMiniMessageReplier<${responseType.cppClassName}> replier) {
						Handle${field.name}(sender, std::move(request), replier);
					});
`;
						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
			::PermebufMiniMessageReplier<${responseType.cppClassName}> replier);
`;
						serverSourceCpp += `
		void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
			::PermebufMiniMessageReplier<${responseType.cppClassName}> replier)) {
				replier->ReplyWithStatus(::perception::Status::UNIMPLEMENTED);
			}
`;
					} else if (responseType.type == FieldType.MESSAGE) {
						headerCpp +=
`		StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>> Call${field.name}(
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request) const;
		void Call${field.name}(std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
			const std::function<void(StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>>)>& on_response);
`;
						sourceCpp += `
StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>> ${thisService.cppClassName}::Call${field.name}(
	std::unique_ptr<Permebuf<${requestType.cppClassName}>> request) const {
		return SendMessageAndWaitForMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, std::move(request), request);
}
void ${thisService.cppClassName}::Call${field.name}(
	std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
	const std::function<void(StatusOr<std::unique_ptr<Permebuf<${responseType.cppClassName}>>>)>& on_response) const {
		return SendMessageAndNotifyOnMessage<
			${requestType.cppClassName},${responseType.cppClassName},
			>(${field.number}, std::move(request), on_response);
}
`;
serverDelegator += `return ProcessMessageForMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
					::PermebufMessageReplier<${responseType.cppClassName}> replier) {
						Handle${field.name}(sender, std::move(request), replier);
					});
`;
						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
			::PermebufMessageReplier<${responseType.cppClassName}> replier);
`;
						serverSourceCpp += `
		void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			std::unique_ptr<Permebuf<${requestType.cppClassName}>> request,
			::PermebufMessageReplier<${responseType.cppClassName}> replier)) {
				replier->ReplyWithStatus(::perception::Status::UNIMPLEMENTED);
			}
`;
					} else {
						console.log('Function ' + field.name + '/' + field.number + ' in service ' +
							thisService.fullName +
							' has a response type ' + field.responseType +
							' that is neither a message nor a mini-message.');
						return false;
					}
					break;
				default:
					console.log('Function ' + field.name + '/' + field.number + ' in service ' +
						thisService.fullName +
						' has a request type ' + field.requestType +
						' that is neither a message nor a mini-message.');
					return false;
			}
		}

		if (thisService.children.length > 0) {
			headerCpp += '\n';
			for (let i = 0; i < thisService.children.length; i++) {
				const child = thisService.children[i];
				headerCpp +=
`		typedef ${child.cppClassName} ${child.name};
`;
			}
		}

		headerCpp +=
`		typedef ${thisService.cppClassName}_Server Server;
};
`;

serverHeaderCpp += `
};
`;

serverDelegator += `
		default:
			return false;
	}
}
`;

		headerCpp += serverHeaderCpp;
		sourceCpp += serverSourceCpp + serverDelegator;
		return true;
	}

	for (let i = 0; i < symbolsToGenerate.length; i++) {
		const symbolToGenerate = symbolTable[symbolsToGenerate[i]];

		if (namespaceLevels == 0) {
			// First encountered symbol, let's add our namespaces.
			headerCpp += '\nnamespace permebuf {\n';
			liteHeaderCpp += '\nnamespace permebuf {\n';
			sourceCpp += '\nnamespace permebuf {\n';

			namespaceLevels = 1;

			const namespace = symbolToGenerate.namespace;
			if (namespace.length > 0) {
				namespace.split('.').forEach((namespacePart) => {
					if (namespacePart.length == 0) {
						return;
					}
					headerCpp += 'namespace ' + namespacePart + ' {\n';
					liteHeaderCpp += 'namespace ' + namespacePart + ' {\n';
					sourceCpp += 'namespace ' + namespacePart + ' {\n';
					namespaceLevels++;
				});
			}
		}

		switch (symbolToGenerate.classType) {
			case ClassType.ENUM:
				if (!generateEnum(symbolToGenerate)) {
					return false;
				}
				break;
			case ClassType.MESSAGE:
				if (!generateMessage(symbolToGenerate)) {
					return false;
				}
				break;
			case ClassType.MINIMESSAGE:
				if (!generateMiniMessage(symbolToGenerate)) {
					return false;
				}
				break;
			case ClassType.ONEOF:
				if (!generateOneOf(symbolsToGenerate)) {
					return false;
				}
				break;
			case ClassType.SERVICE:
				if (!generateService(symbolToGenerate)) {
					return false;
				}
				break;
			default:
				return false;
		}
	}

	for (let i = 0; i < namespaceLevels; i++) {
		headerCpp += '\n}';
		liteHeaderCpp += '\n}';
		sourceCpp += '\n}';
	}

	// Terminate the files with a new line.
	headerCpp += '\n';
	liteHeaderCpp += '\n';
	sourceCpp += '\n';

	const packageRootDirectory = rootDirectory + getPackageTypeDirectoryName(packageType) + '/' + packageName + '/'; 

	writeFile(liteHeaderCpp, packageRootDirectory + 'generated/include/' + generatedFilePath + '.lite.h');
	writeFile(headerCpp, packageRootDirectory + 'generated/include/' + generatedFilePath + '.h');
	writeFile(sourceCpp, packageRootDirectory + 'generated/source/' + generatedFilePath + '.cc');

	return true;
}

module.exports = {
	generateCppSources: generateCppSources
};
