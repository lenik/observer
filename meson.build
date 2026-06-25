project(
    'source2native',
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
            echo "1.0.0"
        fi
        ''',
        check: false,
    ).stdout().strip(),
    license: 'AGPL-3.0-or-later',
    meson_version: '>=0.59',
    default_options: ['warning_level=0'],
)

project_author = 'Lenik'
project_email = 'source2native@bodz.net'
project_year = 2026

py = import('python').find_installation('python3')

cfg = configuration_data()
cfg.set('MESON_PROJECT_VERSION', meson.project_version())

configure_file(
    input: 'source2native.py',
    output: 'source2native',
    configuration: cfg,
    install: true,
    install_mode: 'rwxr-xr-x',
    install_dir: get_option('bindir'),
)

config_man = configuration_data()
config_man.set('PROJECT_VERSION', meson.project_version())
config_man.set('PROJECT_AUTHOR', project_author)
config_man.set('PROJECT_YEAR', project_year)

configure_file(
    input: 'source2native.1.in',
    output: 'source2native.1',
    configuration: config_man,
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man1'),
)

install_data(
    'source2native.bash-completion',
    install_dir: join_paths(get_option('datadir'), 'bash-completion', 'completions'),
    rename: 'source2native',
)

install_data(
    ['LICENSE', 'README.md'],
    install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name()),
)

run_target(
    'unit-tests',
    command: [
        py,
        '-c', 'import sys; sys.path.insert(0, "@SOURCE_ROOT@"); '
        + 'from source2native import strip_debian_revision, VERSION; '
        + 'assert strip_debian_revision("3.36.1-1") == "3.36.1"; '
        + 'assert strip_debian_revision("2.0.2+debian-3") == "2.0.2+debian"; '
        + 'assert VERSION; '
        + 'print("ok")',
    ],
)

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
