project('myfm', 'c',
  version : '0.2',
  meson_version : '>= 0.60.0',
  default_options : [
    'c_std=gnu11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ])

# _GNU_SOURCE is required for S_ISVTX, readlink(), strdup() & friends.
add_project_arguments('-D_GNU_SOURCE', language : 'c')

gtk_dep = dependency('gtk4', version : '>= 4.12')

# Optional: real PDF thumbnails in the preview pane (same as build.sh).
poppler_dep = dependency('poppler-glib', required : false)
if poppler_dep.found()
  add_project_arguments('-DMYFM_HAVE_POPPLER', language : 'c')
endif

myfm_sources = files(
  'src/main.c',
  'src/gui.c',
  'src/settings.c',
)

executable('myfm',
  myfm_sources,
  dependencies : [gtk_dep, poppler_dep],
  install : true)

# Desktop integration for the distribution
install_data('data/myfm.desktop',
  install_dir : get_option('datadir') / 'applications')
