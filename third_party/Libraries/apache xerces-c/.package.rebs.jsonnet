{
  skip_for_tests: true,
  package_type: 'library',
  defines+: [
    'HAVE_CONFIG_H=1',
  ],
  dependencies+: [
    'musl',
    'libcxx',
    'curl',
  ],
  public_include_directories: [
    'public',
  ],
  include_directories: [
    'source',
    'config',
    'public',
  ],
  source_directories: [
    'source',
  ],
  files_to_ignore: [
    'source/xercesc/util/JanitorExports.cpp',
    'source/xercesc/util/MutexManagers/WindowsMutexMgr.cpp',
    'source/xercesc/util/Transcoders/Win32/Win32TransService.cpp',
    'source/xercesc/util/Transcoders/MacOSUnicodeConverter/MacOSUnicodeConverter.cpp',
    'source/xercesc/util/Transcoders/ICU/ICUTransService.cpp',
    'source/xercesc/util/FileManagers/WindowsFileMgr.cpp',
    'source/xercesc/util/NetAccessors/WinSock/WinSockNetAccessor.cpp',
    'source/xercesc/util/NetAccessors/WinSock/BinHTTPURLInputStream.cpp',
    'source/xercesc/util/NetAccessors/MacOSURLAccessCF/URLAccessCFBinInputStream.cpp',
    'source/xercesc/util/NetAccessors/MacOSURLAccessCF/MacOSURLAccessCF.cpp',
    'source/xercesc/util/NetAccessors/Socket/SocketNetAccessor.cpp',
    'source/xercesc/util/NetAccessors/Socket/UnixHTTPURLInputStream.cpp',
    'source/xercesc/util/MsgLoaders/ICU/ICUMsgLoader.cpp',
  ],
}
