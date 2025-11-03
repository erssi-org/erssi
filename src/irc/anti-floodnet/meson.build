# this file is part of erssi - anti-floodnet module

libirc_anti_floodnet_a = static_library('irc_anti_floodnet',
  files(
    'anti-floodnet.c',
    'floodnet-ctcp.c',
    'floodnet-nick.c',
    'floodnet-stats.c',
  ),
  include_directories : rootinc,
  implicit_include_directories : false,
  dependencies : dep)
shared_module('irc_anti_floodnet',
  name_suffix : module_suffix,
  install : true,
  install_dir : moduledir,
  link_with : dl_cross_irc_core,
  link_whole : libirc_anti_floodnet_a)

install_headers(
  files(
    'anti-floodnet.h',
    'module.h'
  ),
  subdir : incdir / 'src' / 'irc' / 'anti-floodnet')