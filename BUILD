filegroup(
  name = "path_bins",
  srcs = [":t-digest"],
  visibility = ["//visibility:public"],
)

cc_binary(
  name = "t-digest",
  srcs = ["main.c"],
  deps = [":libtdigest"],
)

cc_library(
  name = "libtdigest",
  srcs = ["t-digest.c"],
  hdrs = ["t-digest.h"],
  visibility = ["//visibility:public"],
  deps = [
    "//data/serde",
  ],
)
