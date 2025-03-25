# Label Printer

This dead-simple program prints bitmap files. Specifically made to send files
to a Brady label printer, because how I was doing it before was too annoying.

Brady's driver is designed for Windows, and I'm using WIN32 APIs to make the
program work. The upside is there's no additional libraries needed!

Built using MinGW and CMake - so make sure you have those if you want to
compile it yourself.

## Usage

Generate a monochrome bitmap and include the resolution information to
declare how big each pixel is (pixels-per-meter in the BMP header). This
gives you full control over the resolution, and gets automatically converted
to the printer's resolution. Because there's no warping, beyond converting a
pixel in your pixel space to that of the printer, your label will come out
as-drawn.

Here's how you use it:

```
labelprinter.exe --paper-type 'my_custom_paper' my_label.bmp
```

## Building

You'll need CMake, Ninja (or Make), and a C compiler. I used MinGW.

```
cmake -G Ninja -B build
cmake --build build
```

Your binary will be `labelprinter.exe` in the specified output folder (`build`
in the example above).
