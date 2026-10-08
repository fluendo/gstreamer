mpegh3daenc_sources = [
  'gstmpegh3daenc.c',
]

mpegh3daenc_headers = [
  'gstmpegh3daenc.h',
]

doc_sources = []
foreach s: mpegh3daenc_sources + mpegh3daenc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpegh3daenc': pathsep.join(doc_sources)
}

mpegh3daenc_dep = dependency('libmpeghe',
  fallback: 'libmpeghe',
  required: get_option('mpegh3daenc'),
  static: true)

if mpegh3daenc_dep.found()
  gstmpegh3daenc = library('gstmpegh3daenc',
    mpegh3daenc_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstaudio_dep, mpegh3daenc_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstmpegh3daenc]
endif
