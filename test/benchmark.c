#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <simple-tiles/simple_tiles.h>
#include <simple-tiles/pool.h>
#include <simple-tiles/error.h>

#define ITEMS 100000


static void*
setup_map(){
  simplet_map_t *map;
  assert((map = simplet_map_new()));
  return map;
}

static void
teardown_map(void *ctx){
  simplet_map_free(ctx);
}

static void*
setup_list(){
  simplet_list_t *list;
  assert((list = simplet_list_new()));
  return list;
}

static void
teardown_list(void *ctx){
  ctx = NULL;
}

static void
worker(void *val) { (void) val; }

static void*
setup_pool(){
	simplet_pool_t *pool;
	assert((pool = simplet_pool_new()));
	simplet_list_t *list;
	assert((list = simplet_list_new()));
	int t = 1;
	for(int i = 0; i < 1000; i++)
    simplet_list_push(list, &t);
	
	simplet_pool_set_worker(pool, worker);
	simplet_pool_set_work(pool, list);

	return pool;
}


static void
teardown_pool(void *pool){
	simplet_pool_free(pool, worker);
}


static void
initialize_map(simplet_map_t *map){
  simplet_map_set_size(map, 256, 256);
  simplet_map_set_slippy(map, 0, 1, 2);
  simplet_map_add_layer(map, "../data/10m_admin_0_countries.shp");
  simplet_map_add_filter(map,  "SELECT * from '10m_admin_0_countries'");
  simplet_map_add_style(map, "weight", "0.1");
  simplet_map_add_style(map, "fill",   "#061F37ff");
}

static cairo_status_t
stream(void *closure, const unsigned char *data, unsigned int length){
  (void) closure, (void) data, (void) length;  /* suppress warnings */
  return CAIRO_STATUS_SUCCESS;
}

static void
bench_empty(void *ctx){
  simplet_map_t *map = ctx;
  simplet_map_set_size(map, 256, 256);
  simplet_map_set_slippy(map, 0, 1, 2);
  simplet_map_add_layer(map, "../data/noop");
  char *data = NULL;
  assert(simplet_map_render_to_stream(map, data, stream));
}

static void
bench_seamless(void *ctx){
  simplet_map_t *map = ctx;
  initialize_map(map);
  simplet_map_add_style(map, "seamless", "true");
  char *data = NULL;
  assert(simplet_map_render_to_stream(map, data, stream));
}

static void
bench_render(void *ctx){
  simplet_map_t *map = ctx;
  initialize_map(map);
  char *data = NULL;
  assert(simplet_map_render_to_stream(map, data, stream));
}

static void
bench_list(void *ctx){
  simplet_list_t *list = ctx;
  int t = 1;
  for(int i = 0; i < ITEMS; i++)
    simplet_list_push(list, &t);
  simplet_list_free(ctx);
}

static void
bench_pool(void *pool){
	simplet_pool_start(pool);
}


typedef struct {
  const char *name;
  void *(*setup)();
  void (*call)(void *ctx);
  void (*teardown)(void *ctx);
  const int times;
} bench_wrap_t;

#define BENCH(around, name) \
  { #name, &setup_##around, &bench_##name, &teardown_##around, 10 },

bench_wrap_t benchmarks[] = {
  BENCH(map, render)
  BENCH(map, seamless)
  BENCH(map, empty)
  BENCH(list, list)
  BENCH(pool, pool)	
  { NULL, NULL, NULL, NULL, 0}
};

static double
sum(double *arr, int count){
  double sum = 0;
  for(int i = 0; i < count; i++)
    sum += arr[i];
  return sum;
}

static double
mean(double *arr, int count){
  return sum(arr, count) / count;
}

static double
stdev(double *arr, int count){
  double avg = mean(arr, count);
  double var = 0;
  for(int i = 0; i < count; i++)
    var += pow(arr[i] - avg, 2);
  return sqrt(var / (count - 1));
}

static void
elision(simplet_status_t err, const char *mess){
  (void) err, (void) mess; /* suppress warnings */
  return;
}

int
main(){
  simplet_error_handler handle = elision;
  simplet_set_error_handle(handle);

  bench_wrap_t *bench = (bench_wrap_t*)&benchmarks;
  for(bench = (bench_wrap_t*)&benchmarks; bench->call; bench++){
    printf("\nbench %s:\n", bench->name);
    double runs[bench->times];
    for(int i = 0; i < bench->times; i++){
      void *data = bench->setup();
      clock_t start = clock();
      bench->call(data);
      runs[i] = (double) (clock() - start) / CLOCKS_PER_SEC;
      printf("\x1b[1;32m.\x1b[0m");
      fflush(stdout);
      bench->teardown(data);
    }
    printf("\nCompleted %i runs in %f seconds (%f r/s)\n",
                            bench->times, sum(runs, bench->times),
                            bench->times / sum(runs, bench->times));
    printf("\x1b[33mmean\x1b[0m: %f\n", mean(runs, bench->times));
    printf("\x1b[33mstd\x1b[0m:  %f\n",  stdev(runs, bench->times));
  }
}
