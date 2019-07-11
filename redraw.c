#include <Imlib2.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

enum e_draw_funcs {D_LINE, D_BOX, D_ELLIPSE, D_SCATTER, D_WU_LINE, D_TRI, D_POLY, D_RAND}; //D_RAND must be last

enum e_line_dir {TLBR, BLTR, TLBL, TRBR, TLTR, BLBR, DIR_RAND};

enum e_line_dir LINE_DIR = DIR_RAND; //line drawing direction
int g_ITERS  = 1e6;

typedef struct{
  uint8_t alpha, red, green, blue; //imlib format
} Pixel_t;

typedef struct{
  unsigned int width, height;
  Pixel_t *pixels;
} Bitmap_t;

typedef struct {int x,y;} Point_t;

typedef struct {
  Point_t p1, p2;
} Box_t;

static void draw_line(Bitmap_t*, const Pixel_t*, Box_t);
static void draw_box(Bitmap_t*, const Pixel_t*, Box_t);
static void draw_ellipse(Bitmap_t*, const Pixel_t*, Box_t);
static void draw_scatter(Bitmap_t*, const Pixel_t*, Box_t);
static void wu_draw_line(Bitmap_t*, const Pixel_t*, Box_t);
static void draw_triangle(Bitmap_t*, const Pixel_t*, Box_t);
static void draw_poly(Bitmap_t*, const Pixel_t*, Box_t);

void (*draw_funcs[])(Bitmap_t*,const Pixel_t*,Box_t) = {
  &draw_line, &draw_box, &draw_ellipse, &draw_scatter, &wu_draw_line, &draw_triangle, &draw_poly,
};

static enum e_draw_funcs get_draw_func_opt(char *optarg)
{
  if(!strcmp(optarg, "line")) return D_LINE;
  if(!strcmp(optarg, "box")) return D_BOX;
  if(!strcmp(optarg, "ellipse")) return D_ELLIPSE;
  if(!strcmp(optarg, "scatter")) return D_SCATTER;
  if(!strcmp(optarg, "wu_line")) return D_WU_LINE;
  if(!strcmp(optarg, "tri")) return D_TRI;
  if(!strcmp(optarg, "poly")) return D_POLY;
  if(!strcmp(optarg, "rand")) return D_RAND;
  return D_RAND;
}


#define SQR(a)((a)*(a))
#define ABS(a)((a)<0?-(a):(a))
#define SWAP(T,x,y) do {T SWAP = x; x = y; y = SWAP; } while (0)

/* distance change this for effect between color pixels */
static unsigned int dist(Pixel_t a, Pixel_t b)
{
  return SQR(a.alpha - b.alpha) + SQR(a.red - b.red) +
    SQR(a.green - b.green) + SQR(a.blue - b.blue);
}

static void blank_bmp_copy(Bitmap_t *to, unsigned int w, unsigned int h)
{
  to->width = w; to->height = h;
  to->pixels = calloc(to->width * to->height, sizeof(Pixel_t));
  if(to->pixels == NULL){ fprintf(stderr, "Failed alloc memory\n"); exit(EXIT_FAILURE); }
}

//compiler takes care of this as it should probably be a macro to ensure inline
static Pixel_t * pixel_at (const Bitmap_t * bitmap, unsigned int x, unsigned int y)
{
  assert(x < bitmap->width);
  assert(y < bitmap->height);
  /* if(y == bitmap->height) y -= 1; */
  return bitmap->pixels + bitmap->width * y + x;
}

/* compares two images to the original by checking the difference inside a given bounding box */
static int64_t naive_diff(const Bitmap_t *orig, const Bitmap_t *new, const Bitmap_t *prev, Box_t box)
{
  int64_t diff = 0;
  const Pixel_t *o_pix, *n_pix, *p_pix;
  for(unsigned int i=box.p1.y; i<box.p2.y; i++){
    for(unsigned int j=box.p1.x; j<box.p2.x; j++){
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
static const Pixel_t * get_rand_col(const Bitmap_t *bmp)
{
  unsigned int x = rand()%bmp->width;
  unsigned int y = rand()%bmp->height;
  return pixel_at(bmp, x, y);
}

/* only need to copy part inside bbox */
static void copy_bmp(const Bitmap_t *from, Bitmap_t *to, Box_t bbox)
{
  unsigned int width = bbox.p2.x - bbox.p1.x;
  for(unsigned int i=bbox.p1.y; i<bbox.p2.y; i++){
    unsigned int start = (to->width * i) + bbox.p1.x;
    memcpy(to->pixels + start, from->pixels + start, sizeof(Pixel_t) * width);
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/* Drawing */
static void draw_box(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  for(unsigned int i=bbox.p1.y; i<bbox.p2.y; i++)
    for(unsigned int j=bbox.p1.x; j<bbox.p2.x; j++)
      *(pixel_at(bmp, j, i)) = *col;
}


static void draw_ellipse(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  unsigned int hlfw = (bbox.p2.x - bbox.p1.x) /2, hlfh = (bbox.p2.y - bbox.p1.y)/ 2;
  unsigned int cx = bbox.p1.x + hlfw, cy = bbox.p1.y + hlfh;
  unsigned int rad_sq = hlfw * hlfh;
  for(unsigned int i= bbox.p1.y; i<bbox.p2.y; i++)
    for(unsigned int j= bbox.p1.x; j<bbox.p2.x; j++)
      if((SQR(j-cx) + SQR(i-cy)) <= rad_sq){ *(pixel_at(bmp, j, i)) = *col; }
}

/*
  randomly swaps box points so we can get lines travelling in different directions
 */
static Box_t box_line(Box_t bbox)
{
  int xs[2] = {bbox.p1.x, bbox.p2.x - 1};
  int ys[2] = {bbox.p1.y, bbox.p2.y - 1};
  unsigned int rx1 = 0, ry1 = 0, rx2 = 1, ry2 = 1;

  enum e_line_dir choice = LINE_DIR;
  if(choice == DIR_RAND) choice = rand()%DIR_RAND;

  switch(choice){
  case TLBR:
    rx1 = 0, ry1 = 0, rx2 = 1, ry2 = 1;
    break;
  case BLTR:
    rx1 = 0, ry1 = 1, rx2 = 1, ry2 = 0;
    break;
  case TLBL: //down
    rx1 = 0, ry1 = 0, rx2 = 0, ry2 = 1;
    break;
  case TRBR: //down
    rx1 = 1, ry1 = 0, rx2 = 1, ry2 = 1;
    break;
  case TLTR: //side
    rx1 = 0, ry1 = 0, rx2 = 1, ry2 = 0;
    break;
  case BLBR: //side
    rx1 = 0, ry1 = 1, rx2 = 1, ry2 = 1;
    break;
  default:
    break;
  /* case DIR_RAND: */
  /*   while(1){ //needs to be fixed */
  /*     rx1 = rand()%2, ry1 = rand()%2; */
  /*     rx2 = rand()%2, ry2 = rand()%2; */
  /*     if(rx1 <= rx2 && ry1 <= ry2 && (rx1!=rx2 || ry1!=ry2)) break; //the points aren't identical and 2 is larger than 1 */
  /*   } */
  /*   break; */
  }
  /* printf("choice: %i\n",choice); */
  return (Box_t){{xs[rx1], ys[ry1]}, {xs[rx2], ys[ry2]}};
}

/* line drawing using bresenhams algorithm */
static void draw_line(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  Box_t bb2 = box_line(bbox); //allows us to draw lines in diff directions
  int x1 = bb2.p1.x, y1 = bb2.p1.y, x2 = bb2.p2.x, y2 = bb2.p2.y;

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

/* NOTE not sure about this looks bad and is awkward due to alpha */
/* xiaolin wu line drawing algorithm anti-aliased lines */
static void wu_draw_line(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  Box_t bb2 = box_line(bbox); //
  int x1 = bb2.p1.x, y1 = bb2.p1.y, x2 = bb2.p2.x, y2 = bb2.p2.y;

  Pixel_t col_pix = *col;

  float dx = x2 - x1;
  float dy = y2 - y1;
  int steep = ABS(dy) > ABS(dx);

  float gradient;
  if(dx == 0.0f) gradient = 0.0f;
  else gradient = dy/dx;

  int grad_sign = (gradient < 0.0f) ? -1 : 1; //prevent going outside bbox

  /* float llsq = sqrt((dx*dx)+(dy*dy)); */
  /* printf("%i %i %i %i: %f %f %i %f %f\n",tx1,ty1,tx2,ty2,dx,dy,steep,gradient,llsq); */
  /* printf("%i %i %i %i\n",x1,y1,x2,y2); */

  uint8_t ca = 255;//col->alpha;
  if(steep){
    float interx = x1;
    for(int ty = y1; ty <= y2; ty++, interx += gradient){
      int x_inter = interx;

      if(x_inter >= bmp->width) break; //otherwise loops around image

      col_pix.alpha = ca * (1 - (interx - x_inter)); //255 * 0.5
      *(pixel_at(bmp, x_inter, ty)) = col_pix;

      if(grad_sign == -1 && x_inter <= x2) continue;
      if(grad_sign == 1 && x_inter >= x2) continue;

      col_pix.alpha = ca * (interx - x_inter); //255 * 0.5
      *(pixel_at(bmp, x_inter + (1*grad_sign), ty)) = col_pix;
    }
  }else{
    float intery = y1;
    for(int x = x1; x <= x2; x++, intery += gradient){
      int y_inter = intery;

      col_pix.alpha = ca * (1 - (intery - y_inter)); //255 * 0.5
      *(pixel_at(bmp, x, y_inter)) = col_pix;

      if(grad_sign == -1 && y_inter <= y2) continue;
      if(grad_sign == 1 && y_inter >= y2) continue;

      col_pix.alpha = ca * (intery - y_inter); //255 * 0.5
      *(pixel_at(bmp, x, y_inter + (1*grad_sign))) = col_pix;
    }
  }
}


/* colour random pixels in bounding box */
static void draw_scatter(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  unsigned int w = bbox.p2.x - bbox.p1.x, h = bbox.p2.y - bbox.p1.y;
  unsigned int iters = (w*h)*0.1; //10% of pixels approx
  for(unsigned int i=iters; i>0; i--){
    unsigned int x = bbox.p1.x + rand()%w;
    unsigned int y = bbox.p1.y + rand()%h;
    *(pixel_at(bmp, x, y)) = *col;
  }
}

/* POLY stuff */

static int __cmp(const void* a, const void* b)
{
  int* ai = (int*) a, *bi = (int*) b;
  if(*ai < *bi) return -1;
  return (*ai > *bi);
}

/* makes sorted list of x coord intersections, returns num of intersections */
static int poly_intersections(Point_t P, Point_t* V, int n, int* intersection_arr )
{
  int n_inters = 0;
  //for each edge get intersections
  for (int i=0; i<n-1; i++) {
    if (((V[i].y <= P.y) && (V[i+1].y > P.y))     // an upward crossing
        || ((V[i].y > P.y) && (V[i+1].y <=  P.y))) { // a downward crossing

      float vt = (float)(P.y  - V[i].y) / (V[i+1].y - V[i].y);
      int inter_x = V[i].x + vt * (V[i+1].x - V[i].x);
      intersection_arr[n_inters++] = inter_x;
    }
  }
  qsort(intersection_arr, n_inters, sizeof(int), __cmp);
  return n_inters;
}

static void __poly(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox, Point_t *points, int num_points)
{
  int intersection_arr[num_points];
  for (int y = bbox.p1.y; y < bbox.p2.y; y++) {
    Point_t xx = {bbox.p1.x, y};
    int n_inters = poly_intersections(xx, points, num_points, intersection_arr);

    //intersections are::: -> | (inside) -> | (outside) ::: etc.
    //so we draw between intersections then skip outside sections
    for(int i = 0; i<n_inters-1; i+=2) //2 because every even segment is outside polygon
      for(int j=intersection_arr[i]; j<intersection_arr[i+1]; j++)
        *(pixel_at(bmp, j, y)) = *col;
  }
}

static void draw_poly(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  int num_points = 6;
  Point_t points[6] = {bbox.p1,
                     {bbox.p1.x + ((bbox.p2.x - bbox.p1.x)/2), bbox.p1.y + ((bbox.p2.y - bbox.p1.y)/2)},
                     {bbox.p2.x, bbox.p1.y},
                     bbox.p2, {bbox.p1.x, bbox.p2.y}, bbox.p1};
  __poly(bmp, col, bbox, points, num_points);
}

static void draw_triangle(Bitmap_t *bmp, const Pixel_t *col, Box_t bbox)
{
  int num_points = 4;
  /* Point_t points[4] = {bbox.p1, {bbox.p2.x, bbox.p1.y}, bbox.p2, bbox.p1}; */
  Point_t points[4] = {bbox.p1, {bbox.p1.x + ((bbox.p2.x-bbox.p1.x)/2), bbox.p2.y}, {bbox.p2.x, bbox.p1.y}, bbox.p1};
  __poly(bmp, col, bbox, points, num_points);
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/* creates a random sized box for drawing shapes in
 * no point in making top left, bot right swapped boxes
 * since rect/ellipse doesn't care
 */
static Box_t make_box(unsigned int width, unsigned int height)
{
  unsigned int x1 = rand()%width, y1 = rand()%height;
  unsigned int w = 10 + (rand()%20), h = 10 + (rand()%20); //TODO make this more varied
  unsigned int x2 = ((x1+w) > width) ? width : (x1+w);
  unsigned int y2 = ((y1+h) > height) ? height : (y1+h);
  return (Box_t){{x1, y1},{x2, y2}};
}

/* loop creates blank images. Then draws on one of them.
 * Compares the images to original.
 * Uses the one closer to the original for the next loop.
 * As such incrementally creates an approximation of the original image
 */
static Bitmap_t * draw_loop(const Bitmap_t *orig, enum e_draw_funcs choice)
{
  //a on heap since we are going to return it
  Bitmap_t *a = malloc(sizeof(Bitmap_t)), b;
  if(a == NULL){ fprintf(stderr, "Failed alloc memory\n"); exit(EXIT_FAILURE); }
  blank_bmp_copy(a, orig->width, orig->height);
  blank_bmp_copy(&b, orig->width, orig->height);

  int iters = g_ITERS; //
  for(; iters--;){
    //bounding box in which changes will take place
    Box_t bbox = make_box(orig->width, orig->height);

    const Pixel_t *col = get_rand_col(orig); //can replace this with gradient

    /* if we chose rand we randomly choose shape each iter */
    if(choice == D_RAND){
      int choice_rnd = rand()%D_RAND; //random choice each iteration
      draw_funcs[choice_rnd](a, col, bbox); //random choice
    }else{
      draw_funcs[choice](a, col, bbox); //random choice
    }

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

void usage(char *exename)
{
  fprintf(stderr,"usage: %s [-n iterations] [-f draw_function] input_image output_image\n", exename);
  fprintf(stderr,"\t-f <line, box, ellipse, scatter, wu_line, tri, poly, rand>\n");
}

/* for imlib we load image then use read-only data for orig image
 * the actual draw loop works on pixels so we convert to them
 * then at end convert back to imlib image
 */
int main(int argc, char **argv)
{

  int opt;
  enum e_draw_funcs draw_choice = D_RAND;
  while((opt = getopt(argc, argv, "n:f:")) != -1){
    switch(opt){
    case 'n': g_ITERS = atoi(optarg); break;
    case 'f':
      draw_choice = get_draw_func_opt(optarg);
      break;
    default:
      goto usage_fail;
    }
  }
  if((argc - optind) != 2){
    goto usage_fail;
  }

  char *out_fmt = strrchr(argv[optind+1], '.');
  if(out_fmt == NULL){ fprintf(stderr, "Output file has no format %s.\n", argv[optind+1]); exit(EXIT_FAILURE); }

  srand(time(NULL));

  Imlib_Image input_image = imlib_load_image(argv[optind]);
  if(!input_image){ fprintf(stderr, "Failed to open input image %s.\n", argv[optind]); exit(EXIT_FAILURE); }
  imlib_context_set_image(input_image);

  unsigned int w = imlib_image_get_width();
  unsigned int h = imlib_image_get_height();

  Bitmap_t orig = {.width=w, .height=h};
  orig.pixels = (Pixel_t*) imlib_image_get_data_for_reading_only();

  //main loop
  Bitmap_t * a = draw_loop(&orig, draw_choice);

  imlib_free_image(); //free input_image

  //create new image, copy pixels and then save it
  Imlib_Image output_image = imlib_create_image(w, h);
  imlib_context_set_image(output_image);
  /* imlib_image_set_has_alpha(1); //need this for alpha output */
  Pixel_t *pixs = (Pixel_t *) imlib_image_get_data();
  memcpy(pixs, a->pixels, sizeof(Pixel_t) * w * h);
  imlib_image_put_back_data((unsigned int *) pixs);

  free(a->pixels);

  imlib_image_set_format(out_fmt + 1);
  imlib_save_image(argv[optind+1]);

  imlib_free_image(); //free output_image

  exit(EXIT_SUCCESS);

 usage_fail:
  usage(argv[0]);
  exit(EXIT_FAILURE);
}
