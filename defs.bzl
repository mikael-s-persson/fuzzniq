load("@bazel_cc_meta//cc_meta:cc_meta.bzl", "cc_meta_aspect_factory")

fuzzniq_cc_meta_aspect = cc_meta_aspect_factory(
    deviations = [Label("//:fuzzniq_cc_meta_deviations")],
)
