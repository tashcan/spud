#!/bin/bash

find src -iname \*.h -o -iname \*.cc | xargs clang-format -i
find include -iname \*.h -o -iname \*.cc | xargs clang-format -i
