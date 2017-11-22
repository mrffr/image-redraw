#include <Imlib2.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <assert.h>

//bad names
enum line_dir {TLBR, BLTR, TLBL, TRBR, TLTR, BLBR, RAND};

#define LINE_DIR RAND //line drawing direction
//NOTE
//beautiful images from drawing with all lines in one direction

typedef struct{
  uint8_t alpha, red, green, blue; //imlib format
} pixel_t;

typedef struct{
  unsigned int width, height;
  pixel_t *pixels;
} bitmap_t;

typedef struct {
  unsigned int x1, y1, x2, y2;
} box_t;


#define SQR(a)((a)*(a))
#define ABS(a)((a)<0?-(a):(a))

/* distance between color pixels */
static unsigned int dist(pixel_t a, pixel_t b)
{
  return SQR(a.alpha - b.alpha) + SQR(a.red - b.red) +
    SQR(a.green - b.green) + SQR(a.blue - b.blue);
}

static void blank_bmp_copy(bitmap_t *to, unsigned int w, unsigned int h)
{
  to->width = w; to->height = h;
  to->pixels = calloc(to->width * to->height, sizeof(pixel_t));
  if(to->pixels == NULL){ printf("Failed alloc memory\n"); exit(1); }
}

//optimizer takes care of this as it should probably be a macro to ensure inline
static pixel_t * pixel_at (const bitmap_t * bitmap, unsigned int x, unsigned int y)
{
  return bitmap->pixels + bitmap->width * y + x;
}

/* compares two images to the original by checking the difference inside a given bounding box */
static int64_t naive_diff(const bitmap_t *orig, const bitmap_t *new, const bitmap_t *prev, box_t box)
{
  int64_t diff = 0;
  const pixel_t *o_pix, *n_pix, *p_pix;
  for(unsigned int i=box.y1; i<box.y2; i++){
    for(unsigned int j=box.x1; j<box.x2; j++){
      o_pix = pixel_at(orig, j, i);
      n_pix = pixel_at(new, j, i);
      p_pix = pixel_at(prev, j, i);
      diff += dist(*n_pix, *o_pix);
      diff -= dist(*p_pix, *o_pix);
    }
  }
  return diff;
}

/* rand color from anywhere in image */
static const pixel_t * get_rand_col(const bitmap_t *bmp)
{
  unsigned int x = rand()%bmp->width;
  unsigned int y = rand()%bmp->height;
  return pixel_at(bmp, x, y);
}

/* only need to copy part inside bbox */
static void copy_bmp(const bitmap_t *from, bitmap_t *to, box_t bbox)
{
  unsigned int offset = (to->width * bbox.y1) + bbox.x1;
  unsigned int bbox_len = ((to->width * (bbox.y2-1)) + bbox.x2) - offset;
  memcpy(to->pixels + offset, from->pixels + offset, sizeof(pixel_t) * bbox_len);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/* Drawing */
static void draw_box(bitmap_t *bmp, const pixel_t *col, box_t box)
{
  for(unsigned int i=box.y1; i<box.y2; i++)
    for(unsigned int j=box.x1; j<box.x2; j++)
      *(pixel_at(bmp, j, i)) = *col;
}

static void draw_ellipse(bitmap_t *bmp, const pixel_t *col, box_t bbox)
{
  unsigned int hlfw = (bbox.x2 - bbox.x1) / 2, hlfh = (bbox.y2 - bbox.y1)/ 2;
  unsigned int cx = bbox.x1 + hlfw, cy = bbox.y1 + hlfh;
  unsigned int rad_sq = hlfw * hlfh;
  for(unsigned int i= bbox.y1; i<bbox.y2; i++)
    for(unsigned int j= bbox.x1; j<bbox.x2; j++)
      if((SQR(j-cx) + SQR(i-cy)) <= rad_sq){ *(pixel_at(bmp, j, i)) = *col; }
}

/*
  randomly swaps box points so we can get lines travelling in different directions
 */
static box_t box_line(box_t bbox)
{
  unsigned int xs[2] = {bbox.x1, bbox.x2 - 1}; //NOTE why -1??
  unsigned int ys[2] = {bbox.y1, bbox.y2 - 1};
  unsigned int rx1 = 0, ry1 = 0, rx2 = 1, ry2 = 1;

  switch(LINE_DIR){
  case TLBR:
    rx1 = 0, ry1 = 0, rx2 = 1, ry2 = 1;
    break;
  case BLTR:
    rx1 = 1, ry1 = 0, rx2 = 0, ry2 = 1;
    break;
  case TLBL: //down
    rx1 = 0, ry1 = 0, rx2 = 0, ry2 = 1;
    break;
  case TRBR: //down
    rx1 = 0, ry1 = 0, rx2 = 0, ry2 = 1;
    break;
  case TLTR: //side
    rx1 = 0, ry1 = 0, rx2 = 1, ry2 = 0;
    break;
  case BLBR: //side
    rx1 = 0, ry1 = 1, rx2 = 1, ry2 = 1;
    break;
  case RAND:
    rx1 = rand()%2, ry1 = rand()%2;
    rx2 = rand()%2, ry2 = rand()%2;
    break;
  }
  return (box_t){xs[rx1], ys[ry1], xs[rx2], ys[ry2]};
}

/* line drawing using bresenhams algorithm */
static void draw_line(bitmap_t *bmp, const pixel_t *col, box_t bbox)
{
  box_t bb2 = box_line(bbox); //allows us to draw lines in diff directions
  int x1 = bb2.x1, y1 = bb2.y1, x2 = bb2.x2, y2 = bb2.y2;

  int dx = ABS(x2 - x1), sx = x1<x2 ? 1:-1;
  int dy = ABS(y2 - y1), sy = y1<y2 ? 1:-1;
  int err = (dx > dy ? dx : -dy)/2;
  for(;;){
    *(pixel_at(bmp, x1, y1)) = *col;
    if(x1 == x2 && y1 == y2) break;
    int e2 = err;
    if(e2 > -dx){ err -= dy; x1+=sx; }
    if(e2 < dy) { err += dx; y1+=sy; }
  }
}

#define SWAP(T,x,y) do {T SWAP = x; x = y; y = SWAP; } while (0)

/* xiaolin wu line drawing algorithm anti-aliased lines */
static void wu_draw_line(bitmap_t *bmp, const pixel_t *col, box_t bbox)
{
  box_t bb2 = box_line(bbox); //
  int x1 = bb2.x1, y1 = bb2.y1, x2 = bb2.x2, y2 = bb2.y2;

  pixel_t col_pix = *col;

  int steep = ABS(y2 - y1) > ABS(x2 - x1);
  int tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2;
  if (steep){
    SWAP(int,x1, y1);
    SWAP(int,x2, y2);
  }
  if (x1 > x2){
    SWAP(int,x1, x2);
    SWAP(int,y1, y2);
  }

  float dx = x2 - x1;
  float dy = y2 - y1;
  float gradient;
  //div by 0
  if(dx == 0.0f) gradient = 1.0f;
  else gradient = dy / dx;

  //handle first endpoint
  if(steep){
    col_pix.alpha = col->alpha >> 1;
    *(pixel_at(bmp, tx1, ty1)) = col_pix;
    *(pixel_at(bmp, tx2, ty2)) = col_pix;
  }else{
    col_pix.alpha = col->alpha >> 1; //
    *(pixel_at(bmp, x1, y1)) = col_pix;
    *(pixel_at(bmp, x2, y2)) = col_pix;
  }

  if(steep){
    float interx = tx1 + gradient;
    for(unsigned int ty = ty1+1; ty < ty2; ty++){
      /* if((int) interx >= x2) break; //not needed */

      col_pix.alpha = col->alpha * (1 - (interx - (int)interx)); //255 * 0.5
      *(pixel_at(bmp, (unsigned int) interx, ty)) = col_pix;
      col_pix.alpha = col->alpha * (interx - (int)interx); //255 * 0.5
      *(pixel_at(bmp, (unsigned int) interx + 1, ty)) = col_pix;
      interx += gradient;
    }
  }else{
    float intery = y1 + gradient;
    for(unsigned int x = x1+1; x < x2; x++){
      if((int) intery >= y2) break; //this should not be needed did I miss something??

      col_pix.alpha = col->alpha * (1 - (intery - (int)intery)); //255 * 0.5
      *(pixel_at(bmp, x, (unsigned int) intery)) = col_pix;
      col_pix.alpha = col->alpha * (intery - (int)intery); //255 * 0.5
      *(pixel_at(bmp, x, (unsigned int) intery + 1)) = col_pix;
      intery += gradient;
    }
  }
}

/* colour random pixels in bounding box */
static void draw_scatter(bitmap_t *bmp, const pixel_t *col, box_t bbox)
{
  unsigned int w = bbox.x2 - bbox.x1, h = bbox.y2 - bbox.y1;
  unsigned int iters = (w*h)*0.1; //10% of pixels approx
  for(unsigned int i=iters; i>0; i--){
    unsigned int x = bbox.x1 + rand()%w;
    unsigned int y = bbox.y1 + rand()%h;
    *(pixel_at(bmp, x, y)) = *col;
  }
}

static void draw_triangle(bitmap_t *bmp, const pixel_t *col, box_t bbox)
{
  unsigned int w = bbox.x2 - bbox.x1, h = bbox.y2 - bbox.y1;
  for(unsigned int y=bbox.y1; y <bbox.y2 && w > 0; y++, w--){
    for(unsigned int x=bbox.x1; x<bbox.x1+w; x++){
      *(pixel_at(bmp, x, y)) = *col;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/* creates a random sized box for drawing shapes in
 * no point in making top left, bot right swapped boxes
 * since rect/ellipse doesn't care
 */
static box_t make_box(unsigned int width, unsigned int height)
{
  unsigned int x1 = rand()%width, y1 = rand()%height;
  unsigned int w = 10 + (rand()%20), h = 10 + (rand()%20);
  unsigned int x2 = ((x1+w) > width) ? width : (x1+w);
  unsigned int y2 = ((y1+h) > height) ? height : (y1+h);
  return (box_t){x1, y1, x2, y2};
}

/* loop creates blank images. Then draws on one of them.
 * Compare the images to original.
 * Use the closer one for the next loop.
 * As such incrementally create an approximation of the original image
 */
static bitmap_t * draw_loop(const bitmap_t *orig)
{
  //a on heap since we are going to return it
  bitmap_t *a = malloc(sizeof(bitmap_t)), b;
  if(a == NULL){ printf("Failed alloc memory\n"); exit(1); }
  blank_bmp_copy(a, orig->width, orig->height);
  blank_bmp_copy(&b, orig->width, orig->height);

  //randomly select from drawing functions
  void (*fs[6])(bitmap_t*,const pixel_t*,box_t) = {
    &draw_line, &draw_box, &draw_ellipse, &draw_scatter, &wu_draw_line, &draw_triangle
  }; //should be replaced with something else
  int choice = rand()%6;

  int iters = 10e5; //
  for(; iters--;){
    //bounding box in which changes will take place
    box_t bbox = make_box(orig->width, orig->height);

    const pixel_t *col = get_rand_col(orig); //can replace this with gradient
    /* int choice = rand()%4; */ //random choice each iteration
    fs[choice](a, col, bbox); //random choice

    //calculate differences keep the one closest to original
    int64_t diff = naive_diff(orig, a, &b, bbox);
    if(diff >= 0){
      copy_bmp(&b, a, bbox);
    }else{
      copy_bmp(a, &b, bbox);
    }
  }
  free(b.pixels);
  return a;
}

/* for imlib need to load image then use ro data for orig image
 * the actual loop only cares about raw pixels so we operate on them
 * then at end convert back to imlib image
 */
int main(int argc, char **argv)
{
  if(argc != 3){
    printf("usage: %s input_image output_image\n", argv[0]);
    exit(1);
  }

  char *out_fmt = strrchr(argv[2], '.');
  if(out_fmt == NULL){ printf("Output file has no format.\n"); exit(1); }

  srand(time(NULL));

  Imlib_Image input_image = imlib_load_image(argv[1]);
  if(!input_image){ printf("Failed to open input image.\n"); exit(1); }
  imlib_context_set_image(input_image);

  unsigned int w = imlib_image_get_width();
  unsigned int h = imlib_image_get_height();

  bitmap_t orig = {.width=w, .height=h};
  orig.pixels = (pixel_t*) imlib_image_get_data_for_reading_only();

  //main loop
  bitmap_t * a = draw_loop(&orig);

  imlib_free_image(); //free input_image

  //create new image, copy pixels and then save it
  Imlib_Image output_image = imlib_create_image(w, h);
  imlib_context_set_image(output_image);
  pixel_t *pixs = (pixel_t *) imlib_image_get_data();
  memcpy(pixs, a->pixels, sizeof(pixel_t) * w * h);
  imlib_image_put_back_data((unsigned int *) pixs);

  free(a->pixels);

  imlib_image_set_format(out_fmt + 1);
  imlib_save_image(argv[2]);

  imlib_free_image(); //free output_image
  return 0;
}
