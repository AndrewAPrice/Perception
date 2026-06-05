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

#pragma once

#include "perception/registry.h"
#include "status.h"

class RegistryServer : public ::perception::Registry::Server {
 public:
  RegistryServer() = default;
  virtual ~RegistryServer() = default;

  virtual ::StatusOr<::perception::GetRegistryValueResponse>
  GetRegistryValue(const ::perception::GetRegistryValueRequest& request,
                   ::perception::ProcessId sender) override;

  virtual ::perception::Status SetRegistryValue(
      const ::perception::SetRegistryValueRequest& request,
      ::perception::ProcessId sender) override;

  virtual ::perception::Status DeleteRegistryValue(
      const ::perception::DeleteRegistryValueRequest& request,
      ::perception::ProcessId sender) override;

  virtual ::perception::Status RegisterRegistryListener(
      const ::perception::RegisterRegistryListenerRequest& request,
      ::perception::ProcessId sender) override;

  virtual ::perception::Status UnregisterRegistryListener(
      const ::perception::UnregisterRegistryListenerRequest& request,
      ::perception::ProcessId sender) override;

  virtual ::StatusOr<::perception::GetRegistryKeysResponse>
  GetRegistryKeys(const ::perception::GetRegistryKeysRequest& request,
                  ::perception::ProcessId sender) override;

  virtual ::StatusOr<::perception::GetNamespacesResponse>
  GetNamespaces(::perception::ProcessId sender) override;
};
