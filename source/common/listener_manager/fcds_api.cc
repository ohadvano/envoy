#include "source/common/listener_manager/fcds_api.h"

#include "source/common/grpc/common.h"

#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Server {

FcdsApiImpl::FcdsApiImpl(const envoy::config::core::v3::ConfigSource& fcds_config,
                        absl::string_view fcds_config_name, absl::string_view listener_name,
                        Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                        Stats::Scope& scope, ListenerManager& listener_manager,
                        ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>(validation_visitor,
                                                                             "name"),
      fcds_config_name_(fcds_config_name), listener_name_(listener_name),
      cluster_manager_(cluster_manager), scope_(scope.createScope("listener_manager.fcds.")),
      listener_manager_(listener_manager),
      listener_init_target_("FCDS", [this]() {
        initialize();
        init_target_.ready();
      }),
      init_target_("FCDS", [this]() {
        if (initialized_) {
          init_target_.ready();
        }
      }) {
  const auto resource_name = getResourceName();
  subscription_ = THROW_OR_RETURN_VALUE(
    cluster_manager.subscriptionFactory().subscriptionFromConfigSource(
      fcds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {}),
    Config::SubscriptionPtr);

  init_manager.add(listener_init_target_);
  UNREFERENCED_PARAMETER(listener_manager_);
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

  // listener_manager_.updateFilterChains(listener_name, added, removed);
  initialized_ = true;
  init_target_.ready();

  return absl::OkStatus();
}

absl::Status FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                         const std::string& version_info) {
  absl::node_hash_set<std::string> filter_chains_to_remove;

  // filter_chains_to_remove.insert(
  //    listener_manager_.getFilterChainNames(
  //          listener_name_, ListenerManager::WARMING | ListenerManager::ACTIVE));

  for (const auto& resource : resources) {
    filter_chains_to_remove.erase(resource.get().name());
  }

  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (const auto& filter_chain : filter_chains_to_remove) {
    *to_remove_repeated.Add() = filter_chain;
  }

  initialized_ = true;
  init_target_.ready();

  return onConfigUpdate(resources, to_remove_repeated, version_info);
}

void FcdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                       const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

void FcdsApiImpl::initialize() {
  subscription_->start({fcds_config_name_});
}

} // namespace Server
} // namespace Envoy
