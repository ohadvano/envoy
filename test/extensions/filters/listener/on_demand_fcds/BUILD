load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "on_demand_fcds_integration_test",
    size = "small",
    srcs = [
        "on_demand_fcds_integration_test.cc",
    ],
    extension_names = ["envoy.filters.listener.on_demand_fcds"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/listener/on_demand_fcds:config",
        "//test/integration:fcds_integration_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)
