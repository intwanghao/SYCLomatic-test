# ====------ do_test.py--------------------------------- *- Python -* ----===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
import subprocess
import platform
import os
import sys

from test_utils import *

def setup_test():
    change_dir(test_config.current_test)
    call_subprocess("cp -r --dereference " + test_config.include_path + " + .")
    change_dir("src")
    return True

def migrate_test():
    command = test_config.CT_TOOL + " --out-root out test.cu --cuda-include-path ../include --extra-arg=\"-I" + test_config.include_path + "\""
    print(command)
    return call_subprocess(command)

def build_test():
    return call_subprocess("icpx -fsycl out/test.dp.cpp")
def run_test():
    return True
