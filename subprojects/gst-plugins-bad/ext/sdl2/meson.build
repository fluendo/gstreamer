if cc.get_id() != 'emscripten'
  subdir_done()
endif

sdl2_dep = declare_dependency (
  compile_args: ['-sUSE_SDL=2', '-Wall', '-Werror'],
  link_args: ['-sUSE_SDL=2', '-lGL'],
)

gstsdl2 = library('gstsdl2',
  'gstsdl2.c', 'gstsdl2sink.c',
  c_args: [gst_plugins_bad_args],
  include_directories: [configinc, libsinc],
  dependencies: [gstemscripten_dep, gstvideo_dep, sdl2_dep],
  install: true,
  install_dir: plugins_install_dir,
)
plugins += [gstsdl2]
