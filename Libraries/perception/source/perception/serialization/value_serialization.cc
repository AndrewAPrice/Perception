// Copyright 2026 Google LLC
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

#include "perception/serialization/value_serialization.h"

namespace perception {
namespace serialization {
namespace {

struct Parser {
  std::string_view str;
  size_t index = 0;

  void SkipWhitespace() {
    while (index < str.size() && (str[index] == ' ' || str[index] == '\t' ||
                                  str[index] == '\n' || str[index] == '\r')) {
      index++;
    }
  }

  StatusOr<Value> ParseValue() {
    SkipWhitespace();
    if (index >= str.size()) return Status::INVALID_ARGUMENT;

    char c = str[index];
    switch (c) {
      case '{': {
        index++;  // consume '{'
        int r = -1, g = -1, b = -1;
        bool done = false;
        while (!done) {
          SkipWhitespace();
          if (index >= str.size()) return Status::INVALID_ARGUMENT;
          if (str[index] == '}') {
            index++;
            break;
          }
          if (str[index] != '"') return Status::INVALID_ARGUMENT;
          index++;  // consume '"'
          size_t key_start = index;
          while (index < str.size() && str[index] != '"') index++;
          if (index >= str.size()) return Status::INVALID_ARGUMENT;
          std::string_view key = str.substr(key_start, index - key_start);
          index++;  // consume '"'
          SkipWhitespace();
          if (index >= str.size() || str[index] != ':') return Status::INVALID_ARGUMENT;
          index++;  // consume ':'
          SkipWhitespace();
          ASSIGN_OR_RETURN(auto val, ParseValue());
          if (key == "r") {
            r = static_cast<int>(val.IntegerValue().value_or(-1));
          } else if (key == "g") {
            g = static_cast<int>(val.IntegerValue().value_or(-1));
          } else if (key == "b") {
            b = static_cast<int>(val.IntegerValue().value_or(-1));
          }
          SkipWhitespace();
          if (index >= str.size()) return Status::INVALID_ARGUMENT;
          if (str[index] == ',') {
            index++;
          } else if (str[index] == '}') {
            index++;
            done = true;
          } else {
            return Status::INVALID_ARGUMENT;
          }
        }
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
          return Status::INVALID_ARGUMENT;
        }
        return Value(static_cast<uint32>((0xFF << 24) | (r << 16) | (g << 8) | b));
      }
      case 'n': {
        if (index + 4 <= str.size() && str.substr(index, 4) == "null") {
          index += 4;
          return Value();
        }
        return Status::INVALID_ARGUMENT;
      }
      case 't': {
        if (index + 4 <= str.size() && str.substr(index, 4) == "true") {
          index += 4;
          return Value(true);
        }
        return Status::INVALID_ARGUMENT;
      }
      case 'f': {
        if (index + 5 <= str.size() && str.substr(index, 5) == "false") {
          index += 5;
          return Value(false);
        }
        return Status::INVALID_ARGUMENT;
      }
      case '"': {
        index++;  // consume '"'
        std::string s;
        while (index < str.size() && str[index] != '"') {
          char next_c = str[index++];
          if (next_c == '\\') {
            if (index >= str.size()) return Status::INVALID_ARGUMENT;
            switch (str[index++]) {
              case 'n':
                s += '\n';
                break;
              case 't':
                s += '\t';
                break;
              case 'r':
                s += '\r';
                break;
              case '"':
                s += '"';
                break;
              case '\\':
                s += '\\';
                break;
              default:
                return Status::INVALID_ARGUMENT;
            }
          } else {
            s += next_c;
          }
        }
        if (index >= str.size()) return Status::INVALID_ARGUMENT;
        index++;  // consume '"'
        return Value(s);
      }
      case '[': {
        index++;  // consume '['
        std::vector<Value> arr;
        SkipWhitespace();
        if (index < str.size() && str[index] == ']') {
          index++;  // consume ']'
          return Value(arr);
        }

        bool done = false;
        while (!done) {
          ASSIGN_OR_RETURN(auto item, ParseValue());
          arr.push_back(std::move(item));

          SkipWhitespace();
          if (index >= str.size()) return Status::INVALID_ARGUMENT;
          switch (str[index]) {
            case ',':
              index++;  // consume ','
              SkipWhitespace();
              if (index < str.size() && str[index] == ']') {
                index++;  // consume ']'
                done = true;
              }
              break;
            case ']':
              index++;  // consume ']'
              done = true;
              break;
            default:
              return Status::INVALID_ARGUMENT;
          }
        }
        return Value(arr);
      }
      case '-':
      case '+':
      case '.':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': {
        size_t start = index;
        bool has_decimal = false;
        if (str[index] == '-' || str[index] == '+') {
          index++;
        }
        while (
            index < str.size() &&
            ((str[index] >= '0' && str[index] <= '9') || str[index] == '.')) {
          if (str[index] == '.') {
            if (has_decimal) return Status::INVALID_ARGUMENT;
            has_decimal = true;
          }
          index++;
        }
        std::string_view num_str = str.substr(start, index - start);
        if (num_str.empty() || num_str == "-" || num_str == "+" ||
            num_str == ".") {
          return Status::INVALID_ARGUMENT;
        }

        if (has_decimal) {
          try {
            std::string copy(num_str);
            size_t processed = 0;
            double val = std::stod(copy, &processed);
            if (processed != copy.size()) return Status::INVALID_ARGUMENT;
            return Value(val);
          } catch (...) {
            return Status::INVALID_ARGUMENT;
          }
        } else {
          try {
            std::string copy(num_str);
            size_t processed = 0;
            long long val = std::stoll(copy, &processed);
            if (processed != copy.size()) return Status::INVALID_ARGUMENT;
            return Value(static_cast<int64>(val));
          } catch (...) {
            return Status::INVALID_ARGUMENT;
          }
        }
      }
      default:
        return Status::INVALID_ARGUMENT;
    }
  }
};

}  // namespace

StatusOr<Value> StringToValue(std::string_view str) {
  Parser parser{str, 0};
  ASSIGN_OR_RETURN(auto val, parser.ParseValue());
  parser.SkipWhitespace();
  if (parser.index < str.size()) return Status::INVALID_ARGUMENT;
  return val;
}

std::string ValueToString(const Value& value) {
  switch (value.GetType()) {
    case Value::Type::UNDEFINED:
      return "null";
    case Value::Type::BOOLEAN:
      return value.BoolValue().value_or(false) ? "true" : "false";
    case Value::Type::INTEGER:
      return std::to_string(value.IntegerValue().value_or(0));
    case Value::Type::FLOAT: {
      std::string s = std::to_string(value.FloatValue().value_or(0.0));
      if (s.find('.') != std::string::npos) {
        while (s.back() == '0') {
          s.pop_back();
        }
        if (s.back() == '.') {
          s.push_back('0');
        }
      }
      return s;
    }
    case Value::Type::STRING: {
      std::string s = "\"";
      for (char c : value.StringValue().value_or("")) {
        if (c == '\n') {
          s += "\\n";
        } else if (c == '\t') {
          s += "\\t";
        } else if (c == '\r') {
          s += "\\r";
        } else if (c == '"') {
          s += "\\\"";
        } else if (c == '\\') {
          s += "\\\\";
        } else {
          s += c;
        }
      }
      s += "\"";
      return s;
    }
    case Value::Type::COLOR_RGB: {
      auto color = value.ColorRGBValue().value_or(0);
      uint8 r = (color >> 16) & 0xFF;
      uint8 g = (color >> 8) & 0xFF;
      uint8 b = color & 0xFF;
      return "{\"r\":" + std::to_string(r) +
             ",\"g\":" + std::to_string(g) +
             ",\"b\":" + std::to_string(b) + "}";
    }
    case Value::Type::ARRAY: {
      const auto* arr = value.ArrayValue();
      if (!arr) return "null";
      std::string s = "[";
      for (size_t i = 0; i < arr->size(); ++i) {
        if (i > 0) {
          s += ", ";
        }
        s += ValueToString((*arr)[i]);
      }
      s += "]";
      return s;
    }
  }
  return "null";
}

}  // namespace serialization
}  // namespace perception
