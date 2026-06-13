check_dep = dependency('check', required: true)

cpp_pch = '../' + cpp_pch
lib_inc = include_directories('../src', '..')

test_sources = [
]

foreach test_src : test_sources
    split = test_src.split('/')
    base = split[split.length() - 1]
    dir = fs.parent(test_src)
    stem_test = fs.stem(base)

    if stem_test.endswith('_test')
        name = stem_test.replace('_test', '')
    elif stem_test.endswith('_unit')
        name = stem_test.replace('_unit', '')
    else
        continue
    endif

    sibling_dir = '../src' / dir

    # Use 'includes' utility to get implementation dependencies
    # 'includes -e -c test.cpp' returns source files, one per line (not -I include paths)

    use_includes = false
    if use_includes
        includes_result = run_command('includes', '-e', '-c', test_src, check: false)

        impl_deps = []
        if includes_result.returncode() == 0
            # Parse the output: each line is a .c/.cpp file path
            output = includes_result.stdout()
            lines = output.split('\n')
            foreach line : lines
                line = line.strip()
                # Keep non-empty lines that are .c/.cpp files
                if line.endswith('.cpp') or line.endswith('.c')
                    impl_deps += line
                endif
            endforeach
        endif
    else
        impl_deps = []
        # if fs.exists(sibling_dir / stem + '.c')
        #     impl_deps = [sibling_dir / stem + '.c']
        # endif
    endif

    # Create the executable (LOGGER_NAME so program.h and logger.c work in tests)
    test_exe = executable(
        name,
        test_src,
        impl_deps,
        cpp_args: cpp_args,
        cpp_pch: cpp_pch,
        include_directories: [sibling_dir, lib_inc],
        link_with: lib_observer_static,
        dependencies: [lib_deps, check_dep],
        install: false,
    )

    test(name, test_exe)
endforeach
