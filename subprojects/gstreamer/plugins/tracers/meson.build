if not tracer_hooks
  if get_option('coretracers').enabled()
    error('coretracers plugin enabled but not tracer_hooks')
  endif
  subdir_done()
endif

gst_tracers_sources = [
  'gstframerate.c',
  'gstlatency.c',
  'gstleaks.c',
  'gststats.c',
  'gsttracers.c',
  'gstfactories.c'
]

if gst_debug
  gst_tracers_sources += ['gstlog.c']
endif

if cdata.has('HAVE_GETRUSAGE')
  gst_tracers_sources += ['gstrusage.c']
endif

thread_dep = dependency('threads', required : false)

gio_unix_dep = dependency('gio-unix-2.0')

gst_tracers = library('gstcoretracers',
  gst_tracers_sources,
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, thread_dep, gst_base_dep, gio_dep, gio_unix_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gst_tracers]
