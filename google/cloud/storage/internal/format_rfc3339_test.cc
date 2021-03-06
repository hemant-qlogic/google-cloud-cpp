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

#include "google/cloud/storage/internal/format_rfc3339.h"
#include "google/cloud/storage/internal/parse_rfc3339.h"
#include <gmock/gmock.h>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace internal {
namespace {
using ::testing::HasSubstr;

TEST(FormatRfc3339Test, NoFractional) {
  auto timestamp = ParseRfc3339("2018-08-02T01:02:03Z");
  std::string actual = FormatRfc3339(timestamp);
  EXPECT_EQ("2018-08-02T01:02:03Z", actual);
}

TEST(FormatRfc3339Test, FractionalMillis) {
  auto timestamp = ParseRfc3339("2018-08-02T01:02:03.123Z");
  std::string actual = FormatRfc3339(timestamp);
  EXPECT_EQ("2018-08-02T01:02:03.123Z", actual);
}

TEST(FormatRfc3339Test, FractionalMillsSmall) {
  auto timestamp = ParseRfc3339("2018-08-02T01:02:03.001Z");
  std::string actual = FormatRfc3339(timestamp);
  EXPECT_EQ("2018-08-02T01:02:03.001Z", actual);
}

TEST(FormatRfc3339Test, FractionalMicros) {
  auto timestamp = ParseRfc3339("2018-08-02T01:02:03.123456Z");
  std::string actual = FormatRfc3339(timestamp);

  bool system_clock_has_micros = std::ratio_greater_equal<
      std::micro, std::chrono::system_clock::duration::period>::value;
  if (system_clock_has_micros) {
    EXPECT_EQ("2018-08-02T01:02:03.123456Z", actual);
  } else {
    // On platforms where the system clock has less than microsecond precision
    // just check for milliseconds.
    EXPECT_THAT(actual, HasSubstr("2018-08-02T01:02:03.123"));
  }
}

TEST(FormatRfc3339Test, FractionalNanos) {
  auto timestamp = ParseRfc3339("2018-08-02T01:02:03.123456789Z");
  std::string actual = FormatRfc3339(timestamp);

  bool system_clock_has_nanos = std::ratio_greater_equal<
      std::nano, std::chrono::system_clock::duration::period>::value;
  if (system_clock_has_nanos) {
    EXPECT_EQ("2018-08-02T01:02:03.123456789Z", actual);
  } else {
    // On platforms where the system clock has less than nanosecond precision
    // just check for milliseconds.
    EXPECT_THAT(actual, HasSubstr("2018-08-02T01:02:03.123"));
  }
}

}  // namespace
}  // namespace internal
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
