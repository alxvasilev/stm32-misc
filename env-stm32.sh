owndir=`echo "$(cd $(dirname "${BASH_SOURCE[0]}"); pwd)"`

if [ "$#" != "1" ]; then
    STM32_SYSROOT=$owndir
    echo "Defaulting sysroot to script dir"
else
    STM32_SYSROOT=`readlink -f $1`
fi

if [ ! -d "$STM32_SYSROOT" ]; then
   echo -e "\033[1;31msysroot '$STM32_SYSROOT' dir does not exist\033[0m"
   return 2
fi


export STM32_LINK_SCRIPT="$STM32_SYSROOT/stm32.ld"
export OPT_STM32_CHIP_FAMILY=STM32F10X_MD

STM32_CMAKE_TOOLCHAIN="$STM32_SYSROOT/stm32-toolchain.cmake"

# Create cmake toolchain file
echo -e "\n\
#Generated file, do not edit by hand
cmake_policy(VERSION 3.4)\n\
set(CMAKE_SYSTEM_NAME Generic)\n\
set(CMAKE_SYSTEM_PROCESSOR arm)\n\
set(CMAKE_SYSROOT \"$STM32_SYSROOT\")\n\
\n\
set(CMAKE_C_COMPILER arm-none-eabi-gcc)\n\
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)\n\
set(CMAKE_ASM_COMPILER arm-none-eabi-as)\n\
set(CMAKE_LINKER arm-none-eabi-ld)\n\
\n\
set(CMAKE_C_FLAGS \"\${CMAKE_C_FLAGS} -fno-common -mcpu=cortex-m3 -mthumb -nostartfiles\" CACHE STRING \"\")
set(CMAKE_CXX_FLAGS  \"\${CMAKE_CXX_FLAGS} \${CMAKE_C_FLAGS}\" CACHE STRING \"\")
\n\
set(optLinkScript \"$STM32_LINK_SCRIPT\" CACHE PATH \"Linker script\")\n\
set(optChipFamily STM32F10X_MD CACHE STRING \"Chip family for platform headers\")\n\
set_property(CACHE optChipFamily PROPERTY STRINGS STM32F10X_LD STM32F10X_LD_VL\n\
    STM32F10X_MD STM32F10X_MD_VL STM32F10X_HD STM32F10X_HD_VL STM32F10X_XL STM32F10X_CL)\n\
\n\
add_definitions(-D\${optChipFamily})\n\
set(CMAKE_EXE_LINKER_FLAGS \"-nostartfiles -T\${optLinkScript}\" CACHE STRING \"\")\n\
\n\
set(CMAKE_FIND_ROOT_PATH \"$STM32_SYSROOT\")\n\
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n\
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n\
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n\
\n\
set(CMAKE_BUILD_TYPE Debug CACHE STRING \"Build type\")\n\
\n\
" > "$STM32_CMAKE_TOOLCHAIN"

export CMAKE_XCOMPILE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$STM32_CMAKE_TOOLCHAIN"

function xcmake
{
    cmake $CMAKE_XCOMPILE_ARGS $@
}

echo -e "Your environment has been set up for STM32 cross-compilation"

