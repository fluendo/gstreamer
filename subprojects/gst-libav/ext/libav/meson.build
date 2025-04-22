libav_sources = [
    'gstav.c',
    'gstavcodecmap.c',
    'gstavutils.c',
    'gstavcfg.c',
]

foreach group : element_groups
  libav_sources += 'gstav@0@.c'.format(group)
endforeach

if 'mux' in element_groups or 'demux' in element_groups
  libav_sources += 'gstavprotocol.c'
endif

libav_headers = [
  'gstavviddec.h',
  'gstavvidenc.h',
  'gstavcfg.h',
  'gstavprotocol.h',
  'gstavutils.h',
  'gstavaudenc.h',
  'gstav.h',
  'gstavcodecmap.h',
  'gstavauddec.h',
]

gstlibav_plugin = library('gstlibav',
    libav_sources,
    c_args : gst_libav_args,
    include_directories : [configinc],
    dependencies : libav_deps + [gst_dep, gstbase_dep, gstvideo_dep,
        gstaudio_dep, gstpbutils_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
plugins += [gstlibav_plugin]

doc_sources = []
foreach s: libav_sources + libav_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'libav': pathsep.join(doc_sources)
}
