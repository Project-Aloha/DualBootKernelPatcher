# Multi-Boot Kernel Patcher
> Inspired by [SurfaceDuoDualBootKernelImagePatcher
](https://github.com/WOA-Project/SurfaceDuoDualBootKernelImagePatcher)  

Multi-Boot Kernel Patcher packages a base ARM64 Linux Image, a compatible
Shim runtime, and multiple bootable images from one INI configuration.

## Build
  - Preparation
    + A computer with Windows or Linux
    + Clang or GCC as host compiler
    + aarch64 GNU Assembler
    + Git
    + CMake

  - Install dependencies
    + Linux (Debian / Ubuntu)
      ```sh
      sudo apt install build-essential cmake gcc-aarch64-linux-gnu git
      ```
    + Windows
      Install CMake and a Visual Studio C compiler. The inih dependency is
      built from the checked-out `lib/inih` submodule.
    + macOS
      Install CMake and Xcode Command Line Tools. The inih dependency is built
      from the checked-out `lib/inih` submodule.
  - Clone this repo
    ```
    git clone https://github.com/Project-Aloha/DualBootKernelPatcher
    cd DualBootKernelPatcher
    git submodule update --init --recursive
    ```
  - Setup CMake.
    ```
    cd MultiBootKernelPatcher
    cmake -B output -S .
    ```
  - Build!
    ```
    cmake --build output -j 12
    ```
## Usage
  - Copy and edit configuration file, fill paths in it.
  - Pack an image from the configuration file.
    ```sh
    MultiBootKernelPatcher Config/Shim.Sample.cfg
    ```
  - Notice
    + ShimLoader binaries are generated under `output/ShimLoader/`.
    + Simplified assembly Shim binaries are generated under `output/Shims/`.

## See More
  - You can go to our [document website](https://aloha.firmware.icu/) to get more infomation about the Multi-Boot Kernel Patcher.

## License
  MIT License.
