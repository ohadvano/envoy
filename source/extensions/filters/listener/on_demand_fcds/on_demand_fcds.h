#pragma once

#include "envoy/extensions/filters/listener/on_demand_fcds/v3/on_demand_fcds.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/listener_manager.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OnDemandFcds {

static const uint32_t DEFAULT_DISCOVERY_TIMEOUT = 5000;
constexpr absl::string_view PerConnectionResourcesLocator =
    "envoy.on_demand_fcds.per_connection_resources_locator";

class FilterConfig : Logger::Loggable<Logger::Id::filter> {
public:
  FilterConfig(
      const envoy::extensions::filters::listener::on_demand_fcds::v3::FilterConfig& proto_config)
      : config_source_(proto_config.config_source()),
        resources_locator_(proto_config.resources_locator()),
        timeout_(std::chrono::milliseconds(
            PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, DEFAULT_DISCOVERY_TIMEOUT))),
        continue_on_failure_(proto_config.continue_on_failure()) {}

  ~FilterConfig() = default;

  const envoy::config::core::v3::ConfigSource& configSource() const { return config_source_; }
  const std::string& resourcesLocator() const { return resources_locator_; }
  const std::chrono::milliseconds& timeout() const { return timeout_; }
  bool continueOnFailure() const { return continue_on_failure_; }

  const envoy::config::core::v3::ConfigSource config_source_;
  const std::string resources_locator_;
  const std::chrono::milliseconds timeout_;
  const bool continue_on_failure_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * On-Demand FCDS listener filter.
 */
class Filter : public Network::ListenerFilter,
               public Server::OnDemandFcdsDiscoveryCallbacks,
               Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const FilterConfigSharedPtr& config) : config_(config) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  }
  size_t maxReadBytes() const override { return 0; }

  // Server::OnDemandFcdsDiscoveryCallbacks
  void onSuccess(std::vector<std::string> discovered_filter_chains) override;
  void onFailure(Server::OnDemandFcdsDiscoveryFailure failure_reason) override;

private:
  FilterConfigSharedPtr config_;
  Server::OnDemandFcdsDiscoveryHandlePtr handle_;
  Network::ListenerFilterCallbacks* callbacks_{nullptr};
};

} // namespace OnDemandFcds
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
