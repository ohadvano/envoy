#include <string>

#include "envoy/extensions/filters/listener/on_demand_fcds/v3/on_demand_fcds.pb.h"
#include "envoy/extensions/filters/listener/on_demand_fcds/v3/on_demand_fcds.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/on_demand_fcds/on_demand_fcds.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OnDemandFcds {

/**
 * Config registration for the On-Demand FCDS filter. @see NamedNetworkFilterConfigFactory.
 */
class OnDemandFcdsConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {

    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::on_demand_fcds::v3::FilterConfig&>(
        message, context.messageValidationVisitor());

    FilterConfigSharedPtr config = std::make_shared<FilterConfig>(proto_config);
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::on_demand_fcds::v3::FilterConfig>();
  }

  std::string name() const override { return "envoy.filters.listener.on_demand_fcds"; }
};

/**
 * Static registration for the On-Demand FCDS filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OnDemandFcdsConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace OnDemandFcds
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
