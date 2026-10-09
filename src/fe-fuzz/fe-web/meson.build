# this file is part of irssi

# Fuzz targets for what fe-web parses from the network. With
# -Dwith-fuzzer=yes each is a libFuzzer binary (fe-web-<name>-fuzz); in
# every build the same target is linked with replay.c and `meson test`
# runs it over corpus/<name>: the seeds and the inputs of fixed crashes.

fe_web_fuzz_targets = ['handshake', 'frames', 'message', 'crypto', 'irc']

# The corpus holds leak reproducers: under AddressSanitizer the replays run
# with leak detection even when the rest of the suite does not.
fe_web_fuzz_test_env = environment()
fe_web_fuzz_test_env.set('ASAN_OPTIONS', 'detect_leaks=1:abort_on_error=1')

if want_fuzzer
  fe_web_fuzz_fe_common_core = libfuzzer_fe_common_core_a
else
  fe_web_fuzz_fe_common_core = libfe_common_core_a
endif

libfe_web_fuzz_a = static_library('fe_web_fuzz',
  fe_web_sources
  + files(
    'fe-web-fuzz.c',
    '../null-logger.c',
  )
  + [ irssi_version_h ],
  include_directories : rootinc,
  implicit_include_directories : false,
  dependencies : [dep, openssl_dep],
  build_by_default : false,
)

fe_web_fuzz_link_with = [
  libfe_web_fuzz_a,
  libconfig_a,
  libcore_a,
  libirc_core_a,
  fe_web_fuzz_fe_common_core,
  libfe_common_irc_a,
  libfe_irc_dcc_a,
  libfe_irc_notifylist_a,
]

foreach name : fe_web_fuzz_targets
  if want_fuzzer
    executable('fe-web-' + name + '-fuzz',
      files(name + '.c'),
      link_with : fe_web_fuzz_link_with,
      link_args : [fuzzer_lib],
      link_language : fuzzer_link_language,
      include_directories : rootinc,
      implicit_include_directories : false,
      install : true,
      dependencies : [dep, openssl_dep],
    )
  endif

  replay = executable('fe-web-' + name + '-replay',
    files(name + '.c', 'replay.c'),
    link_with : fe_web_fuzz_link_with,
    include_directories : rootinc,
    implicit_include_directories : false,
    dependencies : [dep, openssl_dep],
    build_by_default : false,
  )
  test('fe-web fuzz corpus ' + name, replay,
    env : fe_web_fuzz_test_env,
    args : [meson.current_source_dir() / 'corpus' / name],
    timeout : 120)
endforeach
