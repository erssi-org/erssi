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
