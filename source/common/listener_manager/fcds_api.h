#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/init/manager.h"
#include "envoy/server/listener_manager.h"

#include "source/common/config/subscription_base.h"
#include "source/common/init/target_impl.h"

namespace Envoy {
namespace Server {

/**
 * FCDS API implementation that fetches via Subscription.
 */
class FcdsApiImpl : public FcdsApi,
                    Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  FcdsApiImpl(const envoy::config::core::v3::ConfigSource& fcds_config,
              absl::string_view fcds_config_name, absl::string_view listener_name,
              Upstream::ClusterManager& cm, Init::Manager& init_manager, Stats::Scope& scope,
              ListenerManager& lm, ProtobufMessage::ValidationVisitor& validation_visitor);

  // Server::LdsApi
  std::string versionInfo() const override { return system_version_info_; }

  Init::TargetImpl& initTarget() { return init_target_; }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  void initialize();

  bool initialized_{false};
  const std::string fcds_config_name_;
  const std::string listener_name_;
  Config::SubscriptionPtr subscription_;
  std::string system_version_info_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::ScopeSharedPtr scope_;
  ListenerManager& listener_manager_;
  Init::SharedTargetImpl listener_init_target_;
  Init::TargetImpl init_target_;
};

} // namespace Server
} // namespace Envoy
