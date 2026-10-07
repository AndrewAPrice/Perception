{
  dependencies+: [
    'perception',
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
    'source/network_listener.cc',
    'source/network_service.cc',
    'source/socket.cc',
    'source/dns.cc',
    'source/interface.cc',
    'source/ip.cc',
  ],
} else {
  skip_for_tests: true,
})
