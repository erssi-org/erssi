# image-preview: Shared image preview module for erssi (Chafa-based)

image_preview_sources = files(
  'image-preview.c',
  'image-preview-cache.c',
  'image-preview-fetch.c',
  'image-preview-chafa.c',
)

image_preview_inc = include_directories('.')

# Build as static library
libimage_preview = static_library('image-preview',
  image_preview_sources,
  include_directories : [rootinc, image_preview_inc],
  dependencies : [dep, curl_dep, image_preview_chafa_dep],
  c_args : [
    def_moduledir,
    def_sysconfdir,
    '-DHAVE_CHAFA=1',
  ],
)

image_preview_dep = declare_dependency(
  link_with : libimage_preview,
  include_directories : [image_preview_inc],
  dependencies : [curl_dep, image_preview_chafa_dep],
)

# Install headers
install_headers(
  files('image-preview.h'),
  subdir : incdir / 'src' / 'image-preview',
)
