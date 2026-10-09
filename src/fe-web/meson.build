# this file is part of irssi

# OpenSSL dependency is provided by parent meson.build (when have_fe_web = true)

# fe-web-server.c apart: the fuzz targets (src/fe-fuzz/fe-web) compile it
# into themselves to reach its static input functions
fe_web_sources = files(
  'fe-web.c',
  'fe-web-client.c',
  'fe-web-signals.c',
  'fe-web-utils.c',
  'fe-web-json.c',
  'fe-web-websocket.c',
  'fe-web-ssl.c',
  'fe-web-crypto.c',
  'fe-web-netserver.c',
)

shared_library('fe_web',
  fe_web_sources
  + files('fe-web-server.c')
  + [ irssi_version_h ],
  include_directories : rootinc,
  implicit_include_directories : false,
  name_suffix : module_suffix,
  install : true,
  install_dir : moduledir,
  dependencies : [dep, openssl_dep],
  override_options : ['b_lundef=false'],
)

# noinst_headers = files(
#   'fe-web.h',
#   'module.h',
# )
