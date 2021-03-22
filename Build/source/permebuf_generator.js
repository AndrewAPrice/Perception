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
	ONEOF: 6,
	STRING: 7,
	BYTES: 8,
	NUMBER: 9,
	SERIVCE: 10,
	SHARED_MEMORY: 11
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
			return typeName + '<' + typeInformation.subtype.cppClassName + '>';
		case FieldType.ONEOF:
			return typeName + 'OneOfs<' + typeInformation.subtype.cppClassName + '>';
		case FieldType.STRING:
			return typeName + 'Strings';
		case FieldType.BYTES:
			return typeName + 'Bytes';
		case FieldType.NUMBER:
			return typeName + 'Numbers<' + typeInformation.subtype.cppClassName + '>';
		case FieldType.SHARED_MEMORY:
			return typeName + 'SharedMemory<' + typeInformation.subtype.cppClassName + '>';
		default:
			return false;
	}
}

function generateCppSources(localPath, packageName, packageType, symbolTable, symbolsToGenerate, cppIncludeFiles) {
	const generatedFilePath = 'permebuf/' + getPackageTypeDirectoryName(packageType) + '/' + packageName + '/' + localPath;
	let headerCpp = `#pragma once

#include <optional>
#include <stddef.h>
#include <stdint.h>

#include "perception/shared_memory.h"
#include "permebuf.h"
`;

	let sourceCpp = 
`#include "perception/fibers.h"
#include "perception/messages.h"
#include "perception/services.h"
`;
	Object.keys(cppIncludeFiles).forEach((includeFile) => {
		sourceCpp += '#include "' + includeFile + '"\n';
	})

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
			case 'SharedMemory':
				return {
					'type': FieldType.SHARED_MEMORY,
					'sizeInBytes': 8,
					'sizeInPointers': 0
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
		headerCpp += '\nenum class ' + thisEnum.cppClassName + ' : unsigned short {\n' +
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

			headerCpp += ',\n\t' + enumField.name + ' = ' + enumField.number;
		}

		headerCpp += '\n};\n';
		return true;
	}

	function generateMessage(thisMessage) {
		headerCpp += `
class ${thisMessage.cppClassName} {
	public:
		${thisMessage.cppClassName}() {}
		${thisMessage.cppClassName}(::PermebufBase* buffer, size_t offset);
		${thisMessage.cppClassName}(const ${thisMessage.cppClassName}& other) = default;
		size_t Address() const {
			return offset_ - PermebufBase::GetBytesNeededForVariableLengthNumber(size_);
		}
		static size_t GetSizeInBytes(::PermebufBase* buffer);
`;

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
						headerCpp += '\t\tconst ' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(' + typeInformation.cppClassName + ' value);\n' +
							'\t\t' + typeInformation.cppClassName + ' Mutable' + field.name + '();\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\nconst ' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '(buffer_, buffer_->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(' + typeInformation.cppClassName + ' value) {\n' +
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
							'\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Mutable' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '(buffer_, 0);\n' +
							'\t}\n' +
							'\tsize_t message_address = buffer_->ReadPointer(offset_ + address_offset);\n' +
							'\tif (message_address > 0) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '(buffer_, message_address);\n' +
							'\t}\n' +
							'\tauto message = buffer_->AllocateMessage<' + typeInformation.cppClassName + '>();\n' +
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
					case FieldType.ONEOF:
						headerCpp += '\t\tconst ' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\t' + typeInformation.cppClassName + ' Mutable' + field.name + '();\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';
						sourceCpp += '\nconst ' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 2 + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '(buffer, buffer_->ReadPointer(offset_ + address_offset));\n' +
							'}\n' +
							'\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Mutable' + field.name + '() {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '(' + sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + 2 + buffer_->GetAddressSizeInBytes() > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '(buffer_, 0);\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '(buffer, buffer_->ReadPointer(offset_ + address_offset));\n' +
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
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + ' + ();\n' +
							'\t}\n' +
							'\t' + typeInformation.cppClassName + ' message;\n'+
							'\tmessage.Deserialize(buffer_->Read8Bytes(offset_ + address_offset),\n'+
							'\t\tbuffer_->Read8Bytes(offset_ + address_offset + 1), buffer_->Read8Bytes(offset_ + address_offset + 2),\n' +
							'\t\tbuffer_->Read8Bytes(offset_ + address_offset + 3));\n' +
							'\treturn message;\n' +
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
							'\tbuffer_->Write8Bytes(offset_ + address_offset, a);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 1, b);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 2, c);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 3, d);\n' +
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
							'\tbuffer_->Write8Bytes(offset_ + address_offset, a);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 1, b);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 2, c);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 3, d);\n' +
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
							'\tbuffer_->Write8Bytes(offset_ + address_offset, 0);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 1, 0);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 2, 0);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 3, 0);\n' +
							'}\n';
						break;
					case FieldType.SERVICE:
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '_Server& value);\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn ' + typeInformation.cppClassName + '();\n' +
							'\t}\n' +
							'\treturn ' + typeInformation.cppClassName + '(\n'+
							'\t\tbuffer_->Read8Bytes(offset_ + address_offset), buffer_->Read8Bytes(offset_ + address_offset + 8));\n' +
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
							'\tbuffer_->Write8Bytes(offset_ + address_offset, value.GetProcessId());\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 8, value.GetMessageId());\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '_Server& value) {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn;\n' +
							'\t}\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset, value.GetProcessId());\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 8, value.GetMessageId());\n' +
							'}\n' +
							'\nbool ' + thisMessage.cppClassName + '::Has' + field.name + '() const {\n' +
							'\tsize_t address_offset = ';
						if (sizeInPointers > 0) {
							sourceCpp += '('+ sizeInPointers + ' << static_cast<size_t>(buffer_->GetAddressSize())) + ';
						}
						sourceCpp += sizeInBytes + ';\n' +
							'\tif (address_offset + ' + typeInformation.sizeInBytes + ' > size_) {\n' +
							'\t\treturn false;\n' +
							'\t}\n' +
							'\treturn buffer_->Read8Bytes(offset_ + address_offset) != 0;\n' +
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
							'\tbuffer_->Write8Bytes(offset_ + address_offset, 0);\n' +
							'\tbuffer_->Write8Bytes(offset_ + address_offset + 8, 0);\n' +
							'}\n';
						break;
					case FieldType.SHARED_MEMORY:
						headerCpp += `
		bool Has${field.name}() const;
		::perception::SharedMemory Get${field.name}() const;
		void Set${field.name}(const ::perception::SharedMemory& value);
		void Clear${field.name}();
`;
						sourceCpp += `
bool ${thisMessage.cppClassName}::Has${field.name}() const {
	size_t address_offset = `;
						if (sizeInPointers > 0) {
							sourceCpp += `('${sizeInPointers} << static_cast<size_t>(buffer_->GetAddressSize())) + `;
						}
						sourceCpp += `${sizeInBytes};
	if (address_offset + ${typeInformation.sizeInBytes} > size_) {
		return false;
	}
	return buffer_->Read8Bytes(offset_ + address_offset) != 0;
}

::perception::SharedMemory ${thisMessage.cppClassName}::Get${field.name}() const {
	size_t address_offset = `;
						if (sizeInPointers > 0) {
							sourceCpp += `('${sizeInPointers} << static_cast<size_t>(buffer_->GetAddressSize())) + `;
						}
						sourceCpp += `${sizeInBytes};
	if (address_offset + ${typeInformation.sizeInBytes} > size_) {
		return ::perception::SharedMemory(0);
	}
	return ::perception::SharedMemory(buffer_->Read8Bytes(offset_ + address_offset));
}

void ${thisMessage.cppClassName}::Set${field.name}(const ::perception::SharedMemory& value) {
	size_t address_offset = `;
						if (sizeInPointers > 0) {
							sourceCpp += `('${sizeInPointers} << static_cast<size_t>(buffer_->GetAddressSize())) + `;
						}
						sourceCpp += `${sizeInBytes};
	if (address_offset + ${typeInformation.sizeInBytes} > size_) {
		return;
	}
	buffer_->Write8Bytes(offset_ + address_offset, value.GetId());
}

void ${thisMessage.cppClassName}::Clear${field.name}() {
	Set${field.name}(::perception::SharedMemory(0));
}
`;
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

		headerCpp += `
		private:
			::PermebufBase* buffer_;
			size_t offset_;
			size_t size_;
	};`;

		sourceCpp += `
${thisMessage.cppClassName}::${thisMessage.cppClassName}(
	PermebufBase* buffer, size_t offset) : buffer_(buffer) {
	if (offset == 0) {
		offset_ = 0;
		buffer_ = 0;
	} else {
		size_t bytes;
		size_ = buffer_->ReadVariableLengthNumber(offset, bytes);
		offset_ = offset + bytes;
	}
}

size_t ${thisMessage.cppClassName}::GetSizeInBytes(::PermebufBase* buffer) {`;
		if (sizeInPointers > 0) {
			sourceCpp += `
	return (${sizeInPointers} << static_cast<size_t>(buffer->GetAddressSize())) + ${sizeInBytes};
}
`;
		} else {
			sourceCpp += `
	return ${sizeInBytes};
}
`;
		}

		return true;
	}

	function generateMiniMessage(thisMessage) {
		headerCpp += `
class ${thisMessage.cppClassName} : public PermebufMiniMessage {
	public:
		${thisMessage.cppClassName}() {}
		${thisMessage.cppClassName}(const ${thisMessage.cppClassName}& other) = default;
`;

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
					case FieldType.ONEOF:
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
						headerCpp += '\t\t' + typeInformation.cppClassName + ' Get' + field.name + '() const;\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '& value);\n' +
							'\t\tvoid Set' + field.name + '(const ' + typeInformation.cppClassName + '_Server& value);\n' +
							'\t\tbool Has' + field.name + '() const;\n' +
							'\t\tvoid Clear' + field.name + '();\n';

						sourceCpp += '\n' + typeInformation.cppClassName + ' ' + thisMessage.cppClassName + '::Get' + field.name + '() const {\n' +
							'\treturn ' + typeInformation.cppClassName + '(*(::perception::ProcessId*)&bytes['+sizeInBytes+'],*(::perception::MessageId*)&bytes['+(sizeInBytes+8)+']);\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '& value) {\n' +
							'\t*(::perception::ProcessId*)(&bytes[' + sizeInBytes + ']) = value.GetProcessId();\n' +
							'\t*(::perception::MessageId*)(&bytes[' + (sizeInBytes + 8) + ']) = value.GetMessageId();\n' +
							'}\n' +
							'\nvoid ' + thisMessage.cppClassName + '::Set' + field.name + '(const ' + typeInformation.cppClassName + '_Server& value) {\n' +
							'\t*(::perception::ProcessId*)(&bytes[' + sizeInBytes + ']) = value.GetProcessId();\n' +
							'\t*(::perception::MessageId*)(&bytes[' + (sizeInBytes + 8) + ']) = value.GetMessageId();\n' +
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
					case FieldType.SHARED_MEMORY:
						headerCpp += `
		bool Has${field.name}() const;
		::perception::SharedMemory Get${field.name}() const;
		void Set${field.name}(const ::perception::SharedMemory& value);
		void Clear${field.name}();
`;
						sourceCpp += `
bool ${thisMessage.cppClassName}::Has${field.name}() const {
	return *(size_t*)(&bytes[${sizeInBytes}]) != 0;
}

::perception::SharedMemory ${thisMessage.cppClassName}::Get${field.name}() const {
	return ::perception::SharedMemory(*(size_t*)(&bytes[${sizeInBytes}]));
}

void ${thisMessage.cppClassName}::Set${field.name}(const ::perception::SharedMemory& value) {
	*(size_t*)(&bytes[${sizeInBytes}]) = value.GetId();
}

void ${thisMessage.cppClassName}::Clear${field.name}() {
	Set${field.name}(::perception::SharedMemory(0));
}
`;
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

		return true;
	}

	function generateOneOf(thisOneOf) {
		let enumHeaderCpp = `
enum class ${thisOneOf.cppClassName}_Options : unsigned short {
	Unknown = 0`;

		headerCpp += `
class ${thisOneOf.cppClassName} {
	public:
		typedef ${thisOneOf.cppClassName}_Options Options;
		${thisOneOf.cppClassName}() {}
		${thisOneOf.cppClassName}(::PermebufBase* buffer, size_t offset);
		${thisOneOf.cppClassName}(const ${thisOneOf.cppClassName}& other) = default;
		static size_t GetSizeInBytes(::PermebufBase* Buffer);
		size_t Address() const { return offset_; }
`;

		// Make sure there are no duplicate field names or numbers.
		const usedFieldNames = {"Unknown": true};
		const usedFieldNumbers = {0: true};

		const fieldNames = Object.keys(thisOneOf.fields);
		for (let i = 0; i < fieldNames.length; i++) {
			const field = thisOneOf.fields[fieldNames[i]];

			// Make sure there are no duplicate field numbers.
			if (usedFieldNumbers[field.number]) {
				console.log('Field ' + field.name + ' has a field number of ' + field.number +
					' but oneof ' + thisOneOf.fullName + ' already has a field with that number.');
				return false;
			}
			usedFieldNumbers[field.number] = true;

			if (field.name == 'reserve') {
				// Skip reserved fields, but remember that the ID number was used.
				continue;
			}


			if (usedFieldNames[field.name]) {
				console.log('Field ' + field.name + ' with field number of ' + field.number +
					' in oneof ' + thisOneOf.fullName + ' already has a field with that name.');
				return false;
			}
			usedFieldNames[field.name] = true;

			const typeInformation = getTypeInformation(thisOneOf, field);
			if (!typeInformation) {
				return false;
			}

			enumHeaderCpp += `,
	${field.name} = ${field.number}`;


			switch (typeInformation.type) {
				case FieldType.ARRAY:
				case FieldType.LIST:
					const cppType = convertArrayOrListToCppTypeSuffix(typeInformation);
					if (!cppType) {
						console.log('Something happened parsing type ' + field.type + ' for field ' + field.name + '/' +
							field.number + ' in message ' + thisOneOf.fullName + '.');
						return false;
					}
					const fullCppType = '::Permebuf' + cppType;
					headerCpp += `
	const ${fullCppType} Get${field.name}() const;
	void Set${field.name}(${fullCppType} value);
	${fullCppType} Mutable${field.name}();
	bool Has${field.name}() const;`;

					sourceCpp += `
const ${fullCppType} ${thisOneOf.cppClassName}::Get${field.name}() const {
	if (GetOption() != ${thisOneOf.cppClassName}::Options::${field.name})
		return ${fullCppType}(buffer_, 0);
	return ${fullCppType}(buffer_, ReadOffset());
}

void ${thisOneOf.cppClassName}::Set${field.name}(${fullCppType} value) {
	SetOption(${thisOneOf.cppClassName}::Options::${field.name}, value.Address());
}

${fullCppType} ${thisOneOf.cppClassName}::Mutable${field.name}() {
	if (buffer_ == 0 || offset_ == 0)
		return ${fullCppType}(buffer_, 0);

	if (GetOption() == ${thisOneOf.cppClassName}::Options::${field.name})
		return ${fullCppType}(buffer_, ReadOffset());

	auto object = buffer_->Allocate${cppType}();
	SetOption(${thisOneOf.cppClassName}::Options::${field.name}, object.Address());
	return object;
}

bool ${thisOneOf.cppClassName}::Has${field.name}() const {
	return GetOption() == ${thisOneOf.cppClassName}::Options::${field.name};
}
`;
				case FieldType.MESSAGE:
					headerCpp += `
		const ${typeInformation.cppClassName} Get${field.name}() const;
		void Set${field.name}(${typeInformation.cppClassName} value);
		${typeInformation.cppClassName} Mutable${field.name}();
		bool Has${field.name}() const;
`;

					sourceCpp += `
const ${typeInformation.cppClassName} ${thisOneOf.cppClassName}::Get${field.name}() const {
	if (GetOption() != ${thisOneOf.cppClassName}::Options::${field.name})
		return ${typeInformation.cppClassName}(buffer_, 0);
	return ${typeInformation.cppClassName}(buffer_, ReadOffset());
}

void ${thisOneOf.cppClassName}::Set${field.name}(${typeInformation.cppClassName} value) {
	SetOption(${thisOneOf.cppClassName}::Options::${field.name}, value.Address());
}

${typeInformation.cppClassName} ${thisOneOf.cppClassName}::Mutable${field.name}() {
	if (buffer_ == 0 || offset_ == 0)
		return ${typeInformation.cppClassName}(buffer_, 0);

	if (GetOption() == ${thisOneOf.cppClassName}::Options::${field.name})
		return ${typeInformation.cppClassName}(buffer_, ReadOffset());

	auto object = buffer_->AllocateMessage<${typeInformation.cppClassName}>();
	SetOption(${thisOneOf.cppClassName}::Options::${field.name}, object.Address());
	return object;
}

bool ${thisOneOf.cppClassName}::Has${field.name}() const {
	return GetOption() == ${thisOneOf.cppClassName}::Options::${field.name};
}
`;
					break;
				case FieldType.STRING:
					headerCpp += `
		::PermebufString Get${field.name}() const;
		void Set${field.name}(::PermebufString& value);
		void Set${field.name}(std::string_view value);
		bool Has${field.name}() const;
		void Clear${field.name}();
`;

					sourceCpp += `
PermebufString ${thisOneOf.cppClassName}::Get${field.name}() const {
	if (GetOption() != ${thisOneOf.cppClassName}::Options::${field.name})
		return PermebufString(buffer_, 0);
	return PermebufString(buffer_, GetOffset());
}

void ${thisOneOf.cppClassName}::Set${field.name}(PermebufString value) {
	SetOption(${thisOneOf.cppClassName}::Options::${field.name}, value.Address());
}

void ${thisOneOf.cppClassName}::Set${field.name}(std::string_view value) {
	if (buffer_ == 0 || offset_ == 0)
		return;
	SetOption(${thisOneOf.cppClassName}::Options::${field.name},
		buffer_->AllocateString(value).Address());
}

bool ${thisOneOf.cppClassName}::Has${field.name}() const {
	return GetOption() == ${thisOneOf.cppClassName}::Options::${field.name};
}
`;
				case FieldType.BYTES:
					if (typeInformation.sizeInPointers == 0) {
						console.log('Bytes with fixed sizes aren\'t supported in messages. ' +
						field.type + ' is being used for field ' + field.name + '/' + field.number +
						' in message ' + thisOneOf.fullName + '.');
						return false;
					}

					headerCpp += `
		::PermebufBytes Get${field.name}() const;
		void Set${field.name}(::PermebufBytes& value);
		void Set${field.name}(const void* ptr, size_t size);
		bool Has${field.name}() const;
		void Clear${field.name}();
`;

					sourceCpp += `
PermebufBytes ${thisOneOf.cppClassName}::Get${field.name}() const {
	if (GetOption() != ${thisOneOf.cppClassName}::Options::${field.name})
		return PermebufBytes(buffer_, 0);
	return PermebufBytes(buffer_, GetOffset());
}

void ${thisOneOf.cppClassName}::Set${field.name}(PermebufBytes value) {
	SetOption(${thisOneOf.cppClassName}::Options::${field.name}, value.Address());
}

void ${thisOneOf.cppClassName}::Set${field.name}(const void* ptr, size_t size) {
	if (buffer_ == 0 || offset_ == 0)
		return;
	SetOption(${thisOneOf.cppClassName}::Options::${field.name},
		buffer_->AllocateBytes(ptr, size).Address());
}

bool ${thisOneOf.cppClassName}::Has${field.name}() const {
	return GetOption() == ${thisOneOf.cppClassName}::Options::${field.name};
}
`;
					break;
				default:
					console.log('Field ' + field.name + ' with field number of ' + field.number +
						' in oneof ' + thisOneOf.fullName +
						' is not of type list, array, string, bytes, or message.');
					return false;
			}
		}

		enumHeaderCpp += `
};
`;

		if (thisOneOf.children.length > 0) {
			headerCpp += '\n';
			for (let i = 0; i < thisOneOf.children.length; i++) {
				const child = thisOneOf.children[i];
				headerCpp += '\t\ttypedef ' + child.cppClassName + ' ' + child.name + ';\n';
			}
		}

		headerCpp += `
		bool IsValid() const;
		Options GetOption() const;
		void Clear();

	private:
		void SetOption(Options, size_t offset);
		size_t ReadOffset() const;
		::PermebufBase* buffer_;
		size_t offset_;
};

${enumHeaderCpp}
`;

		sourceCpp += `

${thisOneOf.cppClassName}::${thisOneOf.cppClassName}(::PermebufBase* buffer, size_t offset) :
	buffer_(buffer), offset_(offset) {}

size_t ${thisOneOf.cppClassName}::GetSizeInBytes(::PermebufBase* buffer) {
	return 2 + buffer->GetAddressSizeInBytes();
}

bool ${thisOneOf.cppClassName}::IsValid() const {
	return ReadOffset() != 0;
}

${thisOneOf.cppClassName}::Options ${thisOneOf.cppClassName}::GetOption() const {
	if (buffer_ == 0 || offset_ == 0)
		return ${thisOneOf.cppClassName}::Options::Unknown;

	return static_cast<${thisOneOf.cppClassName}::Options>(
		buffer_->Read2Bytes(offset_));
}

void ${thisOneOf.cppClassName}::Clear() {
	SetOption(${thisOneOf.cppClassName}::Options::Unknown, 0);
}

void ${thisOneOf.cppClassName}::SetOption(
	${thisOneOf.cppClassName}::Options option,
	size_t offset) {
	if (buffer_ == 0 || offset_ == 0)
		return;
	buffer_->Write2Bytes(offset_, static_cast<uint16>(option));
	buffer_->WritePointer(offset_ + 2, offset);
}

size_t ${thisOneOf.cppClassName}::ReadOffset() const {
	if (buffer_ == 0 || offset_ == 0)
		return 0;

	return buffer_->ReadPointer(offset_ + 2);
}

`;
		return true;
	}

	function generateService(thisService) {
		headerCpp += `
class ${thisService.cppClassName} : public PermebufService {
	public:
		${thisService.cppClassName}();
		${thisService.cppClassName}(::perception::ProcessId process_id, ::perception::MessageId message_id);
		${thisService.cppClassName}(const ${thisService.cppClassName}& other);
		${thisService.cppClassName}(const ${thisService.cppClassName}_Server& other);

		static ${thisService.cppClassName} Get();
		static std::optional<${thisService.cppClassName}> FindFirstInstance();

		static void ForEachInstance(const std::function<void(${thisService.cppClassName})>& on_each_instance);

		static ::perception::MessageId NotifyOnEachNewInstance(const std::function<void(${thisService.cppClassName})>& on_each_instance);
		static void StopNotifyingOnEachNewInstance(::perception::MessageId message_id);

		::perception::MessageId NotifyOnDisappearance(const std::function<void()>& on_disappearance);
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
	::PermebufService(other.GetProcessId(), other.GetMessageId()) {}

std::optional<${thisService.cppClassName}> ${thisService.cppClassName}::FindFirstInstance() {
	::perception::ProcessId process_id;
	::perception::MessageId message_id;
	if (::perception::FindFirstInstanceOfService(${thisService.cppClassName}_Server::Name(),
		process_id, message_id)) {
		return std::optional<${thisService.cppClassName}>{
			${thisService.cppClassName}(process_id, message_id)};
	} else {
		return std::nullopt;
	}
}

${thisService.cppClassName} ${thisService.cppClassName}::singleton_;

${thisService.cppClassName} ${thisService.cppClassName}::Get() {
	// TODO: Mutex.
	if (singleton_.IsValid())
		return singleton_;

	auto main_fiber = ::perception::GetCurrentlyExecutingFiber();
	NotifyOnEachNewInstance([main_fiber] (${thisService.cppClassName} instance) {
		singleton_ = instance;
		main_fiber->WakeUp();
	});
	::perception::Sleep();
	return singleton_;
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
	::perception::StopNotifyingOnEachNewServiceInstance(message_id);
}

::perception::MessageId ${thisService.cppClassName}::NotifyOnDisappearance(
	const std::function<void()>& on_disappearance) {
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
			const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response) const;
`;
						sourceCpp += `
StatusOr<${responseType.cppClassName}> ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request) const {
		return SendMiniMessageAndWaitForMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, request);
}
void ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request,
	const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response) const {
		return SendMiniMessageAndNotifyOnMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, request, on_response);
}
`;
serverDelegator += `return ProcessMiniMessageForMiniMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					const ${requestType.cppClassName}& request) {
						return Handle${field.name}(sender, request);
					});
`;

						serverHeaderCpp += `
		virtual StatusOr<${responseType.cppClassName}> Handle${field.name}(
			::perception::ProcessId sender,
			const ${requestType.cppClassName}& request);
`;
						serverSourceCpp += `
		StatusOr<${responseType.cppClassName}> ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request) {
				return ::perception::Status::UNIMPLEMENTED;
			}
`;
					} else if (responseType.type == FieldType.MESSAGE) {
						headerCpp +=
`		StatusOr<Permebuf<${responseType.cppClassName}>> Call${field.name}(
			const ${requestType.cppClassName}& request) const;
		void Call${field.name}(const ${requestType.cppClassName}& request,
			const std::function<void(StatusOr<Permebuf<${responseType.cppClassName}>>)>& on_response) const;
`;
						sourceCpp += `
StatusOr<Permebuf<${responseType.cppClassName}>> ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request) const {
		return SendMiniMessageAndWaitForMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, request);
}
void ${thisService.cppClassName}::Call${field.name}(
	const ${requestType.cppClassName}& request,
	const std::function<void(StatusOr<Permebuf<${responseType.cppClassName}>>)>& on_response) const {
		return SendMiniMessageAndNotifyOnMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, request, on_response);
}
`;
serverDelegator += `return ProcessMiniMessageForMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					const ${requestType.cppClassName}& request) {
						return Handle${field.name}(sender, request);
					});
`;
						serverHeaderCpp += `
		virtual StatusOr<Permebuf<${responseType.cppClassName}>> Handle${field.name}(
			::perception::ProcessId sender,
			const ${requestType.cppClassName}& request);
`;
						serverSourceCpp += `
		StatusOr<Permebuf<${responseType.cppClassName}>> ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			const ${requestType.cppClassName}& request) {
				return ::perception::Status::UNIMPLEMENTED;
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
`		::perception::Status Send${field.name}(Permebuf<${requestType.cppClassName}> request) const;
`;
						sourceCpp += `
::perception::Status ${thisService.cppClassName}::Send${field.name}(
	Permebuf<${requestType.cppClassName}> request) const {
		return SendMessage<${requestType.cppClassName}>(${field.number}, std::move(request));
}
`;
serverDelegator += `return ProcessMessage<${requestType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					Permebuf<${requestType.cppClassName}> request) {
						Handle${field.name}(sender, std::move(request));
					});
`;

						serverHeaderCpp += `
		virtual void Handle${field.name}(::perception::ProcessId sender,
			Permebuf<${requestType.cppClassName}> request);
`;
						serverSourceCpp += `
		void ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			Permebuf<${requestType.cppClassName}> request) {}
`;
					} else if (responseType.type == FieldType.MINIMESSAGE) {
						headerCpp +=
`		StatusOr<${responseType.cppClassName}> Call${field.name}(
			Permebuf<${requestType.cppClassName}> request) const;
		void Call${field.name}(Permebuf<${requestType.cppClassName}> request,
			const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response) const;
`;
						sourceCpp += `
StatusOr<${responseType.cppClassName}> ${thisService.cppClassName}::Call${field.name}(
	Permebuf<${requestType.cppClassName}> request) const {
		return SendMessageAndWaitForMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, std::move(request));
}
void ${thisService.cppClassName}::Call${field.name}(
	Permebuf<${requestType.cppClassName}> request,
	const std::function<void(StatusOr<${responseType.cppClassName}>)>& on_response) const {
		return SendMessageAndNotifyOnMiniMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, std::move(request), on_response);
}
`;
serverDelegator += `return ProcessMessageForMiniMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					Permebuf<${requestType.cppClassName}> request) {
						return Handle${field.name}(sender, std::move(request));
					});
`;
						serverHeaderCpp += `
		virtual StatusOr<${responseType.cppClassName}> Handle${field.name}(
			::perception::ProcessId sender,
			Permebuf<${requestType.cppClassName}> request);
`;
						serverSourceCpp += `
		StatusOr<${responseType.cppClassName}>  ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			Permebuf<${requestType.cppClassName}> request) {
				return ::perception::Status::UNIMPLEMENTED;
			}
`;
					} else if (responseType.type == FieldType.MESSAGE) {
						headerCpp +=
`		StatusOr<Permebuf<${responseType.cppClassName}>> Call${field.name}(
			Permebuf<${requestType.cppClassName}> request) const;
		void Call${field.name}(Permebuf<${requestType.cppClassName}> request,
			const std::function<void(StatusOr<Permebuf<${responseType.cppClassName}>>)>& on_response) const;
`;
						sourceCpp += `
StatusOr<Permebuf<${responseType.cppClassName}>> ${thisService.cppClassName}::Call${field.name}(
	Permebuf<${requestType.cppClassName}> request) const {
		return SendMessageAndWaitForMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, std::move(request));
}
void ${thisService.cppClassName}::Call${field.name}(
	Permebuf<${requestType.cppClassName}> request,
	const std::function<void(StatusOr<Permebuf<${responseType.cppClassName}>>)>& on_response) const {
		return SendMessageAndNotifyOnMessage<
			${requestType.cppClassName},${responseType.cppClassName}
			>(${field.number}, std::move(request), on_response);
}
`;
serverDelegator += `return ProcessMessageForMessage<${requestType.cppClassName}, ${responseType.cppClassName}>(
				sender, metadata, param_1, param_2, param_3, param_4, param_5,
				[this](::perception::ProcessId sender,
					Permebuf<${requestType.cppClassName}> request) {
						return Handle${field.name}(sender, std::move(request));
					});
`;
						serverHeaderCpp += `
		virtual StatusOr<Permebuf<${responseType.cppClassName}>> Handle${field.name}(
			::perception::ProcessId sender,
			Permebuf<${requestType.cppClassName}> request);
`;
						serverSourceCpp += `
		StatusOr<Permebuf<${responseType.cppClassName}>> ${thisService.cppClassName}_Server::Handle${field.name}(::perception::ProcessId sender,
			Permebuf<${requestType.cppClassName}> request) {
				return ::perception::Status::UNIMPLEMENTED;
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
private:
		static ${thisService.cppClassName} singleton_;
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

	headerCpp += '\nnamespace permebuf {\n';
	let namespaceLevels = 0;
	let namespaceParts = [];

	function switchToNamespace(namespace, sourceToo) {
		const newNamespaceParts = namespace.split('.');
		// Find the lowest common part.
		let lowestCommonPartIndex = 0;
		while (lowestCommonPartIndex < namespaceParts.length &&
			lowestCommonPartIndex < newNamespaceParts.length &&
			namespaceParts[lowestCommonPartIndex] ==
			newNamespaceParts[lowestCommonPartIndex]) {
			lowestCommonPartIndex++;
		}

		// Remove everything after the lowest common part.
		while (namespaceLevels > lowestCommonPartIndex) {
			namespaceLevels--;
			headerCpp += '\n}';
			if (sourceToo)
				sourceCpp += '\n}';
		}
		namespaceParts.splice(lowestCommonPartIndex);

		// Enter into the new namespace.
		let namespacePartIdx = lowestCommonPartIndex;
		while (namespacePartIdx < newNamespaceParts.length) {
			const namespacePart = newNamespaceParts[namespacePartIdx];
			namespacePartIdx++;

			if (namespacePart.length == 0) {
				// Skip empty namespace parts such as trailing '.'s.
				continue;
			}

			headerCpp += '\nnamespace ' + namespacePart + ' {\n'
			if (sourceToo)
				sourceCpp += '\nnamespace ' + namespacePart + ' {\n'

			namespaceParts.push(namespacePart);
			namespaceLevels++;
		}
	}

	// Generate forward declarations of everything we reference.
	// Sort the symbols to group things by similar namespaces.
	Object.keys(symbolTable).sort().forEach((symbolToForwardDeclare) => {
		const symbol = symbolTable[symbolToForwardDeclare];
		switchToNamespace(symbol.namespace, /*sourceToo=*/false);

		switch (symbol.classType) {
			case ClassType.ENUM:
				headerCpp += `
enum class ${symbol.cppClassName} : unsigned short;`;
				break;
			case ClassType.MESSAGE:
				headerCpp += `
class ${symbol.cppClassName};`;
				break;
			case ClassType.MINIMESSAGE:
				headerCpp += `
class ${symbol.cppClassName};`;
				break;
			case ClassType.ONEOF:
				headerCpp += `
enum class ${symbol.cppClassName}_Options : unsigned short;
class ${symbol.cppClassName};`;
				break;
			case ClassType.SERVICE:
				headerCpp += `
class ${symbol.cppClassName};
class ${symbol.cppClassName}_Server;`;
				break;
			default:
				console.log('Unknown symbol to forward declare:');
				console.log(symbol);
				return false;
		}
	});


	// Enter the source into the same namespace as the header.
	sourceCpp += '\nnamespace permebuf {\n'
	namespaceParts.forEach((namespacePart) => {
		sourceCpp += '\nnamespace ' + namespacePart + ' {\n'
	});

	// Generate the code.
	// Sort the symbols to group things by similar namespaces.
	symbolsToGenerate = symbolsToGenerate.sort();
	for (let i = 0; i < symbolsToGenerate.length; i++) {
		const symbolToGenerate = symbolTable[symbolsToGenerate[i]];
		switchToNamespace(symbolToGenerate.namespace, /*sourceToo=*/true);

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
				if (!generateOneOf(symbolToGenerate)) {
					return false;
				}
				break;
			case ClassType.SERVICE:
				if (!generateService(symbolToGenerate)) {
					return false;
				}
				break;
			default:
				console.log('Unknown symbol to generate:');
				console.log(symbolToGenerate);
				return false;
		}
	}

	for (let i = 0; i < namespaceLevels; i++) {
		headerCpp += '\n}';
		sourceCpp += '\n}';
	}
	// Parent permebuf namespace.
	headerCpp += '\n}';
	sourceCpp += '\n}';

	// Terminate the files with a new line.
	headerCpp += '\n';
	sourceCpp += '\n';

	const packageRootDirectory = rootDirectory + getPackageTypeDirectoryName(packageType) + '/' + packageName + '/'; 

	writeFile(headerCpp, packageRootDirectory + 'generated/include/' + generatedFilePath + '.h');
	writeFile(sourceCpp, packageRootDirectory + 'generated/source/' + generatedFilePath + '.cc');

	return true;
}

module.exports = {
	generateCppSources: generateCppSources
};
