project('unflobtactical', 'cpp')

engine_dir = include_directories('engine')

engine_sources = ['engine/gpustatemanager.cpp', 'engine/settings.cpp', 'engine/shadermanager.cpp']
executable('ufoattack', engine_sources, include_directories: engine_dir, cpp_args: '-DUFO_LINUX_SDL')

e = executable(
  'unit-tests',
  'tests/main.cpp',
)
test('test', e)
