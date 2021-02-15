#!/bin/bash
clang-format -i -style=file `find $d -name '*.cpp' -or -name "*.h" -or -name "*.c" -or -name "*.cc"`

