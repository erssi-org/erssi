# this file is part of irssi

subdir('lib-config')
subdir('core')
foreach s : chat_modules
  subdir(s)
endforeach
subdir('fe-common')
if have_perl
  subdir('perl')
endif
if have_otr
  subdir('otr')
endif
if have_e2e
  subdir('e2e')
endif
if have_fe_web
  subdir('fe-web')
endif
if have_image_preview
  subdir('image-preview')
endif
subdir('fe-ansi')
if want_bot
  subdir('fe-none')
endif
if want_fuzzer
  subdir('fe-fuzz')
endif
if have_e2e
  # replayed by `meson test` in every build, fuzzed with -Dwith-fuzzer=yes
  subdir('fe-fuzz/e2e')
endif

install_headers(
  files(
    'common.h'
  ),
  subdir : incdir / 'src')
