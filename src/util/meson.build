libutil_sources = [
  'string/pred.cc',
  'string/sanitize.cc',
  'string/strip.cc',
  'string/split.cc',
  'log-level.cc',
  'io.cc',
  'range.cc',
  'demangle.cc',
  'mapping.cc',
  'cmdline.cc',
  'rng.cc',
  'permute.cc',
  'file-paths.cc',
  'ptree.cc',
  'text.cc',
  'myexception.cc',
  'time.cc']

libutil_inc = include_directories('include')

libutil = static_library('util', libutil_sources,
			 include_directories: libutil_inc,
			 dependencies: [boost, eigen, json, librange])

