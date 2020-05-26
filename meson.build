project('unflobtactical', 'cpp')

engine_dir = include_directories('engine')
gamui_dir = include_directories('gamui')
grinliz_dir = include_directories('grinliz')
shared_dir = include_directories('shared')

ufolib_sources = [
  'gamui/gamui.cpp',
  'grinliz/glcontainer.cpp',
  'grinliz/gldebug.cpp',
  'grinliz/gldynamic.cpp',
  'grinliz/glfixed.cpp',
  'grinliz/glgeometry.cpp',
  'grinliz/glmatrix.cpp',
  'grinliz/glmemorypool.cpp',
  'grinliz/glperformance.cpp',
  'grinliz/glprime.cpp',
  'grinliz/glrandom.cpp',
  'grinliz/glstringutil.cpp',
  'grinliz/glutil.cpp',
  'grinliz/glvector.cpp',
  'shared/gamedbreader.cpp',
  'shared/glmap.cpp'
]

sdl_dep = dependency('sdl')
opengl_dep = dependency('opengl')
glew_dep = dependency('glew')
tinyxml2_dep = dependency('tinyxml2')

ufolib = static_library(
  'ufolib',
  ufolib_sources,
  include_directories: [gamui_dir, grinliz_dir, shared_dir],
  cpp_args: ['-fpermissive'],
  dependencies: [sdl_dep, tinyxml2_dep]
)
ufolib_dep = declare_dependency(link_with: ufolib)
engine_sources = ['engine/gpustatemanager.cpp', 'engine/settings.cpp', 'engine/shadermanager.cpp']

executable('ufoattack',
  engine_sources,
  include_directories: engine_dir,
  cpp_args: ['-DUFO_LINUX_SDL', '-fpermissive'],
  dependencies: [ufolib_dep, opengl_dep, glew_dep, tinyxml2_dep]
)

e = executable(
  'unit-tests',
  'tests/main.cpp',
)
test('test', e)
