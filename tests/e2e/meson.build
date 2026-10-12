# Unit tests of the RPE2E protocol code (src/e2e), with fixed vectors from
# libsodium and from the rpe2e.pl script.

# HOME in the build directory: no test may write into the real home of
# whoever builds erssi
e2e_test_env = environment()
e2e_test_env.set('HOME', meson.build_root() / 'test-home')

foreach name : ['crypto', 'wire', 'keyring', 'proto', 'scope', 'outq', 'state']
  exe = executable('test-e2e-' + name,
    files('test-e2e-' + name + '.c'),
    link_with : libe2e_proto_a,
    include_directories : rootinc,
    implicit_include_directories : false,
    dependencies : dep,
  )
  # scope sweeps every code point and 100k random names: ~50 s in ASan;
  # proto floods the request limits with ~1000 signed KEYREQs: ~16 s
  test('test-e2e-' + name + ' test', exe,
    env : e2e_test_env,
    args : ['--tap'],
    protocol : 'tap',
    timeout : {'scope' : 300, 'proto' : 120}.get(name, 30))
endforeach

# native module <-> rpe2e.pl on a local fake IRC server, in a private tmux
# server; skipped without tmux, Perl support or rpe2e.pl's Perl modules.
# It installs this build (meson install), so it depends on what it runs:
# `meson test` rebuilds those first. About 13 min (every encrypted line
# holds the flood queue 4 s and more); skipped without RPE2E_SCRIPT
live_depends = [erssi_exe, libe2e_core_sm, libirc_core_sm]
if have_perl
  live_depends += [libperl_core_sm, libfe_perl_sm]
endif
test('e2e live interop (native <-> rpe2e.pl)', find_program('live-interop.sh'),
  args : [meson.build_root(), meson.source_root()],
  env : e2e_test_env,
  depends : live_depends,
  is_parallel : false,
  timeout : 1500)
