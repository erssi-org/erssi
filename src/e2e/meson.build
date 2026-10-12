# this file is part of irssi

# The protocol code without irssi: crypto, JSON, wire format, keyring file,
# handshake, and the send queue. A static library so tests/e2e and the fuzz
# targets (src/fe-fuzz/e2e) use exactly what the module runs.
e2e_proto_sources = files(
  'e2e-crypto.c',
  'e2e-wire.c',
  'e2e-json.c',
  'e2e-keyring.c',
  'e2e-proto.c',
  'e2e-scope.c',
  'e2e-outq.c',
  'e2e-state.c',
)

libe2e_proto_a = static_library('e2e_proto',
  e2e_proto_sources,
  include_directories : rootinc,
  implicit_include_directories : false,
  dependencies : dep,
  pic : true,
)

# noinst_headers = files(
#   'e2e-crypto.h',
#   'e2e-json.h',
#   'e2e-keyring.h',
#   'e2e-outq.h',
#   'e2e-proto.h',
#   'e2e-scope.h',
#   'e2e-wire.h',
#   'module.h',
# )
