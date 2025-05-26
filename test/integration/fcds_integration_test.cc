#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/test_common/utility.h"

#include "fake_upstream.h"
#include "google/protobuf/any.pb.h"
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
      : AdsIntegrationTest(bootstrapConfig((sotwOrDelta() == Grpc::SotwOrDelta::Sotw) ||
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

  struct FilterChainConfig {
    std::string name_;
    absl::optional<std::string> source_ip_match_;
    std::string filter_name_;
    absl::optional<std::string> direct_response_;
  };

  struct ExpectedListenerDump {
    std::string listener_version_;
    std::string filter_chains_version_;
    envoy::config::listener::v3::Listener listener_;
    std::vector<envoy::config::listener::v3::FilterChain> fcds_filter_chains_;
    bool warming_{false};
  };

  envoy::config::listener::v3::Listener
  listenerConfig(const std::string& name, const std::string& fcds_collection_name,
                 bool start_without_warming, absl::optional<std::string> default_response,
                 absl::optional<FilterChainConfig> filter_chain_config,
                 absl::optional<std::vector<std::tuple<std::string, std::string>>> matcher_rules) {
    std::string filter_chain;
    if (filter_chain_config.has_value()) {
      auto& config = filter_chain_config.value();
      std::string match;
      if (config.source_ip_match_.has_value()) {
        match = fmt::format(R"EOF(
        filter_chain_match:
          source_prefix_ranges:
          - address_prefix: {0}
            prefix_len: 32
        )EOF",
                            config.source_ip_match_.value());
      }

      std::string filter_config;
      if (config.direct_response_.has_value()) {
        filter_config = fmt::format(R"EOF(
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            keep_open_after_response: {0}
            response:
              inline_string: "{1}"
        )EOF",
                                    keep_open_after_response_, config.direct_response_.value());
      } else {
        filter_config = R"EOF(
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
      }

      filter_chain = fmt::format(R"EOF(
      filter_chains:
      - name: {0}
        {1}
        filters:
        - name: {2}
          {3}
      )EOF",
                                 config.name_, match, config.filter_name_, filter_config);
    }

    std::string fcds_config =
        fmt::format(R"EOF(
      fcds_config:
        name: xdstp://test/envoy.config.listener.v3.FilterChain/{0}/*
        start_listener_without_warming: {1}
        config_source:
          resource_api_version: V3
          ads: {{}}
    )EOF",
                    fcds_collection_name, start_without_warming ? "true" : "false");

    std::string default_filter_chain;
    if (default_response.has_value()) {
      default_filter_chain = fmt::format(R"EOF(
      default_filter_chain:
        filters:
        - name: direct_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
            keep_open_after_response: {0}
            response:
              inline_string: "{1}"
      )EOF",
                                         keep_open_after_response_, default_response.value());
    }

    std::string listener_level_matcher;
    if (matcher_rules.has_value()) {
      listener_level_matcher = R"EOF(
      filter_chain_matcher:
        matcher_tree:
          input:
            name: source_ip_matcher
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
          exact_match_map:
            map:
      )EOF";

      for (const auto& [ip, filter_chain] : matcher_rules.value()) {
        listener_level_matcher += fmt::format(R"EOF(
              "{0}":
                action:
                  name: choose_filter_chain
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: {1}
        )EOF",
                                              ip, filter_chain);
      }
    }

    std::string config =
        fmt::format(R"EOF(
      name: {0}
      stat_prefix: {0}
      address:
        socket_address:
          address: {1}
          port_value: 0
      {2}
      {3}
      {4}
      {5}
    )EOF",
                    name, Network::Test::getLoopbackAddressString(ipVersion()), fcds_config,
                    filter_chain, default_filter_chain, listener_level_matcher);

    envoy::config::listener::v3::Listener listener;
    TestUtility::loadFromYaml(config, listener);
    return listener;
  }

  envoy::config::listener::v3::FilterChain
  filterChainConfig(const std::string& listener_name, const std::string& name,
                    const std::string& filter_name, absl::optional<std::string> direct_response,
                    absl::optional<std::string> source_ip_match) {
    std::string filter_config;
    if (direct_response.has_value()) {
      filter_config = fmt::format(R"EOF(
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          keep_open_after_response: {0}
          response:
            inline_string: "{1}"
      )EOF",
                                  keep_open_after_response_, direct_response.value());
    } else {
      filter_config = R"EOF(
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
    }

    std::string filter_chain_match;
    if (source_ip_match.has_value()) {
      filter_chain_match = fmt::format(R"EOF(
      filter_chain_match:
        source_prefix_ranges:
        - address_prefix: {0}
          prefix_len: 32
      )EOF",
                                       source_ip_match.value());
    }

    std::string config = fmt::format(R"EOF(
      name: {0}
      {1}
      filters:
        name: {2}
        {3}
    )EOF",
                                     xdstpResource(listener_name, name), filter_chain_match,
                                     filter_name, filter_config);

    envoy::config::listener::v3::FilterChain filter_chain;
    TestUtility::loadFromYaml(config, filter_chain);
    return filter_chain;
  }

  envoy::config::core::v3::TypedExtensionConfig
  extensionConfig(const std::string& name, const std::string& direct_response) {
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    auto configuration = envoy::extensions::filters::network::direct_response::v3::Config();
    configuration.set_keep_open_after_response(keep_open_after_response_);
    configuration.mutable_response()->set_inline_string(direct_response);
    typed_config.mutable_typed_config()->PackFrom(configuration);
    return typed_config;
  }

  std::string xdstpResource(const std::string& listener_name,
                            const std::string& filter_chain_name) {
    return fmt::format("xdstp://test/envoy.config.listener.v3.FilterChain/{}/{}", listener_name,
                       filter_chain_name);
  }

  IntegrationTcpClientPtr sendDataVerifyResponse(const std::string& port_name,
                                                 const std::string& expected_response,
                                                 const std::string& source_ip,
                                                 bool keep_open = false) {
    registerTestServerPorts({l0_port_, l1_port_});
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
    Network::Address::InstanceConstSharedPtr source_address =
        Network::Utility::parseInternetAddressNoThrow(source_ip);
    IntegrationTcpClientPtr tcp_client =
        makeTcpConnection(lookupPort(port_name), nullptr, source_address);
    EXPECT_TRUE(tcp_client->write("ping", false, false));
    EXPECT_TRUE(tcp_client->waitForData(expected_response.length()));
    EXPECT_EQ(expected_response, tcp_client->data());
    if (!keep_open) {
      tcp_client->close();
    }

    return tcp_client;
  }

  FakeStreamPtr waitForNewXdsStream() {
    FakeStreamPtr stream;
    EXPECT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, stream));
    stream->startGrpcStream();
    return stream;
  }

  void expectFilterChainUpdateStats(const std::string& listener, int listener_updates,
                                    int total_updates, int total_rejected = 0) {
    test_server_->waitForCounterEq("listener_manager.listener_dynamic_filter_chains_update",
                                   total_updates);
    test_server_->waitForCounterEq("listener." + listener + ".fcds.update_success",
                                   listener_updates);
    test_server_->waitForCounterEq("listener." + listener + ".fcds.update_rejected",
                                   total_rejected);
  }

  void expectDrainingFilterChains(int count) {
    test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", count);
  }

  void expectWarmingListeners(int listener_count) {
    test_server_->waitForGaugeGe("listener_manager.total_listeners_active", listener_count);
  }

  void expectDrainingListeners(int listener_count) {
    test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", listener_count);
  }

  void expectListenersUpdateStats(int total_updates, int active_listeners, int total_created) {
    test_server_->waitForCounterEq("listener_manager.lds.update_success", total_updates);
    test_server_->waitForGaugeEq("listener_manager.workers_started", 1);
    test_server_->waitForCounterEq("listener_manager.listener_create_success", total_created);
    test_server_->waitForGaugeEq("listener_manager.total_listeners_active", active_listeners);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);
  }

  void expectExtensionReloadStats(const std::string& name, int updates) {
    test_server_->waitForCounterEq(
        "extension_config_discovery.network_filter." + name + ".config_reload", updates);
  }

  void expectListenersModified(int count) {
    test_server_->waitForCounterEq("listener_manager.listener_modified", count);
  }

  void expectInitializing() {
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  }

  AssertionResult expectLdsSubscription() {
    return compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}, {}, {});
  }

  AssertionResult expectFcdsSubscription(const std::string& resource_names) {
    return compareDiscoveryRequest(Config::TypeUrl::get().FilterChain, "", {}, {resource_names},
                                   {});
  }

  AssertionResult expectExtensionSubscription(const std::string& resource_name,
                                              FakeStreamPtr& stream) {
    return compareSotwDiscoveryRequest(Config::TypeUrl::get().TypedExtension, "1", {resource_name},
                                       true, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                       stream.get());
  }

  AssertionResult expectFcdsUnsubscribe(const std::string& resource_names) {
    return compareDiscoveryRequest(Config::TypeUrl::get().FilterChain, "", {}, {},
                                   {resource_names});
  }

  void expectFcdsFailure(const std::string& filter_name, bool removed = false) {
    const std::string error_message_substring =
        fmt::format("filter chain '{}' cannot be {} as it was not added by filter chain discovery",
                    filter_name, removed ? "removed" : "updated");
    ASSERT_TRUE(compareDeltaDiscoveryRequest(
        Config::TypeUrl::get().FilterChain, {}, {}, xds_stream_.get(),
        Grpc::Status::WellKnownGrpcStatus::Internal, error_message_substring));
  }

  void expectLdsAck() {
    envoy::service::discovery::v3::DeltaDiscoveryRequest request;
    EXPECT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_EQ(request.type_url(), Config::TypeUrl::get().Listener);
    EXPECT_FALSE(request.response_nonce().empty());
  }

  void expectFcdsAck() {
    envoy::service::discovery::v3::DeltaDiscoveryRequest request;
    EXPECT_TRUE(xds_stream_->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_EQ(request.type_url(), Config::TypeUrl::get().FilterChain);
    EXPECT_FALSE(request.response_nonce().empty());
  }

  void sendLdsResponse(envoy::config::listener::v3::Listener listeners,
                       const std::string& version) {
    return sendLdsResponse(std::vector{listeners}, version);
  }

  void sendLdsResponse(std::vector<envoy::config::listener::v3::Listener> listeners,
                       const std::string& version) {
    return sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TypeUrl::get().Listener, listeners, listeners, {}, version);
  }

  void sendFcdsResponse(const std::string& version,
                        envoy::config::listener::v3::FilterChain filter_chains,
                        std::vector<std::string> removed_filter_chains = {}) {
    return sendFcdsResponse(version, std::vector{filter_chains}, removed_filter_chains);
  }

  void sendFcdsResponse(const std::string& version,
                        std::vector<envoy::config::listener::v3::FilterChain> filter_chains,
                        std::vector<std::string> removed_filter_chains = {}) {
    return sendDiscoveryResponse<envoy::config::listener::v3::FilterChain>(
        Config::TypeUrl::get().FilterChain, filter_chains, filter_chains, removed_filter_chains,
        version);
  }

  void sendExtensionResponse(envoy::config::core::v3::TypedExtensionConfig extension_config,
                             FakeStreamPtr& stream, const std::string& version) {
    sendExtensionResponse(std::vector{extension_config}, stream, version);
  }

  void
  sendExtensionResponse(std::vector<envoy::config::core::v3::TypedExtensionConfig> extension_config,
                        FakeStreamPtr& stream, const std::string& version) {
    sendSotwDiscoveryResponse(Config::TypeUrl::get().TypedExtension, extension_config, version,
                              stream.get(), {});
  }

  std::vector<envoy::config::listener::v3::FilterChain> noFilterChains() { return {}; }

  void expectConfigDump(const std::string& listener_version,
                        const std::string& filter_chains_version,
                        const envoy::config::listener::v3::Listener& listeners,
                        std::vector<envoy::config::listener::v3::FilterChain> fcds_filter_chains,
                        bool warming = false) {
    expectConfigDump({ExpectedListenerDump{listener_version, filter_chains_version, listeners,
                                           fcds_filter_chains, warming}});
  }

  void expectConfigDump(std::vector<ExpectedListenerDump> config) {
    expectListenerConfigDump(config);
    expectFilterChainsConfigDump(config);
  }

  void expectListenerConfigDump(std::vector<ExpectedListenerDump> config) {
    envoy::admin::v3::ListenersConfigDump expected_listeners;
    expected_listeners.set_version_info("system_version_info_this_is_a_test");
    envoy::admin::v3::FilterChainsConfigDump expected_filter_chains;

    auto actual_listeners = getListenersConfigDump();
    auto actual_filter_chains = getFilterChainsConfigDump();
    absl::flat_hash_map<std::string, envoy::admin::v3::ListenersConfigDump_DynamicListener*>
        listeners_map;
    for (const auto& [listener_version, filter_chains_Version, listener, filter_chains, warming] :
         config) {
      envoy::admin::v3::ListenersConfigDump_DynamicListener* listener_dump;
      if (listeners_map.contains(listener.name())) {
        listener_dump = listeners_map[listener.name()];
      } else {
        listener_dump = expected_listeners.add_dynamic_listeners();
        listeners_map[listener.name()] = listener_dump;
      }

      listener_dump->set_name(listener.name());
      envoy::admin::v3::ListenersConfigDump_DynamicListenerState* state;
      if (warming) {
        state = listener_dump->mutable_warming_state();
      } else {
        state = listener_dump->mutable_active_state();
      }

      state->set_version_info(listener_version);
      state->mutable_listener()->PackFrom(listener);
    }

    for (auto& listener : *actual_listeners.mutable_dynamic_listeners()) {
      if (listener.has_warming_state()) {
        listener.mutable_warming_state()->clear_last_updated();
      }
      if (listener.has_active_state()) {
        listener.mutable_active_state()->clear_last_updated();
      }
    }

    ASSERT_TRUE(TestUtility::protoEqual(expected_listeners, actual_listeners, true))
        << "Listeners config dump mismatch."
        << "\nExpected:\n"
        << expected_listeners.DebugString() << "\nActual:\n"
        << actual_listeners.DebugString();
  }

  void expectFilterChainsConfigDump(std::vector<ExpectedListenerDump> config) {
    envoy::admin::v3::FilterChainsConfigDump expected_filter_chains;

    auto actual_filter_chains = getFilterChainsConfigDump();
    absl::flat_hash_map<std::string, envoy::admin::v3::FilterChainsConfigDump_DynamicFilterChain*>
        filter_chains_map;
    for (const auto& [listener_version, filter_chains_Version, listener, filter_chains, warming] :
         config) {
      for (const auto& filter_chain : filter_chains) {
        envoy::admin::v3::FilterChainsConfigDump_DynamicFilterChain* filter_chain_dump;
        if (filter_chains_map.contains(filter_chain.name())) {
          filter_chain_dump = filter_chains_map[filter_chain.name()];
        } else {
          filter_chain_dump = expected_filter_chains.add_dynamic_filter_chains();
          filter_chains_map[filter_chain.name()] = filter_chain_dump;
        }

        filter_chain_dump->set_name(filter_chain.name());
        envoy::admin::v3::FilterChainsConfigDump_DynamicFilterChainState* state;
        if (warming) {
          state = filter_chain_dump->mutable_warming_state();
        } else {
          state = filter_chain_dump->mutable_active_state();
        }

        state->set_version_info(filter_chains_Version);
        state->mutable_filter_chain()->PackFrom(filter_chain);
      }
    }

    auto& dynamic_filter_chains = *actual_filter_chains.mutable_dynamic_filter_chains();
    dynamic_filter_chains.erase(
        std::remove_if(dynamic_filter_chains.begin(), dynamic_filter_chains.end(),
                       [](const auto& filter_chain) { return filter_chain.has_error_state(); }),
        dynamic_filter_chains.end());

    for (auto& filter_chain : *actual_filter_chains.mutable_dynamic_filter_chains()) {
      if (filter_chain.has_warming_state()) {
        filter_chain.mutable_warming_state()->clear_last_updated();
      }
      if (filter_chain.has_active_state()) {
        filter_chain.mutable_active_state()->clear_last_updated();
      }
    }

    ASSERT_TRUE(TestUtility::protoEqual(expected_filter_chains, actual_filter_chains, true))
        << "Filter chains config dump mismatch."
        << "\nExpected:\n"
        << expected_filter_chains.DebugString() << "\nActual:\n"
        << actual_filter_chains.DebugString();
  }

  bool keep_open_after_response_{false};
  const std::string all_listeners_ = "all_listeners";
  const std::string l0_name_ = "listener_0";
  const std::string l1_name_ = "listener_1";
  const std::string l0_port_ = "listener_0_tcp";
  const std::string l1_port_ = "listener_1_tcp";
  const std::string fc0_name_ = "filter_chain_0";
  const std::string fc1_name_ = "filter_chain_1";
  const std::string fc2_name_ = "filter_chain_2";
  const std::string ip_2_ = "127.0.0.2";
  const std::string ip_3_ = "127.0.0.3";
  const std::string ip_4_ = "127.0.0.4";
  const std::string ip_5_ = "127.0.0.5";
  const std::string filter_name_ = "direct_response";
  const std::string listeners_v1_ = "listener_0_v1";
  const std::string listeners_v2_ = "listener_0_v2";
  const std::string filter_chains_v1_ = "filter_chains_v1";
  const std::string filter_chains_v2_ = "filter_chains_v2";
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeltaWildcard, FcdsIntegrationTest,
                         FCDS_INTEGRATION_PARAMS);

TEST_P(FcdsIntegrationTest, BasicSuccess) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listeners = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listeners, listeners_v1_);
  expectWarmingListeners(1);
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "pong_from_listener_0_filter_chain_0";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, {});

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listeners, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, TwoListenersSeparateFcdsSubscription) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener_0 = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  auto listener_1 = listenerConfig(l1_name_, l1_name_, false, {}, {}, {});
  sendLdsResponse({listener_0, listener_1}, listeners_v1_);
  expectWarmingListeners(2);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l1_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_1_filter_chain_0";

  auto filter_chain_0 = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, {});
  auto filter_chain_1 = filterChainConfig(l1_name_, fc0_name_, filter_name_, direct_response_1, {});

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 1, 2);
  expectFilterChainUpdateStats(l1_name_, 1, 2);
  expectListenersUpdateStats(1, 2, 2);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_0, {filter_chain_0}},
       ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_1, {filter_chain_1}}});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l1_port_, direct_response_1, ip_2_);
}

TEST_P(FcdsIntegrationTest, TwoListenersSameFcdsSubscription) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener_0 = listenerConfig(l0_name_, all_listeners_, false, {}, {}, {});
  auto listener_1 = listenerConfig(l1_name_, all_listeners_, false, {}, {}, {});
  sendLdsResponse({listener_0, listener_1}, listeners_v1_);
  expectWarmingListeners(2);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(all_listeners_, "*")));

  auto direct_response = "pong_from_any_listener_filter_chain_0";
  auto filter_chain =
      filterChainConfig(all_listeners_, fc0_name_, filter_name_, direct_response, {});

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 1, 2);
  expectFilterChainUpdateStats(l1_name_, 1, 2);
  expectListenersUpdateStats(1, 2, 2);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_0, {filter_chain}},
       ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener_1, {filter_chain}}});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
  sendDataVerifyResponse(l1_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, ListenersWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";

  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0, filter_chain_1});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
}

TEST_P(FcdsIntegrationTest, ListenersWithDefaultFilterChainWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0, filter_chain_1});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, ListenersWithStaticFilterChainWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto static_filter_response = "pong_from_listener_0_static_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {},
                                 FilterChainConfig{"listener_0_static_filter_chain", ip_4_,
                                                   filter_name_, static_filter_response},
                                 {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0, filter_chain_1});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, static_filter_response, ip_4_);
}

TEST_P(FcdsIntegrationTest,
       ListenersWithStaticFilterChainWithDefaultFilterChainWithFcdsWithFilterChainMatching) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  auto static_filter_response = "pong_from_listener_0_static_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response,
                                 FilterChainConfig{"listener_0_static_filter_chain", ip_4_,
                                                   filter_name_, static_filter_response},
                                 {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0, filter_chain_1});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, static_filter_response, ip_4_);
  sendDataVerifyResponse(l0_port_, default_response, ip_5_);
}

TEST_P(FcdsIntegrationTest, ListenerWithFcdsWithEcds) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, {});

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);

  FakeStreamPtr ecds_stream = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  auto direct_response = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, direct_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, TwoFcdsUpdates) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "pong_from_listener_0_filter_chain_0";
  auto filter_chain =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);

  auto direct_response_1 = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);

  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain, filter_chain_2});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, FcdsUpdateNoWarming) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, true, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));
  expectListenersUpdateStats(1, 1, 1);

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto direct_response = "pong_from_listener_0_filter_chain_0";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, NoWarmingFcdsUpdateDuringServerInitialization) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto ecds_filter_name = "listener_0_static_filter_chain_0_direct_response";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(
      l0_name_, l0_name_, true, {},
      FilterChainConfig{"listener_0_static_filter_chain", ip_4_, ecds_filter_name, {}}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  FakeStreamPtr ecds_stream = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  expectInitializing();

  auto direct_response = "pong_from_listener_0_filter_chain_0";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain}, true);

  expectInitializing();

  auto ecds_filter_response = "pong_from_listener_0_static_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, ecds_filter_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
  sendDataVerifyResponse(l0_port_, ecds_filter_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, TwoFcdsUpdatesWhileInitializingDependencies) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto ecds_filter_name = "listener_0_static_filter_chain_0_direct_response";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(
      l0_name_, l0_name_, true, {},
      FilterChainConfig{"listener_0_static_filter_chain", ip_4_, ecds_filter_name, {}}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectWarmingListeners(1);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  FakeStreamPtr ecds_stream = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  expectInitializing();

  auto ecds_filter_name_2 = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name_2, {}, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain}, true);

  FakeStreamPtr ecds_stream_2 = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name_2, ecds_stream_2));

  expectInitializing();

  auto direct_response = "pong_from_listener_0_filter_chain_1";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response, ip_3_);

  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain, filter_chain_2},
                   true);

  expectInitializing();

  auto ecds_direct_response = "pong_from_listener_0_static_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, ecds_direct_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);

  auto ecds_direct_response2 = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config_2 = extensionConfig(ecds_filter_name_2, ecds_direct_response2);
  sendExtensionResponse(extension_config_2, ecds_stream_2, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name_2, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain, filter_chain_2});

  sendDataVerifyResponse(l0_port_, ecds_direct_response2, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);
  sendDataVerifyResponse(l0_port_, ecds_direct_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, FcdsUpdateWhilePreviousFcdsUpdateIsInitializing) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, true, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, "", listener, {});

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, {}},
       ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, {filter_chain}, true}});

  FakeStreamPtr ecds_stream = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto direct_response = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response, ip_3_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener, {}},
       ExpectedListenerDump{
           listeners_v1_, filter_chains_v2_, listener, {filter_chain, filter_chain_2}, true}});

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);

  auto ecds_direct_response = "pong_from_listener_0_filter_chain_0_ecds_response";
  auto extension_config = extensionConfig(ecds_filter_name, ecds_direct_response);
  sendExtensionResponse(extension_config, ecds_stream, "ecds_v1");
  expectExtensionReloadStats(ecds_filter_name, 1);
  expectListenersUpdateStats(1, 1, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain, filter_chain_2});

  sendDataVerifyResponse(l0_port_, ecds_direct_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);
}

TEST_P(FcdsIntegrationTest, FcdsUpdateToExistingFilterChainAddedByFcds) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  auto new_direct_response = "listener_0_filter_chain_0_new_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, new_direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectDrainingFilterChains(1);
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain_2});

  sendDataVerifyResponse(l0_port_, new_direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, FcdsWithListenerLevelMatcher) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(
      l0_name_, l0_name_, false, {}, {},
      {{{ip_2_, xdstpResource(l0_name_, fc0_name_)}, {ip_3_, xdstpResource(l0_name_, fc1_name_)}}});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "listener_0_filter_chain_0_direct_response";
  auto direct_response_1 = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_0 = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, {});
  auto filter_Chain_1 = filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, {});

  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_Chain_1});
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0, filter_Chain_1});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);
}

TEST_P(FcdsIntegrationTest, LdsUpdateCreatesNewFcdsSubscription) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  sendLdsResponse(listener, listeners_v2_);
  expectLdsAck();
  expectFcdsAck();
  expectListenersModified(2);
  EXPECT_TRUE(expectFcdsUnsubscribe(xdstpResource(l0_name_, "*")));
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_2 = "listener_0_filter_chain_0_direct_response_new";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_2, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(3);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectDrainingListeners(1);
  expectDrainingListeners(0);
  expectListenersUpdateStats(2, 1, 2);
  expectConfigDump(listeners_v2_, filter_chains_v2_, listener, {filter_chain_2});

  sendDataVerifyResponse(l0_port_, direct_response_2, ip_2_);
}

TEST_P(FcdsIntegrationTest, AdditionalListenerDoesNotImpactExistingListenerWithFcds) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  auto listener_2 = listenerConfig(l1_name_, l1_name_, false, {}, {}, {});
  sendLdsResponse(listener_2, listeners_v2_);
  expectLdsAck();
  expectFcdsAck();
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l1_name_, "*")));

  auto direct_response_2 = "listener_1_filter_chain_0_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l1_name_, fc0_name_, filter_name_, direct_response_2, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 1, 2);
  expectFilterChainUpdateStats(l1_name_, 1, 2);
  expectListenersUpdateStats(2, 2, 2);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, {filter_chain}},
       ExpectedListenerDump{listeners_v2_, filter_chains_v2_, listener_2, {filter_chain_2}}});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
  sendDataVerifyResponse(l1_port_, direct_response_2, ip_2_);
}

TEST_P(FcdsIntegrationTest, ResendSameFcdsConfig) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);

  sendFcdsResponse(filter_chains_v2_, filter_chain);
  expectListenersModified(2);
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain});

  sendDataVerifyResponse(l0_port_, direct_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, TestDrainingScenariosForMultipleFcdsUpdates) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  keep_open_after_response_ = true;
  drain_time_ = std::chrono::seconds(0);
  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, {}, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  std::string direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain});
  auto tcp_client = sendDataVerifyResponse(l0_port_, direct_response, ip_2_, true);

  // Expect that updating the filter chain with different config will cause existing connection to
  // close.
  std::string direct_response_1 = "listener_0_filter_chain_0_direct_response_1";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_1, ip_2_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2);
  expectListenersModified(2);
  tcp_client->waitForDisconnect();
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain_2});

  // Create a new connection to the same listener and expect the new config to be used.
  tcp_client = sendDataVerifyResponse(l0_port_, direct_response_1, ip_2_, true);

  // Expect that sending the same config again will not cause existing connection to close.
  sendFcdsResponse(filter_chains_v1_, filter_chain_2);
  expectListenersModified(3);
  expectListenersUpdateStats(1, 1, 3);
  expectFilterChainUpdateStats(l0_name_, 3, 3);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_2});
  ASSERT_TRUE(tcp_client->write("ping", false, true));

  // Add another filter chain to the listener and expect the connection to be kept alive.
  std::string direct_response_2 = "listener_0_filter_chain_0_direct_response_2";
  auto filter_chain_3 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_2, ip_3_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_3);
  expectListenersModified(4);
  expectListenersUpdateStats(1, 1, 4);
  expectFilterChainUpdateStats(l0_name_, 4, 4);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain_2, filter_chain_3});
  ASSERT_TRUE(tcp_client->write("ping", false, true));

  sendDataVerifyResponse(l0_port_, direct_response_2, ip_3_);

  // Add another listener and expect the connection to be kept alive.
  auto default_response = "pong_from_listener_0_default_filter_chain";
  auto listener_2 = listenerConfig(l1_name_, l1_name_, true, default_response, {}, {});
  sendLdsResponse(listener_2, listeners_v2_);
  expectListenersModified(4);
  expectFilterChainUpdateStats(l0_name_, 4, 4);
  expectFilterChainUpdateStats(l1_name_, 0, 4);
  expectListenersUpdateStats(2, 2, 5);
  expectConfigDump(
      {ExpectedListenerDump{
           listeners_v1_, filter_chains_v2_, listener, {filter_chain_2, filter_chain_3}},
       ExpectedListenerDump{listeners_v2_, "", listener_2, {}}});
  ASSERT_TRUE(tcp_client->write("ping", false, true));

  sendDataVerifyResponse(l0_port_, direct_response_2, ip_3_);
  sendDataVerifyResponse(l1_port_, default_response, ip_3_);
  tcp_client->close();
}

TEST_P(FcdsIntegrationTest, RemoveFilterChainsByFcds) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto default_response = "listener_0_default_filter_chain";
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response_0 = "listener_0_filter_chain_0_direct_response";
  auto direct_response_1 = "listener_0_filter_chain_1_direct_response";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response_0, ip_2_);
  auto filter_chain_1 =
      filterChainConfig(l0_name_, fc1_name_, filter_name_, direct_response_1, ip_3_);
  sendFcdsResponse(filter_chains_v1_, {filter_chain_0, filter_chain_1});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0, filter_chain_1});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response_1, ip_3_);

  auto direct_response_2 = "listener_0_filter_chain_2_direct_response";
  auto filter_chain_2 =
      filterChainConfig(l0_name_, fc2_name_, filter_name_, direct_response_2, ip_4_);
  sendFcdsResponse(filter_chains_v2_, filter_chain_2, {xdstpResource(l0_name_, fc1_name_)});
  expectListenersModified(2);
  expectDrainingFilterChains(1);
  expectListenersUpdateStats(1, 1, 2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectDrainingFilterChains(0);
  expectConfigDump(listeners_v1_, filter_chains_v2_, listener, {filter_chain_0, filter_chain_2});

  sendDataVerifyResponse(l0_port_, direct_response_0, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);
  sendDataVerifyResponse(l0_port_, direct_response_2, ip_4_);

  sendFcdsResponse(filter_chains_v1_, noFilterChains(),
                   {xdstpResource(l0_name_, fc0_name_), xdstpResource(l0_name_, fc2_name_),
                    xdstpResource(l0_name_, "non_existing")});
  expectListenersModified(3);
  expectDrainingFilterChains(2);
  expectListenersUpdateStats(1, 1, 3);
  expectFilterChainUpdateStats(l0_name_, 3, 3);
  expectDrainingFilterChains(0);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {});

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
  sendDataVerifyResponse(l0_port_, default_response, ip_3_);
  sendDataVerifyResponse(l0_port_, default_response, ip_4_);
}

TEST_P(FcdsIntegrationTest, RejectUpdateOrRemoveStaticFilterChainsByFcds) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  EXPECT_TRUE(expectLdsSubscription());
  auto static_filter_response = "pong_from_listener_0_static_filter_chain";
  auto listener = listenerConfig(l0_name_, l0_name_, false, {},
                                 FilterChainConfig{xdstpResource(l0_name_, "static"), ip_2_,
                                                   filter_name_, static_filter_response},
                                 {});
  sendLdsResponse(listener, listeners_v1_);

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto direct_response = "listener_0_filter_chain_0_direct_response";
  auto filter_chain_0 =
      filterChainConfig(l0_name_, fc0_name_, filter_name_, direct_response, ip_3_);
  sendFcdsResponse(filter_chains_v1_, {filter_chain_0});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0});
  expectLdsAck();
  expectFcdsAck();

  sendDataVerifyResponse(l0_port_, static_filter_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);

  auto direct_response_1 = "pong_from_listener_0_static_filter_chain_new";
  auto filter_chain_1 =
      filterChainConfig(l0_name_, "static", filter_name_, direct_response_1, ip_2_);
  sendFcdsResponse(filter_chains_v2_, {filter_chain_1});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0});
  expectFcdsFailure(xdstpResource(l0_name_, "static"));

  sendDataVerifyResponse(l0_port_, static_filter_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);

  sendFcdsResponse(filter_chains_v2_, noFilterChains(), {xdstpResource(l0_name_, "static")});
  expectListenersModified(1);
  expectListenersUpdateStats(1, 1, 1);
  expectFilterChainUpdateStats(l0_name_, 1, 1, 2);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain_0});
  expectFcdsFailure(xdstpResource(l0_name_, "static"), true);

  sendDataVerifyResponse(l0_port_, static_filter_response, ip_2_);
  sendDataVerifyResponse(l0_port_, direct_response, ip_3_);
}

TEST_P(FcdsIntegrationTest, RemoveInitializingFilterChainDuringServerInitialization) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, false, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(listeners_v1_, filter_chains_v1_, listener, {filter_chain}, true);

  FakeStreamPtr ecds_stream = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  sendFcdsResponse(filter_chains_v2_, noFilterChains(), {xdstpResource(l0_name_, fc0_name_)});
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener, {}}});

  expectListenersUpdateStats(1, 1, 1);
  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
}

TEST_P(FcdsIntegrationTest, RemoveInitializingFilterChainAfterServerInitialization) {
  if (ipVersion() == Network::Address::IpVersion::v6 ||
      clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  initialize();
  expectInitializing();

  auto default_response = "pong_from_listener_0_default_filter_chain";
  EXPECT_TRUE(expectLdsSubscription());
  auto listener = listenerConfig(l0_name_, l0_name_, true, default_response, {}, {});
  sendLdsResponse(listener, listeners_v1_);
  expectListenersUpdateStats(1, 1, 1);
  expectConfigDump(listeners_v1_, "", listener, {});

  EXPECT_TRUE(expectFcdsSubscription(xdstpResource(l0_name_, "*")));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  auto ecds_filter_name = "listener_0_filter_chain_0_direct_response";
  auto filter_chain = filterChainConfig(l0_name_, fc0_name_, ecds_filter_name, {}, ip_2_);
  sendFcdsResponse(filter_chains_v1_, filter_chain);
  expectListenersModified(1);
  expectFilterChainUpdateStats(l0_name_, 1, 1);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, {}},
       ExpectedListenerDump{listeners_v1_, filter_chains_v1_, listener, {filter_chain}, true}});

  FakeStreamPtr ecds_stream = waitForNewXdsStream();
  EXPECT_TRUE(expectExtensionSubscription(ecds_filter_name, ecds_stream));

  sendDataVerifyResponse(l0_port_, default_response, ip_2_);

  sendFcdsResponse(filter_chains_v2_, noFilterChains(), {xdstpResource(l0_name_, fc0_name_)});
  expectListenersModified(2);
  expectFilterChainUpdateStats(l0_name_, 2, 2);
  expectConfigDump(
      {ExpectedListenerDump{listeners_v1_, filter_chains_v2_, listener, {}}});
  sendDataVerifyResponse(l0_port_, default_response, ip_2_);
}

} // namespace
} // namespace Envoy
