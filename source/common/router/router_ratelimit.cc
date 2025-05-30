#include "source/common/router/router_ratelimit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Router {

namespace {
bool populateDescriptor(const std::vector<RateLimit::DescriptorProducerPtr>& actions,
                        std::vector<RateLimit::DescriptorEntry>& descriptor_entries,
                        const std::string& local_service_cluster,
                        const Http::RequestHeaderMap& headers, const StreamInfo::StreamInfo& info) {
  bool result = true;
  for (const RateLimit::DescriptorProducerPtr& action : actions) {
    RateLimit::DescriptorEntry descriptor_entry;
    result = result &&
             action->populateDescriptor(descriptor_entry, local_service_cluster, headers, info);
    if (!result) {
      break;
    }
    if (!descriptor_entry.key_.empty()) {
      descriptor_entries.push_back(descriptor_entry);
    }
  }
  return result;
}

} // namespace

// Ratelimit::DescriptorProducer
bool MatchInputRateLimitDescriptor::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                                       const std::string&,
                                                       const Http::RequestHeaderMap& headers,
                                                       const StreamInfo::StreamInfo& info) const {
  Http::Matching::HttpMatchingDataImpl data(info);
  data.onRequestHeaders(headers);
  auto result = data_input_->get(data);
  if (!absl::holds_alternative<std::string>(result.data_)) {
    return false;
  }
  if (absl::string_view str = absl::get<std::string>(result.data_); !str.empty()) {
    descriptor_entry = {descriptor_key_, std::string(str)};
  }
  return true;
}

bool DynamicMetadataRateLimitOverride::populateOverride(
    RateLimit::Descriptor& descriptor, const envoy::config::core::v3::Metadata* metadata) const {
  const ProtobufWkt::Value& metadata_value =
      Envoy::Config::Metadata::metadataValue(metadata, metadata_key_);
  if (metadata_value.kind_case() != ProtobufWkt::Value::kStructValue) {
    return false;
  }

  const auto& override_value = metadata_value.struct_value().fields();
  const auto& limit_it = override_value.find("requests_per_unit");
  const auto& unit_it = override_value.find("unit");
  if (limit_it != override_value.end() &&
      limit_it->second.kind_case() == ProtobufWkt::Value::kNumberValue &&
      unit_it != override_value.end() &&
      unit_it->second.kind_case() == ProtobufWkt::Value::kStringValue) {
    envoy::type::v3::RateLimitUnit unit;
    if (envoy::type::v3::RateLimitUnit_Parse(unit_it->second.string_value(), &unit)) {
      descriptor.limit_.emplace(RateLimit::RateLimitOverride{
          static_cast<uint32_t>(limit_it->second.number_value()), unit});
      return true;
    }
  }
  return false;
}

bool SourceClusterAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                             const std::string& local_service_cluster,
                                             const Http::RequestHeaderMap&,
                                             const StreamInfo::StreamInfo&) const {
  descriptor_entry = {"source_cluster", local_service_cluster};
  return true;
}

bool DestinationClusterAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                                  const std::string&, const Http::RequestHeaderMap&,
                                                  const StreamInfo::StreamInfo& info) const {
  if (info.route() == nullptr || info.route()->routeEntry() == nullptr) {
    return false;
  }
  descriptor_entry = {"destination_cluster", info.route()->routeEntry()->clusterName()};
  return true;
}

bool RequestHeadersAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                              const std::string&,
                                              const Http::RequestHeaderMap& headers,
                                              const StreamInfo::StreamInfo&) const {
  const auto header_value = headers.get(header_name_);

  // If header is not present in the request and if skip_if_absent is true skip this descriptor,
  // while calling rate limiting service. If skip_if_absent is false, do not call rate limiting
  // service.
  if (header_value.empty()) {
    return skip_if_absent_;
  }
  // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially populate all header values.
  descriptor_entry = {descriptor_key_, std::string(header_value[0]->value().getStringView())};
  return true;
}

bool RemoteAddressAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                             const std::string&, const Http::RequestHeaderMap&,
                                             const StreamInfo::StreamInfo& info) const {
  const Network::Address::InstanceConstSharedPtr& remote_address =
      info.downstreamAddressProvider().remoteAddress();
  if (remote_address->type() != Network::Address::Type::Ip) {
    return false;
  }

  descriptor_entry = {"remote_address", remote_address->ip()->addressAsString()};

  return true;
}

bool MaskedRemoteAddressAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                                   const std::string&,
                                                   const Http::RequestHeaderMap&,
                                                   const StreamInfo::StreamInfo& info) const {
  const Network::Address::InstanceConstSharedPtr& remote_address =
      info.downstreamAddressProvider().remoteAddress();
  if (remote_address->type() != Network::Address::Type::Ip) {
    return false;
  }

  uint32_t mask_len = v4_prefix_mask_len_;
  if (remote_address->ip()->version() == Network::Address::IpVersion::v6) {
    mask_len = v6_prefix_mask_len_;
  }

  // TODO: increase the efficiency, avoid string transform back and forth
  // Note: we don't do validity checking for CIDR range here because we know
  // from addressAsString this is a valid address.
  Network::Address::CidrRange cidr_entry =
      *Network::Address::CidrRange::create(remote_address->ip()->addressAsString(), mask_len);
  descriptor_entry = {"masked_remote_address", cidr_entry.asString()};

  return true;
}

bool GenericKeyAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                          const std::string&, const Http::RequestHeaderMap&,
                                          const StreamInfo::StreamInfo&) const {
  descriptor_entry = {descriptor_key_, descriptor_value_};
  return true;
}

MetaDataAction::MetaDataAction(const envoy::config::route::v3::RateLimit::Action::MetaData& action)
    : metadata_key_(action.metadata_key()), descriptor_key_(action.descriptor_key()),
      default_value_(action.default_value()), source_(action.source()),
      skip_if_absent_(action.skip_if_absent()) {}

MetaDataAction::MetaDataAction(
    const envoy::config::route::v3::RateLimit::Action::DynamicMetaData& action)
    : metadata_key_(action.metadata_key()), descriptor_key_(action.descriptor_key()),
      default_value_(action.default_value()),
      source_(envoy::config::route::v3::RateLimit::Action::MetaData::DYNAMIC),
      skip_if_absent_(false) {}

bool MetaDataAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                        const std::string&, const Http::RequestHeaderMap&,
                                        const StreamInfo::StreamInfo& info) const {
  const envoy::config::core::v3::Metadata* metadata_source;

  switch (source_) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::route::v3::RateLimit::Action::MetaData::DYNAMIC:
    metadata_source = &info.dynamicMetadata();
    break;
  case envoy::config::route::v3::RateLimit::Action::MetaData::ROUTE_ENTRY:
    metadata_source = &info.route()->metadata();
    break;
  }

  const std::string metadata_string_value =
      Envoy::Config::Metadata::metadataValue(metadata_source, metadata_key_).string_value();

  if (!metadata_string_value.empty()) {
    descriptor_entry = {descriptor_key_, metadata_string_value};
    return true;
  } else if (metadata_string_value.empty() && !default_value_.empty()) {
    descriptor_entry = {descriptor_key_, default_value_};
    return true;
  }

  // If the metadata key is not present and no default value is set, skip this
  // descriptor if skip_if_absent is true. If skip_if_absent is false, do not
  // call rate limiting service.
  return skip_if_absent_;
}

QueryParametersAction::QueryParametersAction(
    const envoy::config::route::v3::RateLimit::Action::QueryParameters& action)
    : query_param_name_(action.query_parameter_name()),
      descriptor_key_(!action.descriptor_key().empty() ? action.descriptor_key() : "query_param"),
      skip_if_absent_(action.skip_if_absent()) {}

bool QueryParametersAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                               const std::string&,
                                               const Http::RequestHeaderMap& headers,
                                               const StreamInfo::StreamInfo&) const {
  Http::Utility::QueryParamsMulti query_parameters =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());

  const absl::optional<std::string> query_param_value =
      query_parameters.getFirstValue(query_param_name_);

  // If query parameter is not present and ``skip_if_absent`` is ``true``, skip this descriptor.
  // If ``skip_if_absent`` is ``false``, do not call rate limiting service.
  if (!query_param_value.has_value()) {
    return skip_if_absent_;
  }

  descriptor_entry = {descriptor_key_, query_param_value.value()};
  return true;
}

HeaderValueMatchAction::HeaderValueMatchAction(
    const envoy::config::route::v3::RateLimit::Action::HeaderValueMatch& action,
    Server::Configuration::CommonFactoryContext& context)
    : descriptor_value_(action.descriptor_value()),
      descriptor_key_(!action.descriptor_key().empty() ? action.descriptor_key() : "header_match"),
      expect_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, expect_match, true)),
      action_headers_(Http::HeaderUtility::buildHeaderDataVector(action.headers(), context)) {}

bool HeaderValueMatchAction::populateDescriptor(RateLimit::DescriptorEntry& descriptor_entry,
                                                const std::string&,
                                                const Http::RequestHeaderMap& headers,
                                                const StreamInfo::StreamInfo&) const {
  if (expect_match_ == Http::HeaderUtility::matchHeaders(headers, action_headers_)) {
    descriptor_entry = {descriptor_key_, descriptor_value_};
    return true;
  } else {
    return false;
  }
}

QueryParameterValueMatchAction::QueryParameterValueMatchAction(
    const envoy::config::route::v3::RateLimit::Action::QueryParameterValueMatch& action,
    Server::Configuration::CommonFactoryContext& context)
    : descriptor_value_(action.descriptor_value()),
      descriptor_key_(!action.descriptor_key().empty() ? action.descriptor_key() : "query_match"),
      expect_match_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(action, expect_match, true)),
      action_query_parameters_(
          buildQueryParameterMatcherVector(action.query_parameters(), context)) {}

bool QueryParameterValueMatchAction::populateDescriptor(
    RateLimit::DescriptorEntry& descriptor_entry, const std::string&,
    const Http::RequestHeaderMap& headers, const StreamInfo::StreamInfo&) const {
  Http::Utility::QueryParamsMulti query_parameters =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
  if (expect_match_ ==
      ConfigUtility::matchQueryParams(query_parameters, action_query_parameters_)) {
    descriptor_entry = {descriptor_key_, descriptor_value_};
    return true;
  } else {
    return false;
  }
}

std::vector<ConfigUtility::QueryParameterMatcherPtr>
QueryParameterValueMatchAction::buildQueryParameterMatcherVector(
    const Protobuf::RepeatedPtrField<envoy::config::route::v3::QueryParameterMatcher>&
        query_parameters,
    Server::Configuration::CommonFactoryContext& context) {
  std::vector<ConfigUtility::QueryParameterMatcherPtr> ret;
  for (const auto& query_parameter : query_parameters) {
    ret.push_back(std::make_unique<ConfigUtility::QueryParameterMatcher>(query_parameter, context));
  }
  return ret;
}

RateLimitPolicyEntryImpl::RateLimitPolicyEntryImpl(
    const envoy::config::route::v3::RateLimit& config,
    Server::Configuration::CommonFactoryContext& context, absl::Status& creation_status)
    : disable_key_(config.disable_key()),
      stage_(static_cast<uint64_t>(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, stage, 0))),
      apply_on_stream_done_(config.apply_on_stream_done()) {
  for (const auto& action : config.actions()) {
    switch (action.action_specifier_case()) {
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kSourceCluster:
      actions_.emplace_back(new SourceClusterAction());
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kDestinationCluster:
      actions_.emplace_back(new DestinationClusterAction());
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kQueryParameters:
      actions_.emplace_back(new QueryParametersAction(action.query_parameters()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kRequestHeaders:
      actions_.emplace_back(new RequestHeadersAction(action.request_headers()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kRemoteAddress:
      actions_.emplace_back(new RemoteAddressAction());
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kGenericKey:
      actions_.emplace_back(new GenericKeyAction(action.generic_key()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kDynamicMetadata:
      actions_.emplace_back(new MetaDataAction(action.dynamic_metadata()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kMetadata:
      actions_.emplace_back(new MetaDataAction(action.metadata()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kHeaderValueMatch:
      actions_.emplace_back(new HeaderValueMatchAction(action.header_value_match(), context));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kExtension: {
      ProtobufMessage::ValidationVisitor& validator = context.messageValidationVisitor();
      auto* factory = Envoy::Config::Utility::getFactory<RateLimit::DescriptorProducerFactory>(
          action.extension());
      if (!factory) {
        // If no descriptor extension is found, fallback to using HTTP matcher
        // input functions. Note that if the same extension name or type was
        // dual registered as an extension descriptor and an HTTP matcher input
        // function, the descriptor extension takes priority.
        RateLimitDescriptorValidationVisitor validation_visitor;
        Matcher::MatchInputFactory<Http::HttpMatchingData> input_factory(validator,
                                                                         validation_visitor);
        Matcher::DataInputFactoryCb<Http::HttpMatchingData> data_input_cb =
            input_factory.createDataInput(action.extension());
        actions_.emplace_back(std::make_unique<MatchInputRateLimitDescriptor>(
            action.extension().name(), data_input_cb()));
        break;
      }
      auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
          action.extension().typed_config(), validator, *factory);
      absl::StatusOr<RateLimit::DescriptorProducerPtr> producer_or =
          factory->createDescriptorProducerFromProto(*message, context);
      SET_AND_RETURN_IF_NOT_OK(producer_or.status(), creation_status);
      actions_.emplace_back(std::move(producer_or.value()));
      break;
    }
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::kMaskedRemoteAddress:
      actions_.emplace_back(new MaskedRemoteAddressAction(action.masked_remote_address()));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::
        kQueryParameterValueMatch:
      actions_.emplace_back(
          new QueryParameterValueMatchAction(action.query_parameter_value_match(), context));
      break;
    case envoy::config::route::v3::RateLimit::Action::ActionSpecifierCase::ACTION_SPECIFIER_NOT_SET:
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }
  if (config.has_limit()) {
    switch (config.limit().override_specifier_case()) {
    case envoy::config::route::v3::RateLimit_Override::OverrideSpecifierCase::kDynamicMetadata:
      limit_override_.emplace(
          new DynamicMetadataRateLimitOverride(config.limit().dynamic_metadata()));
      break;
    case envoy::config::route::v3::RateLimit_Override::OverrideSpecifierCase::
        OVERRIDE_SPECIFIER_NOT_SET:
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }
}

void RateLimitPolicyEntryImpl::populateDescriptors(std::vector<RateLimit::Descriptor>& descriptors,
                                                   const std::string& local_service_cluster,
                                                   const Http::RequestHeaderMap& headers,
                                                   const StreamInfo::StreamInfo& info) const {
  RateLimit::Descriptor descriptor;
  const bool result =
      populateDescriptor(actions_, descriptor.entries_, local_service_cluster, headers, info);

  if (limit_override_) {
    limit_override_.value()->populateOverride(descriptor, &info.dynamicMetadata());
  }

  if (result) {
    descriptors.emplace_back(descriptor);
  }
}

void RateLimitPolicyEntryImpl::populateLocalDescriptors(
    std::vector<Envoy::RateLimit::LocalDescriptor>& descriptors,
    const std::string& local_service_cluster, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& info) const {
  RateLimit::LocalDescriptor descriptor({});
  const bool result =
      populateDescriptor(actions_, descriptor.entries_, local_service_cluster, headers, info);
  if (result) {
    descriptors.emplace_back(descriptor);
  }
}

namespace {
// Initializes the ``RateLimitPolicyImpl::rate_limit_entries_`` vector.
std::vector<RateLimitPolicyEntryImpl> initRateLimitEntries(
    const Protobuf::RepeatedPtrField<envoy::config::route::v3::RateLimit>& rate_limits,
    Server::Configuration::CommonFactoryContext& context, absl::Status& creation_status) {
  std::vector<RateLimitPolicyEntryImpl> entries;
  entries.reserve(rate_limits.size());
  for (const auto& rate_limit : rate_limits) {
    entries.emplace_back(rate_limit, context, creation_status);
    // If the entry is invalid, the function returns no policy entries, and
    // changes the creation_status to the error.
    if (!creation_status.ok()) {
      return {};
    }
  }
  return entries;
}

// The maximum stage number supported. This value should match the maximum stage number in
// Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA and
// Json::Schema::RATE_LIMIT_HTTP_FILTER_SCHEMA from common/json/config_schemas.cc.
static constexpr uint64_t MAX_STAGE_NUMBER = 10UL;

// Initializes the ``RateLimitPolicyImpl::rate_limit_entries_reference_`` vector.
std::vector<std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>>
initRateLimitEntriesReference(const std::vector<RateLimitPolicyEntryImpl>& rate_limit_entries) {
  std::vector<std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>> references(
      MAX_STAGE_NUMBER + 1);
  for (const auto& entry : rate_limit_entries) {
    const uint64_t stage = entry.stage();
    // The stage value is validated by PGV.
    ASSERT(stage < references.size());
    references[stage].emplace_back(std::ref(entry));
  }
  return references;
}
} // namespace

RateLimitPolicyImpl::RateLimitPolicyImpl(
    const Protobuf::RepeatedPtrField<envoy::config::route::v3::RateLimit>& rate_limits,
    Server::Configuration::CommonFactoryContext& context, absl::Status& creation_status)
    : rate_limit_entries_(initRateLimitEntries(rate_limits, context, creation_status)),
      rate_limit_entries_reference_(initRateLimitEntriesReference(rate_limit_entries_)) {}

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
RateLimitPolicyImpl::getApplicableRateLimit(uint64_t stage) const {
  ASSERT(stage < rate_limit_entries_reference_.size());
  return rate_limit_entries_reference_[stage];
}

} // namespace Router
} // namespace Envoy
