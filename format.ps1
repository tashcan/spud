Get-ChildItem -Path src -Directory -Recurse |
    foreach {
        pushd $_.FullName
        gci . *.h | where { ! $_.PSIsContainer } | 
        foreach {
            &clang-format -i $_.FullName
        }
        gci . *.cc | 
        foreach {
            &clang-format -i $_.FullName
        }
        popd
    }

Get-ChildItem -Path include -Directory -Recurse |
    foreach {
        pushd $_.FullName
        gci . *.h | where { ! $_.PSIsContainer } | 
        foreach {
            &clang-format -i $_.FullName
        }
        gci . *.cc | 
        foreach {
            &clang-format -i $_.FullName
        }
        popd
    }