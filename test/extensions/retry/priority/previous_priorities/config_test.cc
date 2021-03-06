#include "envoy/config/retry/previous_priorities/previous_priorities_config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/priority/previous_priorities/config.h"
#include "extensions/retry/priority/well_known_names.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

class RetryPriorityTest : public ::testing::Test {
public:
  void initialize() {
    auto factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
        RetryPriorityValues::get().PreviousPrioritiesRetryPriority);

    envoy::config::retry::previous_priorities::PreviousPrioritiesConfig config;
    config.set_update_frequency(update_frequency_);
    // Use createEmptyConfigProto to exercise that code path. This ensures the proto returned
    // by that method is compatible with the downcast in createRetryPriority.
    auto empty = factory->createEmptyConfigProto();
    empty->MergeFrom(config);
    retry_priority_ = factory->createRetryPriority(*empty, 3);
  }

  void addHosts(size_t priority, int count, int healthy_count) {
    auto host_set = priority_set_.getMockHostSet(priority);

    host_set->hosts_.resize(count);
    host_set->healthy_hosts_.resize(healthy_count);
    host_set->runCallbacks({}, {});
  }

  std::vector<Upstream::MockHostSet> host_sets_;
  uint32_t update_frequency_{1};
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Upstream::RetryPrioritySharedPtr retry_priority_;
};

TEST_F(RetryPriorityTest, DefaultFrequency) {
  initialize();

  const Upstream::HealthyLoad original_priority_load({100u, 0u});
  addHosts(0, 2, 2);
  addHosts(1, 2, 2);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  // Before any hosts attempted, load should be unchanged.
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  const Upstream::HealthyLoad expected_priority_load({0u, 100u});
  // After attempting a host in P0, P1 should receive all the load.
  retry_priority_->onHostAttempted(host1);
  ASSERT_EQ(expected_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  // After we've tried host2, we've attempted all priorities and should reset back to the original
  // priority load.
  retry_priority_->onHostAttempted(host2);
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));
}

// Tests that we handle all hosts being unhealthy in the orignal priority set.
TEST_F(RetryPriorityTest, NoHealthyUpstreams) {
  initialize();

  const Upstream::HealthyLoad original_priority_load({0u, 0u, 0u});
  addHosts(0, 10, 0);
  addHosts(1, 10, 0);
  addHosts(2, 10, 0);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  auto host3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host3, priority()).WillByDefault(Return(2));

  // Before any hosts attempted, load should be unchanged.
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  {
    // After attempting a host in P0, load should remain unchanged.
    const Upstream::HealthyLoad expected_priority_load({0u, 0u, 0u});
    retry_priority_->onHostAttempted(host1);
    ASSERT_EQ(expected_priority_load,
              retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));
  }
}

// Tests that spillover happens as we ignore attempted priorities.
TEST_F(RetryPriorityTest, DefaultFrequencyDegradedPriorities) {
  initialize();

  const Upstream::HealthyLoad original_priority_load({42u, 28u, 30u});
  addHosts(0, 10, 3);
  addHosts(1, 10, 2);
  addHosts(2, 10, 10);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  auto host3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host3, priority()).WillByDefault(Return(2));

  // Before any hosts attempted, load should be unchanged.
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  {
    // After attempting a host in P0, load should be split between P1 and P2 since P1 is degraded.
    const Upstream::HealthyLoad expected_priority_load({0u, 28u, 72u});
    retry_priority_->onHostAttempted(host1);
    ASSERT_EQ(expected_priority_load,
              retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));
  }

  // After we've tried host2, everything should go to P2.
  const Upstream::HealthyLoad expected_priority_load({0u, 0u, 100u});
  retry_priority_->onHostAttempted(host2);
  ASSERT_EQ(expected_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  // Once we've exhausted all priorities, we should return to the originial load.
  retry_priority_->onHostAttempted(host3);
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));
}

// Tests that we can override the frequency at which we update the priority load with the
// update_frequency parameter.
TEST_F(RetryPriorityTest, OverridenFrequency) {
  update_frequency_ = 2;
  initialize();

  const Upstream::HealthyLoad original_priority_load({100u, 0u});
  addHosts(0, 2, 2);
  addHosts(1, 2, 2);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  // Before any hosts attempted, load should be unchanged.
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  // After attempting a single host in P0, we should leave the priority load unchanged.
  retry_priority_->onHostAttempted(host1);
  ASSERT_EQ(original_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));

  // After a second attempt, the prioity load should change.
  const Upstream::HealthyLoad expected_priority_load({0u, 100u});
  retry_priority_->onHostAttempted(host1);
  ASSERT_EQ(expected_priority_load,
            retry_priority_->determinePriorityLoad(priority_set_, original_priority_load));
}

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
