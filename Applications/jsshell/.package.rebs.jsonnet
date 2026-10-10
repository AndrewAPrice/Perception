{
  dependencies+: [
    'perception',
    'Linux System Call Shim',
    'quickjs',
    'curl',
    'bearssl',
    'madler zlib',
    'nlohmann json',
  ],
  include_directories: [
    'source',
  ],
  source_directories: [
    'source',
  ],
  asset_directories: [
    'assets',
  ],
} + (if is_testing then {
  dependencies+: [
    'Perception Test',
  ],
  files_to_ignore: [
    'source/main.cc',
    'source/js_engine.cc',
    'source/module/fs.cc',
    'source/module/proc.cc',
    'source/module/sys.cc',
    'source/module/net.cc',
    'source/object_inspector.cc',
    'source/line_editor.cc',
  ],
} else {
  skip_for_tests: true,
})
