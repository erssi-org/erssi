# this file is part of erssi - notcurses frontend

# Notcurses-specific sources (terminal backend replacement)
nc_sources = files(
  'term-notcurses.c',
)

# Shared sources from fe-text (backend-agnostic code)
shared_sources = files(
  '../fe-text/gui-entry.c',
  '../fe-text/gui-expandos.c',
  '../fe-text/gui-gestures.c',
  '../fe-text/gui-mouse.c',
  '../fe-text/gui-printtext.c',
  '../fe-text/gui-readline.c',
  '../fe-text/gui-windows.c',
  '../fe-text/irssi.c',
  '../fe-text/lastlog.c',
  '../fe-text/mainwindow-activity.c',
  '../fe-text/mainwindows-layout.c',
  '../fe-text/mainwindows.c',
  '../fe-text/module-formats.c',
  '../fe-text/sidepanels-activity.c',
  '../fe-text/sidepanels-core.c',
  '../fe-text/sidepanels-layout.c',
  '../fe-text/sidepanels-render.c',
  '../fe-text/sidepanels-signals.c',
  '../fe-text/statusbar-config.c',
  '../fe-text/statusbar-items.c',
  '../fe-text/statusbar.c',
  '../fe-text/term.c',
  '../fe-text/textbuffer-commands.c',
  '../fe-text/textbuffer-formats.c',
  '../fe-text/textbuffer-view.c',
  '../fe-text/textbuffer.c',
)

executable('erssi-nc',
  nc_sources + shared_sources + [
    irssi_version_h,
    default_config_h,
    default_theme_h,
  ],
  include_directories : [rootinc, include_directories('../fe-text')],
  implicit_include_directories : false,
  export_dynamic : true,
  c_args : [
    def_themesdir,
    def_datadir,
    '-DUSE_NOTCURSES',
  ],
  link_with : [
    libconfig_a,
    libcore_a,
    libfe_common_core_a,
  ],
  install : true,
  dependencies : dep + [notcurses_dep],  # notcurses replaces ncurses/terminfo
)

install_headers(
  files(
    'term-notcurses.h',
  ),
  subdir : incdir / 'src' / 'fe-notcurses')
