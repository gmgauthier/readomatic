project(
  'readomatic',
  'cpp',
  version: '1.2.11',
  license: 'Unlicense',
  meson_version: '>=0.56',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'prefix=/usr',
  ],
)

gtkmm = dependency('gtkmm-3.0')
libarchive = dependency('libarchive')
libxml = dependency('libxml-2.0')
fontconfig = dependency('fontconfig')
libmobi = dependency('libmobi')

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('APP_ID', 'org.gmgauthier.ReadOMatic')
conf.set_quoted('DATADIR', get_option('prefix') / get_option('datadir') / 'readomatic')
conf.set_quoted('SOURCE_ROOT', meson.project_source_root())

configure_file(
  output: 'config.hpp',
  configuration: conf,
)

inc = include_directories('.', 'src')

executable(
  'readomatic',
  'src/main.cpp',
  'src/application.cpp',
  'src/paths.cpp',
  'src/main_window.cpp',
  'src/about_dialog.cpp',
  'src/book.cpp',
  'src/topic_view.cpp',
  'src/settings.cpp',
  'src/font_dialog.cpp',
  'src/library_devices.cpp',
  'src/library_catalog.cpp',
  'src/library_window.cpp',
  'src/lastread.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, fontconfig, libmobi],
  install: true,
  install_dir: get_option('bindir'),
)

install_data(
  'data/readomatic.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'data/skin/lcos/lcos.css',
  'data/skin/lcos/btn-back.svg',
  'data/skin/lcos/btn-prev.svg',
  'data/skin/lcos/btn-next.svg',
  install_dir: get_option('datadir') / 'readomatic' / 'skin' / 'lcos',
)

install_data(
  'brand/icon-tile.svg',
  install_dir: get_option('datadir') / 'readomatic' / 'brand',
)

install_data(
  'brand/icon-tile.svg',
  rename: 'readomatic.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

readomatic_tests = executable(
  'readomatic-tests',
  'tests/test_place.cpp',
  'src/lastread.cpp',
  include_directories: inc,
  dependencies: [gtkmm],
)
test('place', readomatic_tests)

readomatic_confine_tests = executable(
  'readomatic-confine-tests',
  'tests/test_confine.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('confine', readomatic_confine_tests)

readomatic_image_tests = executable(
  'readomatic-image-tests',
  'tests/test_image.cpp',
  'src/topic_view.cpp',
  'src/settings.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libxml],
)
test('image', readomatic_image_tests)

readomatic_link_tests = executable(
  'readomatic-link-tests',
  'tests/test_links.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('links', readomatic_link_tests)

readomatic_open_tests = executable(
  'readomatic-open-tests',
  'tests/test_open.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('open', readomatic_open_tests)

readomatic_status_tests = executable(
  'readomatic-status-tests',
  'tests/test_status.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('status', readomatic_status_tests)

readomatic_spine_tests = executable(
  'readomatic-spine-tests',
  'tests/test_spine.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('spine', readomatic_spine_tests)

readomatic_pages_tests = executable(
  'readomatic-pages-tests',
  'tests/test_pages.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('pages', readomatic_pages_tests)

readomatic_find_tests = executable(
  'readomatic-find-tests',
  'tests/test_find.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('find', readomatic_find_tests)

readomatic_jump_tests = executable(
  'readomatic-jump-tests',
  'tests/test_jump.cpp',
  'src/book.cpp',
  include_directories: inc,
  dependencies: [gtkmm, libarchive, libxml, libmobi],
)
test('jump', readomatic_jump_tests)
