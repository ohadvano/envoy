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
    name = "config_test",
    size = "small",
    srcs = select({
        "//bazel:linux": ["config_test.cc"],
        "//bazel:darwin_any": ["config_test.cc"],
        "//conditions:default": [],
    }),
    data = [
        "//test/extensions/geoip_providers/maxmind/test_data:geolocation_databases",
    ],
    extension_names = ["envoy.geoip_providers.maxmind"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/geoip_providers/maxmind:config",
        "//source/extensions/geoip_providers/maxmind:provider_impl",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/geoip_providers/maxmind/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "geoip_provider_test",
    size = "small",
    srcs = select({
        "//bazel:linux": ["geoip_provider_test.cc"],
        "//bazel:darwin_any": ["geoip_provider_test.cc"],
        "//conditions:default": [],
    }),
    data = [
        "//test/extensions/geoip_providers/maxmind/test_data:geolocation_databases",
    ],
    extension_names = ["envoy.geoip_providers.maxmind"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/registry",
        "//source/extensions/geoip_providers/maxmind:config",
        "//source/extensions/geoip_providers/maxmind:provider_impl",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/geoip_providers/maxmind/v3:pkg_cc_proto",
    ],
)
