mediaauth_sources = [
  'gstmpegh3dacommon.c',
  'gstmpegh3dasigner.c'
]

mediaauth_headers = [
  'gstmpegh3dacommon.h',
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

mediaauth_cargs = []
mediaauth_deps = [gst_dep, gstbase_dep]

# OpenSSL EVP is only needed for SHA-224 (GChecksum covers the rest).
openssl_dep = dependency('openssl', version : '>= 1.1.0', required : false)
if openssl_dep.found()
  mediaauth_cargs += ['-DHAVE_OPENSSL']
  mediaauth_deps += openssl_dep
endif

gstmpegh3daauth = library('gstmpegh3daauth',
  mediaauth_sources,
  c_args : gst_plugins_bad_args + mediaauth_cargs,
  include_directories : [configinc],
  dependencies : mediaauth_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpegh3daauth]
