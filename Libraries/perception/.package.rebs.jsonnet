{
  package_type: 'library',
  public_include_directories: [
    'public',
  ],
  source_directories: [
    'source',
  ],
  dependencies+: [
    'tlsf',
  ],
} + (if is_testing then {
  files_to_ignore: [
    'source/perception/debug.cc',
    'source/perception/draw.cc',
    'source/perception/fibers.cc',
    'source/perception/font.cc',
    'source/perception/heap_allocator.cc',
    'source/perception/loader.cc',
    'source/perception/memory.cc',
    'source/perception/messages.cc',
    'source/perception/permissions.cc',
    'source/perception/processes.cc',
    'source/perception/profiling.cc',
    'source/perception/random.cc',
    'source/perception/rpc_memory.cc',
    'source/perception/scheduler.cc',
    'source/perception/service_client.cc',
    'source/perception/service_server.cc',
    'source/perception/services.cc',
    'source/perception/threads.cc',
    'source/perception/time.cc',
    'source/perception/type_id.cc',
    'source/perception/devices/keyboard_listener.cc',
    'source/perception/devices/mouse_listener.cc',
    'source/perception/devices/graphics_device.cc',
    'source/perception/devices/graphics_listener.cc',
    'source/perception/devices/device_manager.cc',
    'source/perception/registry_service.cc',
    'source/perception/registry.cc',
    'source/perception/clipboard.cc',
  ]
} else {
  files_to_ignore: [
    'source/perception/test_stubs.cc',
  ]
})
