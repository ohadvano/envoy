#include "source/common/listener_manager/fcds_api.h"

#include "source/common/grpc/common.h"

#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Server {

FcdsApiImpl::FcdsApiImpl(const envoy::config::core::v3::ConfigSource& fcds_config,
                        absl::string_view fcds_config_name, absl::string_view listener_name,
                        Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                        ListenerManager& listener_manager,
                        ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>(validation_visitor,
                                                                             "name"),
      fcds_config_name_(fcds_config_name), listener_name_(listener_name),
      cluster_manager_(cluster_manager), scope_(scope.createScope("listener_manager.fcds.")),
      listener_manager_(listener_manager),
      init_target_("FCDS", [this]() { subscription_->start({fcds_config_name_}); }) {
  const auto resource_name = getResourceName();
  subscription_ = THROW_OR_RETURN_VALUE(
    cluster_manager.subscriptionFactory().subscriptionFromConfigSource(
      fcds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {}),
    Config::SubscriptionPtr);
}

absl::Status
FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>&,
                            const Protobuf::RepeatedPtrField<std::string>&,
                            const std::string&) {
  Config::ScopedResume maybe_resume_rds_sds;
  if (cluster_manager_.adsMux()) {
    const std::vector<std::string> paused_xds_types{
        // Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(),
        // Config::getTypeUrl<envoy::config::route::v3::ScopedRouteConfiguration>(),
        Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>()};
    maybe_resume_rds_sds = cluster_manager_.adsMux()->pause(paused_xds_types);
  }

  UNREFERENCED_PARAMETER(listener_manager_);
  // listener_manager_.updateFilterChains(listener_name, added, removed);
  init_target_.ready();

  return absl::OkStatus();
}

absl::Status FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>&,
                                         const std::string&) {
  return absl::UnavailableError("SoTW FCDS is not implemented");
}

void FcdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                       const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

} // namespace Server
} // namespace Envoy
