mediaauth_sources = [
  'gstmpegh3daauth.c',
  'gstmpegh3dacommon.c',
  'gstmpegh3daparse.c',
  'gstmpegh3dacrypto.c',
  'gstmpegh3daauthpktsbuild.c',
  'gstmpegh3dasigner.c',
  'gstmpegh3daverifier.c',
  'gstmpegh3daauthdata.c'
]

mediaauth_headers = [
  'gstmpegh3dacommon.h',
  'gstmpegh3daparse.h',
  'gstmpegh3dacrypto.h',
  'gstmpegh3daauthpktsbuild.h',
  'gstmpegh3dasigner.h',
  'gstmpegh3daverifier.h',
  'gstmpegh3daauthdata.h'
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

openssl_dep = dependency('openssl', version : '>= 1.1.0',
    required : get_option('mpegh3da-media-auth'))

mediaauth_deps = [gst_dep, gstbase_dep, openssl_dep]

gstmpegh3daauth = library('gstmpegh3daauth',
  mediaauth_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : mediaauth_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpegh3daauth]
