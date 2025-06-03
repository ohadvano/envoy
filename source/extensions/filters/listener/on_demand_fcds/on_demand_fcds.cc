#include "source/extensions/filters/listener/on_demand_fcds/on_demand_fcds.h"

#include "envoy/router/string_accessor.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OnDemandFcds {

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& callbacks) {
  ENVOY_LOG(trace, "on-demand FCDS onAccept");
  callbacks_ = &callbacks;

  const auto* dynamic_resources_locator =
      callbacks_->filterState().getDataReadOnly<Router::StringAccessor>(
          PerConnectionResourcesLocator);

  std::string resources_locator;
  if (dynamic_resources_locator) {
    resources_locator = dynamic_resources_locator->asString();
  } else {
    resources_locator = config_->resourcesLocator();
  }

  auto result = callbacks_->requestOnDemandFilterChainDiscovery(
      config_->configSource(), resources_locator, *this, config_->timeout());

  switch (result.status_) {
  case Server::OnDemandFcdsDiscoveryStatus::Warmed: {
    ASSERT(result.handle_ == nullptr);
    ENVOY_LOG(debug, "filter chain collection is already discovered");
    return Network::FilterStatus::Continue;
  }
  case Server::OnDemandFcdsDiscoveryStatus::Loading: {
    ASSERT(result.handle_ != nullptr);
    handle_ = std::move(result.handle_);
    ENVOY_LOG(debug, "on-demand filter chain discovery is in progress");
    return Network::FilterStatus::StopIteration;
  }
  case Server::OnDemandFcdsDiscoveryStatus::RequestError: {
    ASSERT(result.handle_ == nullptr);
    ENVOY_LOG(error, "on-demand filter chain discovery request is malformed");
    if (config_->continueOnFailure()) {
      ENVOY_LOG(debug, "continuing filter chain despite request error");
      return Network::FilterStatus::Continue;
    } else {
      callbacks.socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    }
  }
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

void Filter::onSuccess(std::vector<std::string> filter_chain_names) {
  ENVOY_LOG(debug, "on-demand filter chain discovery is successful; discovered {} filter chains",
            filter_chain_names.size());
  handle_.reset();
  callbacks_->continueFilterChain(true);
};

void Filter::onFailure(Server::OnDemandFcdsDiscoveryFailure failure_reason) {
  ENVOY_LOG(debug, "on-demand filter chain discovery failed with status: {}",
            static_cast<uint32_t>(failure_reason));
  handle_.reset();

  if (config_->continueOnFailure()) {
    ENVOY_LOG(debug, "continuing filter chain despite failure");
    callbacks_->continueFilterChain(true);
    return;
  }

  callbacks_->socket().ioHandle().close();
  callbacks_->continueFilterChain(false);
};

} // namespace OnDemandFcds
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
