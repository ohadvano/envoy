#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"

#include "google/protobuf/any.pb.h"
#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// FCDS currently supports only Delta discovery.
#define FCDS_INTEGRATION_PARAMS                                                                    \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Delta))

class FcdsIntegrationTest : public AdsIntegrationTest {
public:
  FcdsIntegrationTest()
      : AdsIntegrationTest(
            bootstrapConfig((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
                                             (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                         ? "GRPC"
                                         : "DELTA_GRPC")) {}

  std::string bootstrapConfig(const std::string& api_type) {
    return fmt::format(R"EOF(
      dynamic_resources:
        lds_config:
          ads: {{}}
        ads_config:
          api_type: {0}
          set_node_on_first_message_only: true
      static_resources:
        clusters:
          name: cluster_0
          load_assignment:
            cluster_name: cluster_0
            endpoints:
            - lb_endpoints:
              - endpoint:
                  address:
                    socket_address:
                      address: 127.0.0.1
                      port_value: 0
      admin:
        access_log:
        - name: envoy.access_loggers.file
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
            path: "{1}"
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 0
      )EOF",
                          api_type, Platform::null_device_path);
  }

  std::vector<envoy::config::listener::v3::Listener> singleListenerWithFcds() {
    const std::string config = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return {listener};
  }

  std::vector<envoy::config::listener::v3::Listener> singleListenerWithDefaultFilterChainWithFcds() {
    const std::string config = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      default_filter_chain:
        filters:
          name: direct_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            response:
              inline_string: "pong_from_listener_0_default_filter_chain"
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return {listener};
  }

  std::vector<envoy::config::listener::v3::Listener> singleListenerWithDefaultFilterChainWithFcdsNoWarming() {
    const std::string config = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      default_filter_chain:
        filters:
          name: direct_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            response:
              inline_string: "pong_from_listener_0_default_filter_chain"
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        start_listener_without_warming: true
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return {listener};
  }

  std::vector<envoy::config::listener::v3::Listener> singleListenerWithStaticFilterChainWithFcds() {
    const std::string config = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
      - name: listener_0_static_filter_chain
        filter_chain_match:
          source_prefix_ranges:
          - address_prefix: 127.0.0.4
            prefix_len: 32
        filters:
          name: direct_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            response:
              inline_string: "pong_from_listener_0_static_filter_chain"
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return {listener};
  }

  std::vector<envoy::config::listener::v3::Listener>
  singleListenerWithStaticFilterWithEcdsAndNoWarmingFcds() {
    const std::string config = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
      - name: listener_0_static_filter_chain
        filter_chain_match:
          source_prefix_ranges:
          - address_prefix: 127.0.0.4
            prefix_len: 32
        filters:
        - name: listener_0_static_filter_chain_0_direct_response
          config_discovery:
            config_source:
              resource_api_version: V3
              api_config_source:
                api_type: GRPC
                transport_api_version: V3
                grpc_services:
                - envoy_grpc:
                    cluster_name: ads_cluster
            type_urls:
            - type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        start_listener_without_warming: true
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return {listener};
  }

  std::vector<envoy::config::listener::v3::Listener>
  singleListenerWithStaticFilterChainWithDefaultFilterChainWithFcds() {
    const std::string config = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      default_filter_chain:
        filters:
          name: direct_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            response:
              inline_string: "pong_from_listener_0_default_filter_chain"
      filter_chains:
      - name: listener_0_static_filter_chain
        filter_chain_match:
          source_prefix_ranges:
          - address_prefix: 127.0.0.4
            prefix_len: 32
        filters:
          name: direct_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            response:
              inline_string: "pong_from_listener_0_static_filter_chain"
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return {listener};
  }

  std::vector<envoy::config::listener::v3::Listener> twoListenersSeparateFcdsSubscription() {
    const std::string config1 = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener1;
    TestUtility::loadFromYaml(config1, listener1);

    const std::string config2 = fmt::format(R"EOF(
      name: listener_1
      stat_prefix: listener_1
      address:
        socket_address:
          address: {}
          port_value: 0
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_1/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener2;
    TestUtility::loadFromYaml(config2, listener2);

    return {listener1, listener2};
  }

  std::vector<envoy::config::listener::v3::Listener> twoListenersSameFcdsSubscription() {
    const std::string config1 = fmt::format(R"EOF(
      name: listener_0
      stat_prefix: listener_0
      address:
        socket_address:
          address: {}
          port_value: 0
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/all_listeners/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener1;
    TestUtility::loadFromYaml(config1, listener1);

    const std::string config2 = fmt::format(R"EOF(
      name: listener_1
      stat_prefix: listener_1
      address:
        socket_address:
          address: {}
          port_value: 0
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/all_listeners/*
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF", Network::Test::getLoopbackAddressString(ipVersion()));

    envoy::config::listener::v3::Listener listener2;
    TestUtility::loadFromYaml(config2, listener2);

    return {listener1, listener2};
  }

  std::vector<envoy::config::listener::v3::FilterChain> singleFilterChain() {
    const std::string config = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_0
      filters:
        name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_0_filter_chain_0"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config, filter_chain);
    return {filter_chain};
  }

  std::vector<envoy::config::listener::v3::FilterChain> singleFilterChainWithEcds() {
    const std::string config = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_0
      filters:
      - name: listener_0_filter_chain_0_direct_response
        config_discovery:
          config_source:
            resource_api_version: V3
            api_config_source:
              api_type: GRPC
              transport_api_version: V3
              grpc_services:
              - envoy_grpc:
                  cluster_name: ads_cluster
          type_urls:
          - type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config, filter_chain);
    return {filter_chain};
  }

  std::vector<envoy::config::listener::v3::FilterChain> twoFilterChainsSeparateSubscription() {
    const std::string config1 = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_0
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_0_filter_chain_0"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain1;
    TestUtility::loadFromYaml(config1, filter_chain1);

    const std::string config2 = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_1/filter_chain_0
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_1_filter_chain_0"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain2;
    TestUtility::loadFromYaml(config2, filter_chain2);

    return {filter_chain1, filter_chain2};
  }

  std::vector<envoy::config::listener::v3::FilterChain> singleFilterChainTwoListeners() {
    const std::string config = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/all_listeners/filter_chain_0
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_any_listener_filter_chain_0"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config, filter_chain);
    return {filter_chain};
  }

  std::vector<envoy::config::listener::v3::FilterChain> twoFilterChainsWithMatching() {
    const std::string config1 = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_0
      filter_chain_match:
        source_prefix_ranges:
        - address_prefix: 127.0.0.2
          prefix_len: 32
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_0_filter_chain_0"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain1;
    TestUtility::loadFromYaml(config1, filter_chain1);

    const std::string config2 = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_1
      filter_chain_match:
        source_prefix_ranges:
        - address_prefix: 127.0.0.3
          prefix_len: 32
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_0_filter_chain_1"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain2;
    TestUtility::loadFromYaml(config2, filter_chain2);

    return {filter_chain1, filter_chain2};
  }

  std::vector<envoy::config::listener::v3::FilterChain> singleFilterChainWithMatching() {
    const std::string config1 = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_0
      filter_chain_match:
        source_prefix_ranges:
        - address_prefix: 127.0.0.2
          prefix_len: 32
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_0_filter_chain_0"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config1, filter_chain);

    return {filter_chain};
  }

  std::vector<envoy::config::listener::v3::FilterChain> secondSingleFilterChainWithMatching() {
    const std::string config1 = R"EOF(
      name: xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/filter_chain_1
      filter_chain_match:
        source_prefix_ranges:
        - address_prefix: 127.0.0.3
          prefix_len: 32
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "pong_from_listener_0_filter_chain_1"
    )EOF";

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config1, filter_chain);

    return {filter_chain};
  }

  std::vector<envoy::config::core::v3::TypedExtensionConfig> getExtensionConfigForEcds() {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name("listener_0_filter_chain_0_direct_response");
    auto configuration = envoy::extensions::filters::network::direct_response::v3::Config();
    configuration.mutable_response()->set_inline_string("pong_from_listener_0_filter_chain_0_ecds_response");
    typed_config.mutable_typed_config()->PackFrom(configuration);
    return {typed_config};
  }

  std::vector<envoy::config::core::v3::TypedExtensionConfig> getExtensionConfigForStaticFilterChainEcds() {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name("listener_0_static_filter_chain_0_direct_response");
    auto configuration = envoy::extensions::filters::network::direct_response::v3::Config();
    configuration.mutable_response()->set_inline_string("pong_from_listener_0_static_filter_chain_0_ecds_response");
    typed_config.mutable_typed_config()->PackFrom(configuration);
    return {typed_config};
  }

  void sendDataVerifyResponse(const std::string& port_name, const std::string& expected_response, const std::string& source_ip) {
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
    Network::Address::InstanceConstSharedPtr source_address =
        Network::Utility::parseInternetAddressNoThrow(source_ip);
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name), nullptr, source_address);
    ASSERT_TRUE(tcp_client->write("ping", false, false));
    ASSERT_TRUE(tcp_client->waitForData(expected_response.length()));
    ASSERT_EQ(expected_response, tcp_client->data());
    tcp_client->close();
  }

  const std::string listener_0_port_name_ = "listener_0_tcp";
  const std::string listener_1_port_name_ = "listener_1_tcp";
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, FcdsIntegrationTest,
                         FCDS_INTEGRATION_PARAMS);

TEST_P(FcdsIntegrationTest, BasicSuccess) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = singleFilterChain();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
}

TEST_P(FcdsIntegrationTest, TwoListenersSeparateFcdsSubscription) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = twoListenersSeparateFcdsSubscription();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 2);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_1/*"}, {}));
  auto filter_chains = twoFilterChainsSeparateSubscription();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 2);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);
  test_server_->waitForCounterEq("listener.listener_1.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 2);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 2);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_1_port_name_, "pong_from_listener_1_filter_chain_0", "127.0.0.2");
}

TEST_P(FcdsIntegrationTest, TwoListenersSameFcdsSubscription) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = twoListenersSameFcdsSubscription();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 2);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/all_listeners/*"}, {}));
  auto filter_chains = singleFilterChainTwoListeners();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 2);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);
  test_server_->waitForCounterEq("listener.listener_1.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 2);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 2);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_any_listener_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_1_port_name_, "pong_from_any_listener_filter_chain_0", "127.0.0.2");
}

TEST_P(FcdsIntegrationTest, ListenersWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = twoFilterChainsWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_1", "127.0.0.3");
}

TEST_P(FcdsIntegrationTest,
       ListenersWithDefaultFilterChainWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithDefaultFilterChainWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = twoFilterChainsWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_1", "127.0.0.3");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_default_filter_chain", "127.0.0.4");
}

TEST_P(FcdsIntegrationTest,
       ListenersWithStaticFilterChainWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithStaticFilterChainWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = twoFilterChainsWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_1", "127.0.0.3");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_static_filter_chain", "127.0.0.4");
}

TEST_P(FcdsIntegrationTest,
       ListenersWithStaticFilterChainWithDefaultFilterChainWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithStaticFilterChainWithDefaultFilterChainWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = twoFilterChainsWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_1", "127.0.0.3");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_static_filter_chain", "127.0.0.4");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_default_filter_chain", "127.0.0.5");
}

TEST_P(FcdsIntegrationTest, ListenerWithFcdsWithEcds) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = singleFilterChainWithEcds();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");

  FakeStreamPtr ecds_stream;
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, ecds_stream));
  ecds_stream->startGrpcStream();

  EXPECT_TRUE(compareSotwDiscoveryRequest(
      Config::TypeUrl::get().TypedExtension, "1",
      {"listener_0_filter_chain_0_direct_response"}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok,
      "", ecds_stream.get()));

  auto extension_config = getExtensionConfigForEcds();
  sendSotwDiscoveryResponse(Config::TypeUrl::get().TypedExtension,
                            extension_config, "ecds_v1", ecds_stream.get(), {});
  test_server_->waitForCounterEq(
      "extension_config_discovery.network_filter.listener_0_filter_chain_0_direct_response.config_reload", 1);

  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0_ecds_response", "127.0.0.2");
}

TEST_P(FcdsIntegrationTest, TwoFcdsUpdates) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithDefaultFilterChainWithFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  auto filter_chains = singleFilterChainWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_default_filter_chain", "127.0.0.3");

  auto filter_chains2 = secondSingleFilterChainWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains2,
      filter_chains2, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 2);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 2);
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_1", "127.0.0.3");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_default_filter_chain", "127.0.0.4");
}

TEST_P(FcdsIntegrationTest, FcdsUpdateNoWarming) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithDefaultFilterChainWithFcdsNoWarming();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_default_filter_chain", "127.0.0.2");

  auto filter_chains = singleFilterChainWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
}

TEST_P(FcdsIntegrationTest, NoWarmingFcdsUpdateDuringServerInitialization) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {}));
  auto listeners = singleListenerWithStaticFilterWithEcdsAndNoWarmingFcds();
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TypeUrl::get().Listener, listeners,
      listeners, {}, "listener_v1");
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active", 1);

  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TypeUrl::get().FilterChain, "", {},
      {"xdstp://test/envoy.config.listener.v3.FilterChain/listener_0/*"}, {}));

  FakeStreamPtr ecds_stream;
  ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, ecds_stream));
  ecds_stream->startGrpcStream();

  EXPECT_TRUE(compareSotwDiscoveryRequest(
      Config::TypeUrl::get().TypedExtension, "1",
      {"listener_0_static_filter_chain_0_direct_response"}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok,
      "", ecds_stream.get()));

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  auto filter_chains = singleFilterChainWithMatching();
  sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
      Config::TypeUrl::get().FilterChain, filter_chains,
      filter_chains, {}, "filter_chains_v1");
  test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update", 1);
  test_server_->waitForCounterEq("listener.listener_0.fcds.update_success", 1);

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  auto extension_config = getExtensionConfigForStaticFilterChainEcds();
  sendSotwDiscoveryResponse(Config::TypeUrl::get().TypedExtension,
                            extension_config, "ecds_v1", ecds_stream.get(), {});
  test_server_->waitForCounterEq(
      "extension_config_discovery.network_filter.listener_0_static_filter_chain_0_direct_response.config_reload", 1);
  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  test_server_->waitForCounterEq("listener_manager.lds.update_success", 1);
  test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);

  registerTestServerPorts({listener_0_port_name_, listener_1_port_name_});
  test_server_->waitForCounterEq("listener_manager.listener_create_success", 1);
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_filter_chain_0", "127.0.0.2");
  sendDataVerifyResponse(listener_0_port_name_, "pong_from_listener_0_static_filter_chain_0_ecds_response", "127.0.0.4");
}


// TODO: after initialization, test FCDS update during previous FCDS update is still initializing

} // namespace
} // namespace Envoy
