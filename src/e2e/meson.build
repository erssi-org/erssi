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

# the module: /load e2e, or "e2e" in autoload_modules; the live interop
# test depends on it
libe2e_core_sm = shared_library('e2e_core',
  files(
    'e2e-commands.c',
    'e2e-core.c',
    'e2e-formats.c',
    'e2e-statusbar.c',
    'e2e-web.c',
  ),
  link_with : libe2e_proto_a,
  include_directories : rootinc,
  implicit_include_directories : false,
  name_suffix : module_suffix,
  install : true,
  install_dir : moduledir,
  dependencies : dep,
  override_options : ['b_lundef=false'],
)

# noinst_headers = files(
#   'e2e-core.h',
#   'e2e-crypto.h',
#   'e2e-formats.h',
#   'e2e-json.h',
#   'e2e-keyring.h',
#   'e2e-outq.h',
#   'e2e-proto.h',
#   'e2e-scope.h',
#   'e2e-statusbar.h',
#   'e2e-web.h',
#   'e2e-wire.h',
#   'module.h',
# )
