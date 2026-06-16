project(
    'observer',
    'c',
    'cpp',
    version: run_command(
        'sh',
        '-c', '''
        v=$(git describe --tags --always --dirty 2>/dev/null || true)
        v=${v#v}
        if [ -n "$v" ]; then
            if [ "${v%.*}" = "$v" ]; then
                v="0.0.0-$v"
            fi
            printf "%s" "$v"
        else
            echo "0.0.0"
        fi
        ''',
        check: false,
    ).stdout().strip(),
    license: 'AGPL-3.0-or-later',
    default_options: [
        'c_std=c99',
        'warning_level=2',
    ],
)

project_author = 'Lenik'
project_email = 'observer@bodz.net'
project_year = 2026

fs = import('fs')
c = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
libdir = prefix / get_option('libdir')
localedir = prefix / get_option('localedir')
mandir = prefix / get_option('mandir')
pkgdatadir = datadir / meson.project_name()
pkgdocdir = datadir / 'doc' / meson.project_name()

config_h = configuration_data()
config_h.set_quoted('PROJECT_VERSION', meson.project_version())
config_h.set_quoted('PROJECT_AUTHOR', project_author)
config_h.set_quoted('PROJECT_EMAIL', project_email)
config_h.set('PROJECT_YEAR', project_year)

config_h.set_quoted('PREFIX', prefix)
config_h.set_quoted('DATADIR', datadir)
config_h.set_quoted('MANDIR', mandir)
config_h.set_quoted('PKGDATADIR', pkgdatadir)
config_h.set_quoted('LOCALEDIR', localedir)

configure_file(
    output: 'config.h',
    configuration: config_h,
)

bas_cpp_dep = dependency('bas-cpp', required: true)
bas_ui_dep = dependency('bas-ui', required: true)

# bas_c_static_dep = dependency('bas-c-static', required: true)
# bas_cpp_static_dep = dependency('bas-cpp-static', required: true)
# bas_ui_static_dep = dependency('bas-ui-static', required: true)

sqlite_dep = dependency('sqlite3', required: true)
wx_dep = dependency('wxwidgets', modules: ['base', 'core', 'stc', 'adv'], required: true)

lib_objects = []

# Build assets.zip every time (no file dependency)
assets_dir = meson.current_source_dir() / 'assets'
assets_src = 'src/assets.s'
if fs.exists(assets_dir) and fs.exists(assets_src)
    build_dir = meson.current_build_dir()
    assets_zip = custom_target(
        'assets_zip',
        output: 'assets.zip',
        input: [],
        command: [
            'sh',
            '-c', 'dst=$(readlink -f "@1@"); cd "@0@" && zip -r "$dst" .'.format(assets_dir, '@OUTPUT@'),
        ],
        #build_always : true,
        build_by_default: true,
        install: false,
    )

    # Compile assembly file that embeds assets.zip using .incbin
    # Use configure_file to copy assets.s to build directory
    assets_s = configure_file(
        input: 'src/assets.s',
        output: 'assets.s',
        copy: true,
    )

    # Compile the assembly file (assets.zip will be in the same directory)
    as = find_program('as', required: true)
    assets_o = custom_target(
        'assets_o',
        output: 'assets.o',
        input: [assets_s, assets_zip],
        command: [as, '-o', '@OUTPUT@', '@INPUT0@'],
        depends: assets_zip,
    )

    lib_objects += [assets_o]
endif

# Disable all warnings (wx 3.0 and deps are noisy)
nowarn = cpp.get_supported_arguments(['-w', '/w'])
bas_args = ['-DLOGGER_NAME=observer', '-DASSETS_NAME=observer']

cpp_pch = 'src/cpp_pch.hpp'
cpp_args = [nowarn, bas_args, '-DTEXT_DOMAIN=observer'] + cpp.get_supported_arguments([
  '-Wno-deprecated-copy-with-user-provided-copy',
  '-Wno-cast-function-type-mismatch',
])

lib_inc = [
    include_directories('src'),
]

lib_deps = [
    bas_cpp_dep,
    bas_ui_dep,
    wx_dep,
    sqlite_dep,
]

subdir('po')

lib_sources = [
  'src/AppIcon.cpp',
  'src/AppConfig.cpp',
  'src/DataDirStore.cpp',
  'src/ObservationStore.cpp',
  'src/QuoteProvider.cpp',
  'src/SqliteStore.cpp',
  'src/lunar.cpp',
  'src/formatting.cpp',
  'src/emoji.cpp',
  'src/calendar.cpp',
]

lib_version = meson.project_version().split('-')[0]
lib_soversion = lib_version.split('.')[0]

lib_observer = shared_library(
    'observer',
    lib_sources,
    'src/module.cpp',
    lib_objects,
    cpp_args: cpp_args,
    cpp_pch: cpp_pch,
    include_directories: lib_inc,
    dependencies: lib_deps,
    version: lib_version,
    soversion: lib_soversion,
    install: true,
    install_dir: libdir,
)

lib_observer_static = static_library(
    'observer',
    lib_sources,
    'src/module.cpp',
    lib_objects,
    cpp_args: cpp_args,
    cpp_pch: cpp_pch,
    include_directories: lib_inc,
    dependencies: lib_deps,
)

observer_dep = declare_dependency(
    link_with: lib_observer,
    include_directories: lib_inc,
)
observer_static_dep = declare_dependency(
    link_with: lib_observer_static,
    include_directories: lib_inc,
)

meson.override_dependency('observer', observer_dep)
meson.override_dependency('observer-static', observer_static_dep)

# ---- pkg-config ----
pkgconfig = import('pkgconfig')
pkgconfig.generate(
    lib_observer,
    name: 'observer',
    description: 'Zephyr shared library',
    version: meson.project_version(),
    subdirs: 'observer',
    requires: ['bas-c'],
)

pkgconfig.generate(
    lib_observer_static,
    name: 'observer-static',
    description: 'Zephyr static library',
    version: meson.project_version(),
    subdirs: 'observer',
    requires: ['bas-c'],
)

# ---- Headers for mesonfmt package (includedir/meson) ----
find_other_cmd = run_command(
    'find',
    'src',
    '-name', '*.h',
    '-o',
    '-name', '*.hpp',
    '-o',
    '-type', 'd',
    '-o',
    '-print',
    check: true,
)
other_files = find_other_cmd.stdout().strip().split('\n')

other_list = [
    'bulk.h',
    'bulk.hpp',
    'c_pch.h',
    'cpp_pch.hpp',
    'AppConfig.h',
]
foreach f : other_files
    other_list += f.substring(4)
endforeach

install_subdir(
    'src',
    install_dir: get_option('includedir') / 'observer',
    exclude_files: other_list,
    strip_directory: true,
)

app_sources = [
  'src/oremind.cpp',
]

foreach source : app_sources
    split = source.split('/')
    base = split[-1]
    name = fs.stem(base)
    sources = []
    
    if name == 'oremind'
        sources = [
          'src/ObserverApp.cpp',
          'src/ObserverFrame.cpp',
          'src/ObservationDialog.cpp',
          'src/StatisticsDialog.cpp',
          'src/WxDialogDriver.cpp',
        ]
    endif
    
    executable(
        name,
        source,
        sources,
        cpp_args: cpp_args,
        cpp_pch: cpp_pch,
        include_directories: lib_inc,
        dependencies: lib_deps,
        link_with: lib_observer,
        install: true,
        install_dir: bindir,
    )
endforeach

bash_files = [
    'oremind.bash',
]

foreach file : bash_files
    name = fs.stem(file)
    install_data(
        file,
        install_dir: datadir / 'bash-completion' / 'completions',
        rename: name,
    )
endforeach

install_data(
    [
        'LICENSE',
        'README.md',
        'README-zh.md',
    ],
    install_dir: pkgdocdir,
)

config_man = configuration_data()
config_man.set('PROJECT_VERSION', meson.project_version())
config_man.set('PROJECT_AUTHOR', project_author)
config_man.set('PROJECT_EMAIL', project_email)
config_man.set('PROJECT_YEAR', project_year)

configure_file(
    input: 'oremind.1.in',
    output: 'oremind.1',
    configuration: config_man,
    install: true,
    install_dir: mandir / 'man1',
)

zh_man_page = configure_file(
    input: 'oremind.zh_CN.1.in',
    output: 'oremind.zh_CN.1',
    configuration: config_man,
)

install_data(
    zh_man_page,
    install_dir: mandir / 'zh_CN' / 'man1',
    rename: 'oremind.1',
)

# look task: list what installed files looks like
# ninja install <tmpdir> && tree <tmpdir> && rm -fr <tmpdir>
run_target(
    'look',
    command: [
        'bash',
        '-euc', '''
            tmpdir=$(mktemp -d)
            DESTDIR=$tmpdir meson install -C "@BUILD_ROOT@"
            tree "$tmpdir"
            rm -fr "$tmpdir"
        ''',
    ],
)

# Fast local debugging against the active installation prefix:
# symlink installed paths to project/build outputs.
run_target(
    'install-symlinks',
    command: [
        'bash',
        '-euc', '''
            prefix="@0@"
            bindir="@1@"
            datadir="@2@"
            localedir="@3@"
            mandir="@4@"

            mkdir -p "$bindir" "$datadir/bash-completion/completions" "$localedir" "$mandir/man1"

            sudo ln -sfn "@BUILD_ROOT@/oremind" "$bindir/oremind"
            sudo ln -sfn "@BUILD_ROOT@/oremind.1" "$mandir/man1/oremind.1"
            sudo ln -sfn "@SOURCE_ROOT@/oremind.bash" "$datadir/bash-completion/completions/oremind"

            printf "Symlinks installed under %s\n" "$prefix"
        '''.format(
            get_option('prefix'), #
            bindir,
            datadir,
            localedir,
            mandir,
        ),
    ],
)

run_target(
    'uninstall-symlinks',
    command: [
        'bash',
        '-euc', '''
            prefix="@0@"
            bindir="@1@"
            datadir="@2@"
            localedir="@3@"
            mandir="@4@"

            for p in \
                "$bindir/oremind" \
                "$mandir/man1/oremind.1" \
                "$datadir/bash-completion/completions/oremind"
            do
                if [ -L "$p" ]; then
                    sudo rm -f "$p"
                    printf "Removed %s\n" "$p"
                else
                    printf "Skipped (not a symlink): %s\n" "$p"
                fi
            done
        '''.format(
            get_option('prefix'), #
            bindir,
            datadir,
            localedir,
            mandir,
        ),
    ],
)

subdir('tests')
