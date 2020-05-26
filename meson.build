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
tinyxml2_sources = ['tinyxml2/tinyxml2.cpp']

ufolib = static_library(
  'ufolib',
  ufolib_sources,
  include_directories: [gamui_dir, grinliz_dir, shared_dir],
  cpp_args: ['-fpermissive'],
  dependencies: [sdl_dep]
)
ufolib_dep = declare_dependency(link_with: ufolib)
engine_sources = ['engine/gpustatemanager.cpp', 'engine/settings.cpp', 'engine/shadermanager.cpp']
ufoattack_sources = [
  'engine/gpustatemanager.cpp',
  'engine/settings.cpp',
  'engine/shadermanager.cpp',
  'faces/faces.cpp',
  'game/ai.cpp',
  'game/areaWidget.cpp',
  'game/basetradescene.cpp',
  'game/battledata.cpp',
  'game/battlescene.cpp',
  'game/battlevisibility.cpp',
  'game/buildbasescene.cpp',
  'game/cgame.cpp',
  'game/characterscene.cpp',
  'game/chits.cpp',
  'game/consolewidget.cpp',
  'game/dialogscene.cpp',
  'game/fastbattlescene.cpp',
  'game/firewidget.cpp',
  'game/game.cpp',
  'game/gameresources.cpp',
  'game/gamesettings.cpp',
  'game/geoai.cpp',
  'game/geoendscene.cpp',
  'game/geomap.cpp',
  'game/geoscene.cpp',
  'game/helpscene.cpp',
  'game/inventory.cpp',
  'game/inventoryWidget.cpp',
  'game/item.cpp',
  'game/newgeooptions.cpp',
  'game/newtacticaloptions.cpp',
  'game/research.cpp',
  'game/researchscene.cpp',
  'game/saveloadscene.cpp',
  'game/scene.cpp',
  'game/settingscene.cpp',
  'game/stats.cpp',
  'game/storageWidget.cpp',
  'game/tacmap.cpp',
  'game/tacticalendscene.cpp',
  'game/tacticalintroscene.cpp',
  'game/tacticalunitscorescene.cpp',
  'game/ufosound.cpp',
  'game/unit.cpp',
  'tinyxml2/tinyxml2.cpp',
  'win32/audio.cpp',
  # 'win32/glew.c',
  'win32/main.cpp',
  'engine/camera.cpp',
  'engine/engine.cpp',
  'engine/fixedgeom.cpp',
  'engine/loosequadtree.cpp',
  'engine/map.cpp',
  'micropather/micropather.cpp',
  'engine/model.cpp',
  'engine/particle.cpp',
  'engine/particleeffect.cpp',
  'engine/renderqueue.cpp',
  'engine/screenport.cpp',
  'engine/serialize.cpp',
  'engine/surface.cpp',
  'engine/text.cpp',
  'engine/texture.cpp',
  'engine/ufoutil.cpp',
  'engine/uirendering.cpp',
]
executable('ufoattack',
  ufoattack_sources + tinyxml2_sources,
  include_directories: engine_dir,
  cpp_args: [
    '-DUFO_LINUX_SDL',
    '-DGRINLIZ_NO_STL',
    '-fpermissive',
    '-Wno-narrowing'
  ],
  dependencies: [ufolib_dep, opengl_dep, glew_dep]
)

e = executable(
  'unit-tests',
  'tests/main.cpp',
)
test('test', e)
