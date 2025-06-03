#include "test/integration/fcds_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

// FCDS currently supports only Delta discovery.
#define ON_DEMAND_FCDS_INTEGRATION_PARAMS                                                          \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Delta))

class OnDemandFcdsIntegrationTest : public FcdsIntegrationTestBase {
public:
  envoy::config::listener::v3::Listener listenerConfig(const std::string& name,
                                                       const std::string& fcds_collection_name) {
    auto listener =
        FcdsIntegrationTestBase::listenerConfig(name, fcds_collection_name, true, {}, {}, {});
    auto* listener_filter = listener.add_listener_filters();

    const std::string config = R"EOF(
      name: on_demand_fcds
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.on_demand_fcds.v3.FilterConfig
    )EOF";

    TestUtility::loadFromYaml(config, *listener_filter);
    return listener;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, OnDemandFcdsIntegrationTest,
                         ON_DEMAND_FCDS_INTEGRATION_PARAMS);

TEST_P(OnDemandFcdsIntegrationTest, BasicSuccess) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_);
  sendLdsResponse(listener, listeners_v1_);
  expectListenersUpdateStats(1, 1, 1);
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));
  expectLdsAck();

  registerTestServerPorts({l0_port_, l1_port_});
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  // Test that the on-demand FCDS filter is properly configured and can handle connections
  Network::Address::InstanceConstSharedPtr source_address =
      Network::Utility::parseInternetAddressNoThrow(ip_2_);
  IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort(l0_port_), nullptr, source_address);

  tcp_client->close();
}

} // namespace Envoy
