// Copyright 2024 Google LLC
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

#pragma once

#include "perception/ui/node.h"

#define NODE_MODIFIER_1(name, method, type_a)                     \
  class name {                                                    \
   public:                                                        \
    name(type_a a) : a_(a) {}                                     \
    void Apply(::perception::ui::Node &node) { node.method(a_); } \
                                                                  \
   private:                                                       \
    type_a a_;                                                    \
  };

#define NODE_MODIFIER_1d(name, method, type_a, a_default)         \
  class name {                                                    \
   public:                                                        \
    name(type_a a = a_default) : a_(a) {}                         \
    void Apply(::perception::ui::Node &node) { node.method(a_); } \
                                                                  \
   private:                                                       \
    type_a a_;                                                    \
  };

#define LAYOUT_MODIFIER(name, method, params)  \
  class name {                                 \
   public:                                     \
    name() {}                                  \
    void Apply(::perception::ui::Node &node) { \
      node.GetLayout().method(params);         \
    }                                          \
  };

#define LAYOUT_MODIFIER_1(name, method, type_a)                               \
  class name {                                                                \
   public:                                                                    \
    name(type_a a) : a_(a) {}                                                 \
    void Apply(::perception::ui::Node &node) { node.GetLayout().method(a_); } \
                                                                              \
   private:                                                                   \
    type_a a_;                                                                \
  };

#define LAYOUT_MODIFIER_2(name, method, type_a, type_b) \
  class name {                                          \
   public:                                              \
    name(type_a a, type_b b) : a_(a), b_(b) {}          \
    void Apply(::perception::ui::Node &node) {          \
      node.GetLayout().method(a_, b_);                  \
    }                                                   \
                                                        \
   private:                                             \
    type_a a_;                                          \
    type_b b_;                                          \
  };

#define NODE_WITH_COMPONENT(name, type)                                \
  template <typename... Modifiers>                                     \
  std::shared_ptr<perception::ui::Node> name(Modifiers... modifiers) { \
    auto node = std::make_shared<perception::ui::Node>();              \
    node->Add<type>();                                                 \
    ApplyModifiersToNode(*node, modifiers...);                         \
    return std::move(node);                                            \
  }

#define COMPONENT_MODIFIER(name, type, method)                    \
  class name {                                                    \
   public:                                                        \
    name() {}                                                     \
    void Apply(::perception::ui::Node &node) {                    \
      if (auto component = node.Get<type>()) component->method(); \
    }                                                             \
  };

#define COMPONENT_MODIFIER_1(name, type, method, type_a)            \
  class name {                                                      \
   public:                                                          \
    name(type_a a) : a_(a) {}                                       \
    void Apply(::perception::ui::Node &node) {                      \
      if (auto component = node.Get<type>()) component->method(a_); \
    }                                                               \
                                                                    \
   private:                                                         \
    type_a a_;                                                      \
  };

#define COMPONENT_MODIFIER_1d(name, type, method, type_a, a_default) \
  class name {                                                      \
   public:                                                          \
    name(type_a a = a_default) : a_(a) {}                           \
    void Apply(::perception::ui::Node &node) {                      \
      if (auto component = node.Get<type>()) component->method(a_); \
    }                                                               \
                                                                    \
   private:                                                         \
    type_a a_;                                                      \
  };
