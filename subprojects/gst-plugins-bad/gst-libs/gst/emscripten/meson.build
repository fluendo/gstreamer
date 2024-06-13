gstemscripten_dep = dependency('', required : false)

if cc.get_id() != 'emscripten'
  subdir_done()
endif

gstemscripten = library('gstemscripten-' + api_version,
  files('gstemscripten.c'),
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep],
)

library_def = {'lib': gstemscripten}
pkg_name = 'gstreamer-emscripten-1.0'
pkgconfig.generate(gstemscripten,
  libraries : [gst_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Emscripten helper',
)

gst_libraries += [[pkg_name, library_def]]

gstemscripten_dep = declare_dependency(link_with : gstemscripten,
  include_directories : [libsinc],
  dependencies : [gst_dep])
meson.override_dependency(pkg_name, gstemscripten_dep)

install_headers(['gstemscripten.h'], subdir : 'gstreamer-1.0/gst/emscripten/')
