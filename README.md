# About
### Build
- Notes: only supported setup is Windows with MSVC compiler, supporting c++23 (i.e. std latest in Visual Studio), using Visual Studio build system. Dependancies are manged using vcpkg.
- To build you may have to update the solution properties for include or lib directories, the default vcpkg dir is at C:\dev\vcpkg\installed\x64-windows\bin

Dependancies:
- Eigen (vcpkg) for linear algebra, matrix multiplication, etc...
- Rapidjson (vcpkg) for serialization
- Intel TBB (see intel's download page) for multithreading

Build Process:
- run: git clone https://github.com/ntorm1/AgisCore
- run: cd AgisCore
- run: git submodule update --init --recursive
- Open AgisCore.sln and build.

Once Built:
- Add AgisCore/include to your include path
- Add AgisCore.lib to linker
- Make AgisCore.dll available at run time.