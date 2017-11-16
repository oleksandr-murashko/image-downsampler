# image-downsampler
This program reduces image resolution by resampling it into the smaller size using Fant method - a geometrically accurate anti-aliasing technique. Each target pixel is calculated as a weighted average of the related pixels. For example, here is how 8x8 pixels image is resampled into 3x3:

![screenshot](https://user-images.githubusercontent.com/21956223/32907452-f6e6f4b8-caf7-11e7-98db-e1a1ca50f0f7.png)

The application is optimized with Intel AVX/AVX2 instructions for simultaneous processing of all three colour channels. Therefore, it requires a modern CPU to run.

The program also needs a Qt library for reading and writing various image formats. The input images can be in BMP, JPG, PNG and TIFF formats. The output format by default is determined by extension (*.png is a recommended option).

In order to build the application use Qt Creator or type in Linux command line: `qmake image-downsampler.pro && make`.

Run application using the following syntax:
```
image-downsampler <input_image> <output_image> <target_width> <target_height>
```
