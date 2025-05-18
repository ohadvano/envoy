#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class FcdsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                 public BaseIntegrationTest {
public:
  FcdsIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseConfig()) {
    skip_tag_extraction_rule_check_ = true;
  }

  // ~FcdsIntegrationTest() override {
  //   resetConnection(ads_connection_);
  // }

  void initialize() override {
    skipPortUsageValidation();
    // defer_listener_finalization_ = true;
    // setUpstreamCount(1);

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* fcds_config = listener->mutable_fcds_config();
      fcds_config->set_name("xdstp://test/envoy.config.listener.v3.FilterChain/foo-listener");

      auto* config_source = fcds_config->mutable_config_source();
      config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);

      // TODO: use ads here for tests?
      auto* api_config_source = config_source->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::V3);

      auto* grpc_service = api_config_source->add_grpc_services();
      setGrpcService(*grpc_service, "ads_cluster", getAdsFakeUpstream().localAddress());
    });

    // config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    //   listener_config_.Swap(bootstrap.mutable_static_resources()->mutable_listeners(0));
    //   listener_config_.set_name(listener_name_);
    //   ENVOY_LOG_MISC(debug, "listener config: {}", listener_config_.DebugString());
    //   bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
    //   auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    //   lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    //   auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
    //   lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    //   lds_api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    //   envoy::config::core::v3::GrpcService* grpc_service =
    //       lds_api_config_source->add_grpc_services();
    //   setGrpcService(*grpc_service, "lds_cluster", getAdsFakeUpstream().localAddress());
    // });

    BaseIntegrationTest::initialize();
    // registerTestServerPorts({port_name_});
  }

  // void resetConnection(FakeHttpConnectionPtr& connection) {
  //   if (connection != nullptr) {
  //     AssertionResult result = connection->close();
  //     RELEASE_ASSERT(result, result.message());
  //     result = connection->waitForDisconnect();
  //     RELEASE_ASSERT(result, result.message());
  //     connection.reset();
  //   }
  // }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    // Create the ADS config discovery upstream (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  // void waitXdsStream() {
  //   // Wait for LDS stream.
  //   auto& ads_upstream = getAdsFakeUpstream();
  //   AssertionResult result = ads_upstream.waitForHttpConnection(*dispatcher_, ads_connection_);
  //   RELEASE_ASSERT(result, result.message());
  //   result = ads_connection_->waitForNewStream(*dispatcher_, ads_stream_);
  //   RELEASE_ASSERT(result, result.message());
  //   ads_stream_->startGrpcStream();

  //   // Response with initial LDS.
  //   sendLdsResponse("initial");
  // }

  // void sendLdsResponse(const std::string& version) {
  //   envoy::service::discovery::v3::DiscoveryResponse response;
  //   response.set_version_info(version);
  //   response.set_type_url(Config::TypeUrl::get().Listener);
  //   response.add_resources()->PackFrom(listener_config_);
  //   ads_stream_->sendGrpcMessage(response);
  // }

  // const std::string port_name_ = "tcp";

  FakeUpstream& getAdsFakeUpstream() const { return *fake_upstreams_[1]; }

  // envoy::config::listener::v3::Listener listener_config_;
  // std::string listener_name_{"testing-listener-0"};
  // FakeHttpConnectionPtr ads_connection_{nullptr};
  // FakeStreamPtr ads_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, FcdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(FcdsIntegrationTest, BasicSuccess) {
  if (ipVersion() == Network::Address::IpVersion::v6 || clientType() == Grpc::ClientType::GoogleGrpc) {
    return;
  }

  // on_server_init_function_ = [&]() { waitXdsStream(); };
  initialize();

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

  // test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  // EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
}

} // namespace
} // namespace Envoy
