debugutilsbad_sources = [
  'debugutilsbad.c',
  'fpsdisplaysink.c',
  'gstchecksumsink.c',
  'gstchopmydata.c',
  'gstclockselect.c',
  'gstcompare.c',
  'gstdebugspy.c',
  'gsterrorignore.c',
  'gsttestsrcbin.c',
  'gstwatchdog.c',
]

gstdebugutilsbad_deps = [gstbase_dep, gstvideo_dep, gstnet_dep, gstaudio_dep]
if host_machine.system() != 'emscripten'
  debugutilsbad_sources += [
    'gstfakeaudiosink.c',
    'gstfakesinkutils.c',
    'gstfakevideosink.c',
    'gstvideocodectestsink.c',
  ]
  gstdebugutilsbad_deps += gio_dep
endif

gstdebugutilsbad = library('gstdebugutilsbad',
  debugutilsbad_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : gstdebugutilsbad_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdebugutilsbad]
