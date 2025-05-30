#include <memory>

#include "source/extensions/load_balancing_policies/random/config.h"
#include "source/extensions/load_balancing_policies/random/random_lb.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_fuzz_base.h"
#include "test/extensions/load_balancing_policies/random/random_load_balancer_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::RandomLoadBalancerTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  LoadBalancerFuzzBase load_balancer_fuzz = LoadBalancerFuzzBase();
  load_balancer_fuzz.initializeLbComponents(input.load_balancer_test_case());

  try {
    Extensions::LoadBalancingPolices::Random::TypedRandomLbConfig config(
        input.load_balancer_test_case().common_lb_config());
    const auto threshold = PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
        input.load_balancer_test_case().common_lb_config(), healthy_panic_threshold, 100, 50);

    load_balancer_fuzz.lb_ = std::make_unique<RandomLoadBalancer>(
        load_balancer_fuzz.priority_set_, nullptr, load_balancer_fuzz.stats_,
        load_balancer_fuzz.runtime_, load_balancer_fuzz.random_, threshold, config.lb_config_);
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException; {}", e.what());
    return;
  }

  load_balancer_fuzz.replay(input.load_balancer_test_case().actions());
}

} // namespace Upstream
} // namespace Envoy
