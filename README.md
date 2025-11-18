# Problem-Set-4

To install vcpkg, open the command line and navigate to a directory of your choice. (Warning, the packages to install are large)
- git clone vcpkg into the directory by typing "**git clone https://github.com/microsoft/vcpkg**"

To build this CMake Project, you need to install vcpkg first and install the following packages using "vcpkg install \package-name\:x64-windows":
-  tesseract
-  leptonica
-  gRPC
-  protobuf

Note: leptonica is usually installed along with tesseract. Also, protobuf is usually installed with gRPC,
so you only need to run the install packages in the Command Line twice.

After installing vcpkg and the packages, type "**vcpkg integrate install**" into the Command Line and copy the directory of your vcpkg.cmake

Afterwards, if you have Qt installed, you need to add your Qt installation path (including the compiler type) to you device's Environmental Variables and name it QT6_PATH.
Then, open the CMakePresets.json in the project files and replace the value of VCPKG_ROOT with the root path of your vcpkg installation.

Once you've done all that, Build the CMakeLists.txt in the root of the project folder. Then build the ones in the client and server folders if they did not build with the root.

Finally, test if the client is working.
