mediaauth_sources = [
  'gstmpegh3dacommon.c',
  'gstmpegh3dahash.c',
  'gstmpegh3dasigner.c'
]

mediaauth_headers = [
  'gstmpegh3dacommon.h',
  'gstmpegh3dahash.h',
  'gstmpegh3dasigner.h'
]

doc_sources = []
foreach s: mediaauth_sources + mediaauth_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpegh3daauth': pathsep.join(doc_sources)
}

if get_option('mpegh3da-media-auth').disabled()
  subdir_done()
endif

mediaauth_deps = [gst_dep, gstbase_dep]

gstmpegh3daauth = library('gstmpegh3daauth',
  mediaauth_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : mediaauth_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpegh3daauth]
