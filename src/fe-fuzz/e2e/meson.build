# this file is part of irssi

# Fuzz targets for what the e2e module parses: RPE2E wire messages, the
# key exchange CTCPs, IRC lines and CASEMAPPING folds (wire), keyring.json
# and /e2e import files with what is read out of them (keyring).
#
# With -Dwith-fuzzer=yes each is a libFuzzer binary (e2e-<name>-fuzz), and
# `meson test` runs that binary over corpus/<name> with -runs=0: every
# input once, nothing mutated, nothing written. Every object of such a
# build is instrumented (-fsanitize=fuzzer-no-link), so a replay linked
# without libFuzzer has no __sanitizer_cov_* symbols - unless a sanitizer
# runtime happens to bring weak ones. In every other build the target is
# linked with replay.c and `meson test` runs that over corpus/<name>: the
# seeds and the inputs of fixed crashes.

# HOME in the build directory; the corpus holds leak reproducers, so under
# AddressSanitizer it runs with leak detection
e2e_fuzz_env = environment()
e2e_fuzz_env.set('HOME', meson.build_root() / 'test-home')
e2e_fuzz_env.set('ASAN_OPTIONS', 'detect_leaks=1:abort_on_error=1')

e2e_fuzz_targets = ['wire', 'keyring']

foreach name : e2e_fuzz_targets
  corpus = meson.current_source_dir() / 'corpus' / name
  if want_fuzzer
    fuzz = executable('e2e-' + name + '-fuzz',
      files(name + '.c'),
      link_with : libe2e_proto_a,
      link_args : [fuzzer_lib],
      link_language : fuzzer_link_language,
      include_directories : rootinc,
      implicit_include_directories : false,
      install : true,
      dependencies : dep,
    )
    test('e2e fuzz corpus ' + name, fuzz,
      env : e2e_fuzz_env,
      args : ['-runs=0', corpus],
      timeout : 120)
  else
    replay = executable('e2e-' + name + '-replay',
      files(name + '.c', 'replay.c'),
      link_with : libe2e_proto_a,
      include_directories : rootinc,
      implicit_include_directories : false,
      dependencies : dep,
      build_by_default : false,
    )
    test('e2e fuzz corpus ' + name, replay,
      env : e2e_fuzz_env,
      args : [corpus],
      timeout : 120)
  endif
endforeach

# noinst_headers = files(
#   'e2e-fuzz.h',
# )
