# this file is part of irssi

# OpenSSL dependency is provided by parent meson.build (when have_fe_web = true)

shared_module('fe_web',
  files(
    'fe-web.c',
    'fe-web-client.c',
    'fe-web-server.c',
    'fe-web-signals.c',
    'fe-web-utils.c',
    'fe-web-json.c',
    'fe-web-websocket.c',
    'fe-web-ssl.c',
    'fe-web-crypto.c',
    'fe-web-netserver.c',
  )
  + [ irssi_version_h ],
  include_directories : rootinc,
  implicit_include_directories : false,
  name_suffix : module_suffix,
  install : true,
  install_dir : moduledir,
  dependencies : [dep, openssl_dep],
)

# noinst_headers = files(
#   'fe-web.h',
#   'module.h',
# )
