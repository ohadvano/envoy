load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_android//android:rules.bzl", "android_binary")
load("@rules_jvm_external//:defs.bzl", "artifact")
load("@rules_kotlin//kotlin:android.bzl", "kt_android_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_binary(
    name = "hello_envoy",
    custom_package = "io.envoyproxy.envoymobile.helloenvoy",
    manifest = "AndroidManifest.xml",
    proguard_specs = ["//library:proguard_rules"],
    deps = [
        "hello_envoy_java_lib",
    ],
)

# We'll use the kotlin rule here to avoid having an explicit dependency on
# kotlin runtime.
kt_android_library(
    name = "hello_envoy_java_lib",
    srcs = [
        "EmptyFile.kt",  # Needed for the kotlin rule
        "MainActivity.java",
    ],
    custom_package = "io.envoyproxy.envoymobile.helloenvoy",
    manifest = "AndroidManifest.xml",
    resource_files = [
        "res/layout/activity_main.xml",
    ],
    deps = [
        "//:envoy_mobile_android",
        "//examples/kotlin/shared:hello_envoy_shared_lib",
        artifact("androidx.recyclerview:recyclerview"),
        artifact("androidx.annotation:annotation"),
        artifact("com.google.code.findbugs:jsr305"),
        artifact("com.google.protobuf:protobuf-javalite"),
    ],
)
