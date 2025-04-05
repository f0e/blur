#!/usr/bin/env fish

# https://gist.github.com/alin23/4e1bf49e8fa01db94235dedbceb101d3

# Needs rg: `brew install ripgrep`

set -xg visitedLibs

function libdeps -a bin depth -d "Finds all dylibs that a binary depends on recursively and prints them on separate lines, sorted alphabetically"
    if test -z "$depth"
        set depth 0
    end
    if not test -f $bin
        set bin (realpath (which $bin))
    end
    set binname (basename $bin)
    set bindir (dirname $bin)

    set bindeps (otool -L $bin | rg -o --no-filename -i '((?:/opt/homebrew|/usr/local|@loader_path|@@HOMEBREW_PREFIX@@|@rpath).+\.dylib)' -r '$1')
    for dep in $bindeps
        set realDylib (realpath (string replace "@@HOMEBREW_PREFIX@@" /usr/local -- (string replace "@loader_path" $bindir -- (string replace "@rpath" /usr/local/lib -- $dep))))
        if test -f $realDylib; and not test "$realDylib" = "$bin"; and not contains $realDylib $visitedLibs
            set -xg visitedLibs $visitedLibs $realDylib
            libdeps $realDylib (math $depth + 1)
        end
    end

    if test $depth -eq 0
        set depsfile $(mktemp -t "$binname")
        for lib in $visitedLibs
            echo $lib >> $depsfile
        end
        cat $depsfile | sort | uniq
        set -xg visitedLibs
    end
end

function __change_libs_path -a bin -d "Changes the search path of non-system dylibs in a binary to @executable_path"
    set binname (basename $bin)

    for dylib in (otool -L $bin | rg -o --no-filename -i '((?:/opt/homebrew|/usr/local|@loader_path|@@HOMEBREW_PREFIX@@|@rpath).+\.dylib)' -r '$1')
        set libver (basename -s .dylib $dylib | string split -m 2 '.')
        if test (count $libver) -eq 1
            set locallib $libver[1].*
        else
            set locallib $libver[1].$libver[2].*
        end
        if test -z "$locallib"
            set locallib $libver[1].*
        end
        echo install_name_tool (set_color -o yellow)-change $dylib @executable_path/$locallib $binname(set_color normal)
        install_name_tool -change $dylib @executable_path/$locallib $binname 2>&1 | rg 'error'
    end
end

function collect-bin-with-deps -d "Collects a binary and all its dependencies into a temporary directory, and changes the search path of non-system dylibs to @executable_path"
    argparse -n collect-bin-with-deps h/help s/sign= -- $argv

    set bin $argv[1]
    if set -q _flag_help || test -z "$bin"
        echo "Usage: collect-bin-with-deps [-s|--sign <identity>] <bin>"
        echo "Collects a binary and all its dependencies into a temporary directory, and changes the search path of non-system dylibs to @executable_path"
        echo
        echo "Options:"
        echo "  -s, --sign <identity>  Sign the binary and its dylibs with the given identity"
        echo "  -h, --help             Show this help message and exit"
        return
    end

    set start_dir (pwd)  # Store the starting directory

    set dir $(mktemp -d -t (basename $bin))
    cd "$dir"

    if not test -f $bin
        set bin (realpath (which $bin))
    end
    set binname (basename $bin)
    set bindir (dirname $bin)

    sudo cp $bin $dir
    sudo cp $(libdeps $bin) $dir
    sudo chown $(whoami) *
    sudo chmod 755 *

    echo \n(set_color -o green)$binname:(set_color normal)
    __change_libs_path $bin

    for dylib in *.dylib
        echo \n(set_color -o green)$(basename $dylib):(set_color normal)

        echo install_name_tool (set_color -o blue)-id (basename $dylib) $dylib(set_color normal)
        install_name_tool -id (basename $dylib) $dylib 2>&1 | rg 'error'

        __change_libs_path $dylib

        sudo cp "$dylib" "$start_dir/out/vapoursynth" # i added
    end

    strip -rSTx -no_code_signature_warning *

    if set -q _flag_sign
        set entitlements (mktemp -t entitlements)
        echo '<?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
        <key>com.apple.security.cs.allow-jit</key>
        <true/>
        <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
        <true/>
        </dict>
        </plist>
        ' > "$entitlements"

        codesign -fs "$_flag_sign" \
            --options runtime \
            --entitlements "$entitlements" \
            --timestamp *
    end
end
