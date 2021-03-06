// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/bigtable/internal/endian.h"
#include "google/cloud/bigtable/testing/table_integration_test.h"
#include "google/cloud/testing_util/chrono_literals.h"
#include "google/cloud/testing_util/init_google_mock.h"

namespace google {
namespace cloud {
namespace bigtable {
inline namespace BIGTABLE_CLIENT_NS {
namespace {
namespace btadmin = google::bigtable::admin::v2;
using namespace google::cloud::testing_util::chrono_literals;

class AdminAsyncIntegrationTest
    : public bigtable::testing::TableIntegrationTest {
 protected:
  std::shared_ptr<AdminClient> admin_client_;
  std::unique_ptr<TableAdmin> table_admin_;
  std::unique_ptr<noex::TableAdmin> noex_table_admin_;

  void SetUp() {
    TableIntegrationTest::SetUp();
    admin_client_ = CreateDefaultAdminClient(
        testing::TableTestEnvironment::project_id(), ClientOptions());
    table_admin_ = google::cloud::internal::make_unique<TableAdmin>(
        admin_client_, bigtable::testing::TableTestEnvironment::instance_id());
    noex_table_admin_ = google::cloud::internal::make_unique<noex::TableAdmin>(
        admin_client_, bigtable::testing::TableTestEnvironment::instance_id());
  }

  void TearDown() {}

  int CountMatchingTables(std::string const& table_id,
                          std::vector<btadmin::Table> const& tables) {
    std::string table_name =
        table_admin_->instance_name() + "/tables/" + table_id;
    auto count = std::count_if(tables.begin(), tables.end(),
                               [&table_name](btadmin::Table const& t) {
                                 return table_name == t.name();
                               });
    return static_cast<int>(count);
  }
};

/// @test Verify that `noex::TableAdmin` Async CRUD operations work as expected.
TEST_F(AdminAsyncIntegrationTest, CreateListGetDeleteTableTest) {
  // Currently this test uses mostly synchronous operations, as we implement
  // async versions we should replace them in this function.

  std::string const table_id = RandomTableId();
  auto previous_table_list =
      table_admin_->ListTables(btadmin::Table::NAME_ONLY);
  auto previous_count = CountMatchingTables(table_id, previous_table_list);
  ASSERT_EQ(0, previous_count) << "Table (" << table_id << ") already exists."
                               << " This is unexpected, as the table ids are"
                               << " generated at random.";

  CompletionQueue cq;
  std::thread pool([&cq] { cq.Run(); });

  // AsyncCreateTable()
  TableConfig table_config({{"fam", GcRule::MaxNumVersions(5)},
                            {"foo", GcRule::MaxAge(std::chrono::hours(24))}},
                           {"a1000", "a2000", "b3000", "m5000"});
  std::promise<btadmin::Table> promise_create_table;
  noex_table_admin_->AsyncCreateTable(
      table_id, table_config, cq,
      [&promise_create_table](CompletionQueue& cq, btadmin::Table& table,
                              grpc::Status const& status) {
        promise_create_table.set_value(std::move(table));
      });

  auto table = promise_create_table.get_future().get();

  std::promise<btadmin::Table> promise_get_table;
  noex_table_admin_->AsyncGetTable(
      table_id, btadmin::Table::FULL, cq,
      [&promise_get_table](CompletionQueue& cq, btadmin::Table& table,
                           grpc::Status const& status) {
        promise_get_table.set_value(std::move(table));
      });

  auto table_result = promise_get_table.get_future().get();

  EXPECT_EQ(table.name(), table_result.name())
      << "Mismatched names for GetTable(" << table_id << "): " << table.name()
      << " != " << table_result.name();

  auto count_matching_families = [](btadmin::Table const& table,
                                    std::string const& name) {
    int count = 0;
    for (auto const& kv : table.column_families()) {
      if (kv.first == name) {
        ++count;
      }
    }
    return count;
  };
  EXPECT_EQ(1, count_matching_families(table_result, "fam"));
  EXPECT_EQ(1, count_matching_families(table_result, "foo"));

  // update table
  std::vector<bigtable::ColumnFamilyModification> column_modification_list = {
      bigtable::ColumnFamilyModification::Create(
          "newfam",
          GcRule::Intersection(GcRule::MaxAge(std::chrono::hours(7 * 24)),
                               GcRule::MaxNumVersions(1))),
      bigtable::ColumnFamilyModification::Update("fam",
                                                 GcRule::MaxNumVersions(2)),
      bigtable::ColumnFamilyModification::Drop("foo")};

  std::promise<btadmin::Table> promise_column_family;

  noex_table_admin_->AsyncModifyColumnFamilies(
      table_id, column_modification_list, cq,
      [&promise_column_family](CompletionQueue& cq, btadmin::Table& table,
                               grpc::Status const& status) {
        promise_column_family.set_value(std::move(table));
      });

  auto table_modified = promise_column_family.get_future().get();

  EXPECT_EQ(1, count_matching_families(table_modified, "fam"));
  EXPECT_EQ(0, count_matching_families(table_modified, "foo"));
  EXPECT_EQ(1, count_matching_families(table_modified, "newfam"));
  auto const& gc = table_modified.column_families().at("newfam").gc_rule();
  EXPECT_TRUE(gc.has_intersection());
  EXPECT_EQ(2, gc.intersection().rules_size());

  // delete table
  DeleteTable(table_id);
  // List to verify it is no longer there
  auto current_table_list = table_admin_->ListTables(btadmin::Table::NAME_ONLY);
  auto table_count = CountMatchingTables(table_id, current_table_list);
  EXPECT_EQ(0, table_count);

  cq.Shutdown();
  pool.join();
}

/// @test Verify that `noex::TableAdmin` AsyncDropRowsByPrefix works
TEST_F(AdminAsyncIntegrationTest, AsyncDropRowsByPrefixTest) {
  std::string const table_id = RandomTableId();
  std::string const column_family1 = "family1";
  std::string const column_family2 = "family2";
  std::string const column_family3 = "family3";

  bigtable::TableConfig table_config = bigtable::TableConfig(
      {{column_family1, bigtable::GcRule::MaxNumVersions(10)},
       {column_family2, bigtable::GcRule::MaxNumVersions(10)},
       {column_family3, bigtable::GcRule::MaxNumVersions(10)}},
      {});

  CompletionQueue cq;
  std::thread pool([&cq] { cq.Run(); });

  std::promise<btadmin::Table> promise_create_table;
  noex_table_admin_->AsyncCreateTable(
      table_id, table_config, cq,
      [&promise_create_table](CompletionQueue& cq, btadmin::Table& table,
                              grpc::Status const& status) {
        promise_create_table.set_value(std::move(table));
      });

  auto table_created = promise_create_table.get_future().get();

  bigtable::Table table(data_client_, table_id);

  // Create a vector of cell which will be inserted into bigtable
  std::string const row_key1_prefix = "DropRowPrefix1";
  std::string const row_key2_prefix = "DropRowPrefix2";
  std::string const row_key1 = row_key1_prefix + "-Key1";
  std::string const row_key1_1 = row_key1_prefix + "_1-Key1";
  std::string const row_key2 = row_key2_prefix + "-Key2";
  std::vector<bigtable::Cell> created_cells{
      {row_key1, column_family1, "column_id1", 0, "v-c-0-0", {}},
      {row_key1, column_family1, "column_id1", 1000, "v-c-0-1", {}},
      {row_key1, column_family2, "column_id3", 2000, "v-c-0-2", {}},
      {row_key1_1, column_family2, "column_id3", 2000, "v-c-0-2", {}},
      {row_key1_1, column_family2, "column_id3", 3000, "v-c-0-2", {}},
      {row_key2, column_family2, "column_id2", 2000, "v-c0-0-0", {}},
      {row_key2, column_family3, "column_id3", 3000, "v-c1-0-2", {}},
  };
  std::vector<bigtable::Cell> expected_cells{
      {row_key2, column_family2, "column_id2", 2000, "v-c0-0-0", {}},
      {row_key2, column_family3, "column_id3", 3000, "v-c1-0-2", {}}};

  // Create records
  CreateCells(table, created_cells);

  // Delete all the records for a row
  std::promise<google::protobuf::Empty> promise_drop_row;
  noex_table_admin_->AsyncDropRowsByPrefix(
      table_id, row_key1_prefix, cq,
      [&promise_drop_row](CompletionQueue& cq,
                          google::protobuf::Empty& response,
                          grpc::Status const& status) {
        promise_drop_row.set_value(std::move(response));
      });

  auto response = promise_drop_row.get_future().get();
  auto actual_cells = ReadRows(table, bigtable::Filter::PassAllFilter());
  DeleteTable(table_id);

  CheckEqualUnordered(expected_cells, actual_cells);

  cq.Shutdown();
  pool.join();
}

TEST_F(AdminAsyncIntegrationTest, AsyncDropAllRowsTest) {
  std::string const table_id = RandomTableId();
  std::string const column_family1 = "family1";
  std::string const column_family2 = "family2";
  std::string const column_family3 = "family3";
  bigtable::TableConfig table_config = bigtable::TableConfig(
      {{column_family1, bigtable::GcRule::MaxNumVersions(10)},
       {column_family2, bigtable::GcRule::MaxNumVersions(10)},
       {column_family3, bigtable::GcRule::MaxNumVersions(10)}},
      {});

  CompletionQueue cq;
  std::thread pool([&cq] { cq.Run(); });

  std::promise<btadmin::Table> promise_create_table;
  noex_table_admin_->AsyncCreateTable(
      table_id, table_config, cq,
      [&promise_create_table](CompletionQueue& cq, btadmin::Table& table,
                              grpc::Status const& status) {
        promise_create_table.set_value(std::move(table));
      });

  auto table_created = promise_create_table.get_future().get();

  bigtable::Table table(data_client_, table_id);

  // Create a vector of cell which will be inserted into bigtable
  std::string const row_key1 = "DropRowKey1";
  std::string const row_key2 = "DropRowKey2";
  std::vector<bigtable::Cell> created_cells{
      {row_key1, column_family1, "column_id1", 0, "v-c-0-0", {}},
      {row_key1, column_family1, "column_id1", 1000, "v-c-0-1", {}},
      {row_key1, column_family2, "column_id3", 2000, "v-c-0-2", {}},
      {row_key2, column_family2, "column_id2", 2000, "v-c0-0-0", {}},
      {row_key2, column_family3, "column_id3", 3000, "v-c1-0-2", {}},
  };

  // Create records
  CreateCells(table, created_cells);

  // Delete all the records from a table
  std::promise<google::protobuf::Empty> promise_drop_row;
  noex_table_admin_->AsyncDropAllRows(
      table_id, cq,
      [&promise_drop_row](CompletionQueue& cq,
                          google::protobuf::Empty& response,
                          grpc::Status const& status) {
        promise_drop_row.set_value(std::move(response));
      });
  auto response = promise_drop_row.get_future().get();

  auto actual_cells = ReadRows(table, bigtable::Filter::PassAllFilter());
  DeleteTable(table_id);

  ASSERT_TRUE(actual_cells.empty());
  cq.Shutdown();
  pool.join();
}

}  // namespace
}  // namespace BIGTABLE_CLIENT_NS
}  // namespace bigtable
}  // namespace cloud
}  // namespace google

int main(int argc, char* argv[]) {
  google::cloud::testing_util::InitGoogleMock(argc, argv);

  // Make sure the arguments are valid.
  if (argc != 3) {
    std::string const cmd = argv[0];
    auto last_slash = std::string(argv[0]).find_last_of('/');
    std::cerr << "Usage: " << cmd.substr(last_slash + 1)
              << " <project> <instance>" << std::endl;
    return 1;
  }

  std::string const project_id = argv[1];
  std::string const instance_id = argv[2];

  (void)::testing::AddGlobalTestEnvironment(
      new google::cloud::bigtable::testing::TableTestEnvironment(project_id,
                                                                 instance_id));

  return RUN_ALL_TESTS();
}
