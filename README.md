# ERS-Template Project
This template project is used as a starting point for a project that uses ERS.
Within this README we go over some of the prerequisites before using ERS

## Requirements
Before starting it is assumed a C/C++ compatible compiler which supports C++20 is preinstalled, if this isn't the case we recommend MSVC(Visual studio) on windows and GCC or Clang on linux distributions.

ERS also requires CMake to be installed and added to the path

ERS relies on having one environment variable set for VCPKG.
VCPKG is a package manager we use to build software from source

### Windows
Create a directory to clone vcpkg in this example we use C:/Local
```bash
mkdir C:/local
cd C:/local
git clone https://github.com/microsoft/vcpkg
```
Then add a new variable called VCPKG_ROOT to your systems environment variables. This variable needs to point to ```C:/Local/vcpkg```

### Linux
Create a directory to clone vcpkg in this example we use a user's home directory
```bash
cd ~
git clone https://github.com/microsoft/vcpkg
```
Then add a new variable called VCPKG_ROOT to your systems environment variables. This variable needs to point to ```/home/CHANGEME/vcpkg``` please change CHANGEME into the current user.


## Building
In order to build an executable on top of ERS there are a few steps, they will be similar on other operating systems.

```cmake
# Execute this in a shell from inside template project directory

mkdir build
cd build
cmake .. # This downloads the latest ERS build depending on branch & version
cmake --build . # Optionally add -j16 to build with 16 threads or more if needed
```