Building GStreamer on Windows
-----------------------------

Running GStreamer on Windows is currently experimental, but improving.

Building on MinGW/MSys
----------------------

Should work out of the box from the toplevel directory using the standard
Unix build system provided.

This build type is fairly well supported.

Building with Visual Studio 6
-----------------------------

The directory vs6/ contains the workspaces needed to build GStreamer from
Visual Studio.

This build type is fairly well supported.

Building with Visual Studio 7
-----------------------------

vs7/ contains the files needed, but they haven't been updated since the
0.8 series.

This build is currently unsupported.

The common/ directory contains support files that can be shared between
these two versions of Visual Studio.

Building with Visual Studio 2019
--------------------------------

Directory vs16/ contains the Visual Studio 2019 solution to build gstreamer
libraries and tools.

In order to build gstreamer with Visual Studio 2019, you must define 2 environment
variables pointing at your Cerbero prefixes for the 32 bits and 64 bits versions:

- **VS_CERBERO_PREFIX_X86** for 32 bits (ex: Z:\fluendo-sdk-devel\dist\windows_x86)
- **VS_CERBERO_PREFIX_X86_64** for 64 bits (ex: Z:\fluendo-sdk-devel\dist\windows_x86_64)

To add a new environment variable on Windows, follow these steps:

1. Open "Run..." menu (Win+R)
2. Run sysdm.cpl
3. Select "Advanced" Tab and click on "Environment Variables..."
4. Add a "New..." variable in "User variables..." or "System variables..." if you want it
   to be accessible only by current user or by all users.

You will also need MSYS-1.0 to be installed into *C:\MinGW\msys\1.0*
