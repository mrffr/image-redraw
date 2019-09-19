# Image Redraw

This program takes an input image and attempts to redraw the image using some basic shapes.

Usage:
```
redraw [-n iterations] [-f draw_function] input_image output_image
        -f <line, box, ellipse, scatter, wu_line, tri, poly, rand>
```

The example below shows the result of asking the program to redraw the first photograph using only ellipses and a few million iterations. Each iteration gets progressively closer to the original as the program only keeps the result of drawing shape if it is closer to the original than not drawing the shape.

original

![Original](/examples/Calm.png)

ellipse

![Circular](/examples/round_.png)
