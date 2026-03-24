# fe-ansi: Pure ANSI terminal frontend for erssi

fe_ansi_sources = files(
  'term-ansi.c',
  'gui-entry.c',
  'gui-expandos.c',
  'gui-gestures.c',
  'gui-mouse.c',
  'gui-printtext.c',
  'gui-readline.c',
  'gui-windows.c',
  'irssi.c',
  'lastlog.c',
  'mainwindow-activity.c',
  'mainwindows-layout.c',
  'mainwindows.c',
  'module-formats.c',
  'sidepanels-core.c',
  'sidepanels-render.c',
  'sidepanels-activity.c',
  'sidepanels-signals.c',
  'sidepanels-layout.c',
  'statusbar-config.c',
  'statusbar-items.c',
  'statusbar.c',
  'textbuffer-commands.c',
  'textbuffer-formats.c',
  'textbuffer-view.c',
  'textbuffer.c',
  'resize-debug.c',
)

fe_ansi_dep = []
fe_ansi_libs = [
  libconfig_a,
  libcore_a,
  libfe_common_core_a,
]
fe_ansi_c_args = [
  def_themesdir,
  def_datadir,
]
fe_ansi_inc = [rootinc]

# Add Chafa dependency if available
if chafa_dep.found()
  fe_ansi_dep += chafa_dep
endif

# Add image-preview module if available
if have_image_preview
  fe_ansi_libs += libimage_preview
  fe_ansi_dep += [curl_dep, image_preview_chafa_dep]
  fe_ansi_c_args += '-DHAVE_IMAGE_PREVIEW=1'
  fe_ansi_inc += image_preview_inc
endif

executable('erssi',
  fe_ansi_sources + [
    irssi_version_h,
    default_config_h,
    default_theme_h,
  ],
  include_directories : fe_ansi_inc,
  implicit_include_directories : false,
  export_dynamic : true,
  c_args : fe_ansi_c_args,
  link_with : fe_ansi_libs,
  install : true,
  dependencies : dep + fe_ansi_dep,
)

install_headers(
  files(
    'gui-entry.h',
    'gui-gestures.h',
    'gui-mouse.h',
    'gui-printtext.h',
    'gui-readline.h',
    'gui-windows.h',
    'mainwindows.h',
    'module-formats.h',
    'resize-debug.h',
    'sidepanels.h',
    'sidepanels-activity.h',
    'sidepanels-layout.h',
    'sidepanels-render.h',
    'sidepanels-signals.h',
    'sidepanels-types.h',
    'statusbar.h',
    'statusbar-config.h',
    'statusbar-item.h',
    'term.h',
    'textbuffer.h',
    'textbuffer-formats.h',
    'textbuffer-view.h',
  ),
  subdir : incdir / 'src' / 'fe-ansi',
)
