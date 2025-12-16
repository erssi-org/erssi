# this file is part of erssi - notcurses frontend

# Notcurses-specific sources (terminal backend replacement)
nc_sources = files(
  'term-notcurses.c',
)

# Image preview sources (optional, requires libcurl)
image_preview_sources = []
image_preview_deps = []
image_preview_args = []

if have_image_preview
  image_preview_sources = files(
    'image-preview.c',
    'image-preview-cache.c',
    'image-preview-fetch.c',
    'image-preview-render.c',
  )
  image_preview_deps = [curl_dep]
  image_preview_args = ['-DHAVE_IMAGE_PREVIEW']
endif

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

# Build rpath for notcurses (needed when installed in non-standard location like /opt/notcurses)
nc_install_rpath = notcurses_libdir != '' ? notcurses_libdir : ''

# Extra dependencies for image preview (full notcurses with multimedia)
nc_extra_deps = []
if have_image_preview
  nc_extra_deps = [notcurses_full_lib]
endif

executable('erssi-nc',
  nc_sources + shared_sources + image_preview_sources + [
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
  ] + image_preview_args,
  link_with : [
    libconfig_a,
    libcore_a,
    libfe_common_core_a,
  ],
  install : true,
  install_rpath : nc_install_rpath,
  dependencies : dep + [notcurses_dep] + nc_extra_deps + image_preview_deps,
)

nc_headers = ['term-notcurses.h']
if have_image_preview
  nc_headers += ['image-preview.h']
endif

install_headers(
  files(nc_headers),
  subdir : incdir / 'src' / 'fe-notcurses')
