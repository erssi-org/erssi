# fe-ansi: Pure ANSI terminal frontend for erssi

fe_ansi_sources = files(
  'term-ansi.c',
)

# Shared sources from fe-text (reuse the same UI logic)
fe_text_shared_sources = files(
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
  '../fe-text/sidepanels-core.c',
  '../fe-text/sidepanels-render.c',
  '../fe-text/sidepanels-activity.c',
  '../fe-text/sidepanels-signals.c',
  '../fe-text/sidepanels-layout.c',
  '../fe-text/statusbar-config.c',
  '../fe-text/statusbar-items.c',
  '../fe-text/statusbar.c',
  '../fe-text/textbuffer-commands.c',
  '../fe-text/textbuffer-formats.c',
  '../fe-text/textbuffer-view.c',
  '../fe-text/textbuffer.c',
  '../fe-text/resize-debug.c',
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
  '-DUSE_ANSI_BACKEND',
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

executable('erssi-ansi',
  fe_ansi_sources + fe_text_shared_sources + [
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
