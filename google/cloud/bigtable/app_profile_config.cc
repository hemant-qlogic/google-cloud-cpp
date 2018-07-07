// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/bigtable/app_profile_config.h"

namespace google {
namespace cloud {
namespace bigtable {
inline namespace BIGTABLE_CLIENT_NS {
AppProfileConfig AppProfileConfig::MultiClusterUseAny(AppProfileId profile_id) {
  AppProfileConfig tmp;
  tmp.proto_.set_app_profile_id(std::move(profile_id.get()));
  tmp.proto_.mutable_app_profile()
      ->mutable_multi_cluster_routing_use_any()
      ->Clear();
  return tmp;
}

AppProfileConfig AppProfileConfig::SingleClusterRouting(
    AppProfileId profile_id, ClusterId cluster_id,
    bool allow_transactional_writes) {
  AppProfileConfig tmp;
  tmp.proto_.set_app_profile_id(std::move(profile_id.get()));
  auto& routing =
      *tmp.proto_.mutable_app_profile()->mutable_single_cluster_routing();
  routing.set_cluster_id(std::move(cluster_id.get()));
  routing.set_allow_transactional_writes(allow_transactional_writes);
  return tmp;
}

}  // namespace BIGTABLE_CLIENT_NS
}  // namespace bigtable
}  // namespace cloud
}  // namespace google