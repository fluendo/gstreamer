project('tutorials', 'c')

dependencies = []
dependencies += dependency('gstreamer-1.0')
dependencies += dependency('gtk+-3.0', version : '>=3.0')
dependencies += dependency('clutter-gst-3.0', version : '>=3.0')
dependencies += dependency('gstreamer-audio-1.0')
dependencies += dependency('gstreamer-pbutils-1.0')

# Available tutorials
available_tutorials = [
  'basic-tutorial-1',
  'basic-tutorial-2',
  'basic-tutorial-3',
  'basic-tutorial-4',
  'basic-tutorial-5',
  'basic-tutorial-6',
  'basic-tutorial-7',
  'basic-tutorial-8',
  'basic-tutorial-9',
  'basic-tutorial-12',
  'basic-tutorial-13',
  'basic-tutorial-15',
  'playback-tutorial-1',
  'playback-tutorial-2',
  'playback-tutorial-3',
  'playback-tutorial-4',
  'playback-tutorial-5',
  'playback-tutorial-6',
  'playback-tutorial-7',
]


# Iterate over all tutorials and create an executable for each
foreach tutorial : available_tutorials
  executable(tutorial, '@0@.c'.format(tutorial), dependencies : dependencies)
endforeach
