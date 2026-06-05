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

#include "perception/serialization/text_deserializer.h"

#include <types.h>

#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"

namespace perception {
namespace serialization {

namespace {

struct TextNode {
  enum Type { INTEGER, DOUBLE, STRING, OBJECT, ARRAY } type;
  int64 integer_value = 0;
  double double_value = 0.0;
  std::string string_value;
  std::unordered_map<std::string, std::shared_ptr<TextNode>> object_value;
  std::vector<std::shared_ptr<TextNode>> array_value;
};

class TextParser {
 public:
  TextParser(std::string_view text) : text_(text), offset_(0) {}

  std::shared_ptr<TextNode> Parse() {
    SkipWhitespace();
    if (offset_ >= text_.size()) return nullptr;
    if (text_[offset_] == '{') {
      return ParseObject();
    }
    return nullptr;
  }

 private:
  void SkipWhitespace() {
    while (offset_ < text_.size()) {
      char c = text_[offset_];
      if (std::isspace(c) || c == ',') {
        offset_++;
      } else if (c == '#' || (c == '/' && offset_ + 1 < text_.size() &&
                              text_[offset_ + 1] == '/')) {
        // Line comment
        while (offset_ < text_.size() && text_[offset_] != '\n') {
          offset_++;
        }
      } else {
        break;
      }
    }
  }

  std::shared_ptr<TextNode> ParseObject() {
    if (offset_ >= text_.size() || text_[offset_] != '{') return nullptr;
    offset_++;  // skip '{'

    auto node = std::make_shared<TextNode>();
    node->type = TextNode::OBJECT;

    while (true) {
      SkipWhitespace();
      if (offset_ >= text_.size()) break;
      if (text_[offset_] == '}') {
        offset_++;
        break;
      }

      // Read key: everything up to the next ':'
      size_t key_start = offset_;
      while (offset_ < text_.size() && text_[offset_] != ':') {
        offset_++;
      }
      if (offset_ >= text_.size()) return nullptr;  // Malformed

      // Trim trailing spaces from the key
      size_t key_end = offset_;
      while (key_end > key_start && std::isspace(text_[key_end - 1])) {
        key_end--;
      }
      std::string key(text_.substr(key_start, key_end - key_start));
      if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
        key = key.substr(1, key.size() - 2);
      }
      offset_++;  // skip ':'

      auto val = ParseValue();
      if (!val) return nullptr;

      node->object_value[key] = val;
    }
    return node;
  }

  std::shared_ptr<TextNode> ParseValue() {
    SkipWhitespace();
    if (offset_ >= text_.size()) return nullptr;

    if (offset_ + 4 <= text_.size() && text_.substr(offset_, 4) == "true") {
      offset_ += 4;
      auto node = std::make_shared<TextNode>();
      node->type = TextNode::INTEGER;
      node->integer_value = 1;
      return node;
    }
    if (offset_ + 5 <= text_.size() && text_.substr(offset_, 5) == "false") {
      offset_ += 5;
      auto node = std::make_shared<TextNode>();
      node->type = TextNode::INTEGER;
      node->integer_value = 0;
      return node;
    }

    char c = text_[offset_];
    if (c == '"') {
      return ParseString();
    } else if (c == '{') {
      return ParseObject();
    } else if (c == '[') {
      return ParseArray();
    } else {
      return ParseNumber();
    }
  }

  std::shared_ptr<TextNode> ParseString() {
    offset_++;  // skip opening '"'
    size_t start = offset_;
    while (offset_ < text_.size() && text_[offset_] != '"') {
      if (text_[offset_] == '\\' && offset_ + 1 < text_.size()) {
        offset_ += 2;
      } else {
        offset_++;
      }
    }
    std::string val(text_.substr(start, offset_ - start));
    if (offset_ < text_.size() && text_[offset_] == '"') {
      offset_++;
    }
    // Handle escape characters in the parsed string
    std::string unescaped = "";
    for (size_t i = 0; i < val.size(); ++i) {
      if (val[i] == '\\' && i + 1 < val.size()) {
        char next = val[i + 1];
        if (next == 'n')
          unescaped += '\n';
        else if (next == 'r')
          unescaped += '\r';
        else if (next == 't')
          unescaped += '\t';
        else
          unescaped += next;
        i++;
      } else {
        unescaped += val[i];
      }
    }

    auto node = std::make_shared<TextNode>();
    node->type = TextNode::STRING;
    node->string_value = unescaped;
    return node;
  }

  std::shared_ptr<TextNode> ParseArray() {
    offset_++;  // skip '['
    auto node = std::make_shared<TextNode>();
    node->type = TextNode::ARRAY;

    while (true) {
      SkipWhitespace();
      if (offset_ >= text_.size()) break;
      if (text_[offset_] == ']') {
        offset_++;
        break;
      }
      auto val = ParseValue();
      if (!val) return nullptr;
      node->array_value.push_back(val);
    }
    return node;
  }

  std::shared_ptr<TextNode> ParseNumber() {
    size_t start = offset_;
    bool is_double = false;

    // Check sign
    if (offset_ < text_.size() &&
        (text_[offset_] == '-' || text_[offset_] == '+')) {
      offset_++;
    }

    // Read digits, decimal point, or exponents
    while (offset_ < text_.size() &&
           (std::isdigit(text_[offset_]) || text_[offset_] == '.' ||
            text_[offset_] == 'e' || text_[offset_] == 'E')) {
      if (text_[offset_] == '.') is_double = true;
      offset_++;
    }

    std::string val(text_.substr(start, offset_ - start));
    auto node = std::make_shared<TextNode>();

    if (is_double) {
      node->type = TextNode::DOUBLE;
      node->double_value = std::stod(val);
    } else {
      node->type = TextNode::INTEGER;
      node->integer_value = std::stoll(val);
    }
    return node;
  }

  std::string_view text_;
  size_t offset_;
};

class TextDeserializer : public Serializer {
 public:
  TextDeserializer(std::shared_ptr<TextNode> node)
      : node_(node), current_field_index_(0) {}

  virtual bool HasThisField(std::string_view name = "") override {
    if (!node_ || node_->type != TextNode::OBJECT) return false;
    return node_->object_value.find(std::string(name)) !=
           node_->object_value.end();
  }

  virtual bool IsDeserializing() override { return true; }

  virtual void Integer() override { current_field_index_++; }

  virtual void Float() override { current_field_index_++; }

  virtual void Float(std::string_view name, float& value) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::DOUBLE) {
        value = static_cast<float>(child->double_value);
      } else if (child->type == TextNode::INTEGER) {
        value = static_cast<float>(child->integer_value);
      }
    } else {
      value = 0.0f;
    }
    current_field_index_++;
  }

  virtual void Double() override { current_field_index_++; }

  virtual void Double(std::string_view name, double& value) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::DOUBLE) {
        value = child->double_value;
      } else if (child->type == TextNode::INTEGER) {
        value = static_cast<double>(child->integer_value);
      }
    } else {
      value = 0.0;
    }
    current_field_index_++;
  }

  virtual void String() override { current_field_index_++; }

  virtual void String(std::string_view name, std::string& str) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::STRING) {
        str = child->string_value;
      }
    } else {
      str.clear();
    }
    current_field_index_++;
  }

  virtual void Serializable() override { current_field_index_++; }

  virtual void Serializable(std::string_view name,
                            class Serializable& obj) override {
    if (auto child = GetField(name)) {
      TextDeserializer child_serializer(child);
      obj.Serialize(child_serializer);
    } else {
      TextDeserializer child_serializer(nullptr);
      obj.Serialize(child_serializer);
    }
    current_field_index_++;
  }

  virtual void ArrayOfSerializables() override { current_field_index_++; }

  virtual void ArrayOfSerializables(
      std::string_view name, int current_size,
      const std::function<
          void(const std::function<void(class Serializable& serializable)>&
                   serialize_entry)>& serialization_function,
      const std::function<
          void(int elements,
               const std::function<void(class Serializable& serializable)>&
                   deserialize_entry)>& deserialization_function) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::ARRAY) {
        int elements = child->array_value.size();
        int current_index = 0;
        deserialization_function(
            elements,
            [child, &current_index](class Serializable& serializable) {
              if (current_index < child->array_value.size()) {
                auto item_node = child->array_value[current_index++];
                TextDeserializer child_serializer(item_node);
                serializable.Serialize(child_serializer);
              }
            });
        current_field_index_++;
        return;
      }
    }
    deserialization_function(0, [](class Serializable& serializable) {});
    current_field_index_++;
  }

  virtual void ArrayOfStrings() override { current_field_index_++; }

  virtual void ArrayOfStrings(std::string_view name,
                              std::vector<std::string>& arr) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::ARRAY) {
        int elements = child->array_value.size();
        arr.resize(elements);
        for (int i = 0; i < elements; i++) {
          auto item_node = child->array_value[i];
          if (item_node && item_node->type == TextNode::STRING) {
            arr[i] = item_node->string_value;
          } else {
            arr[i].clear();
          }
        }
        current_field_index_++;
        return;
      }
    }
    arr.clear();
    current_field_index_++;
  }

 private:
  std::shared_ptr<TextNode> GetField(std::string_view name) {
    if (!node_ || node_->type != TextNode::OBJECT) return nullptr;
    auto it = node_->object_value.find(std::string(name));
    if (it != node_->object_value.end()) {
      return it->second;
    }
    return nullptr;
  }

  virtual void UnsignedInteger(std::string_view name, uint64& value) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::INTEGER) {
        value = static_cast<uint64>(child->integer_value);
      }
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  virtual void SignedInteger(std::string_view name, int64& value) override {
    if (auto child = GetField(name)) {
      if (child->type == TextNode::INTEGER) {
        value = child->integer_value;
      }
    } else {
      value = 0;
    }
    current_field_index_++;
  }

  std::shared_ptr<TextNode> node_;
  int current_field_index_;
};

}  // namespace

bool DeserializeFromString(Serializable& object, std::string_view text) {
  TextParser parser(text);
  auto root_node = parser.Parse();
  if (!root_node) return false;

  TextDeserializer deserializer(root_node);
  object.Serialize(deserializer);
  return true;
}

}  // namespace serialization
}  // namespace perception
