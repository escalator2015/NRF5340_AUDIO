#!/bin/bash
# ncs_env.sh — Activar entorno NCS v3.2.2 para builds
# Generado automáticamente desde toolchains/927563c840/environment.json

TC=/home/aa/ncs/toolchains/927563c840

export PATH="${TC}/bin:${TC}/usr/bin:${TC}/usr/local/bin:${TC}/opt/bin:${TC}/opt/nanopb/generator-bin:${TC}/nrfutil/bin:${TC}/opt/zephyr-sdk/arm-zephyr-eabi/bin:${TC}/opt/zephyr-sdk/riscv64-zephyr-elf/bin:${PATH}"
export LD_LIBRARY_PATH="${TC}/lib:${TC}/lib/x86_64-linux-gnu:${TC}/usr/local/lib:${LD_LIBRARY_PATH}"
export GIT_EXEC_PATH="${TC}/usr/local/libexec/git-core"
export GIT_TEMPLATE_DIR="${TC}/usr/local/share/git-core/templates"
export PYTHONHOME="${TC}/usr/local"
export PYTHONPATH="${TC}/usr/local/lib/python3.12:${TC}/usr/local/lib/python3.12/site-packages"
export NRFUTIL_HOME="${TC}/nrfutil/home"
export ZEPHYR_SDK_INSTALL_DIR="${TC}/opt/zephyr-sdk"
export CMAKE_PREFIX_PATH="${TC}/opt/zephyr-sdk/cmake"

source "${HOME}/ncs/v3.2.2/zephyr/zephyr-env.sh"

echo "NCS v3.2.2 environment activated (toolchain: 927563c840)"
