#include <stdlib.h>
#include <cpl_error.h>

#include "style.h"
#include "filter.h"
#include "util.h"

simplet_filter_t *
simplet_filter_new(const char *sqlquery){
  simplet_filter_t *filter;
  if(!(filter = malloc(sizeof(*filter))))
    return NULL;

  if(!(filter->styles = simplet_list_new())){
    free(filter);
    return NULL;
  }

  filter->_bounds  = NULL;
  filter->_ctx     = NULL;
  filter->_surface = NULL;
  filter->ogrsql  = simplet_copy_string(sqlquery);
  return filter;
}

void
simplet_filter_free(simplet_filter_t *filter){
  simplet_list_t* styles = filter->styles;
  simplet_list_set_item_free(styles, simplet_style_vfree);
  simplet_list_free(styles);

  if(filter->_bounds)
    simplet_bounds_free(filter->_bounds);

  if(filter->_ctx)
    cairo_destroy(filter->_ctx);

  if(filter->_surface)
    cairo_surface_destroy(filter->_surface);

  free(filter->ogrsql);
  free(filter);
}

void
simplet_filter_vfree(void *filter){
  simplet_filter_free(filter);
}

static void
plot_part(OGRGeometryH geom, simplet_filter_t *filter){
  double x, y, last_x, last_y;
  OGR_G_GetPoint(geom, 0, &x, &y, NULL);
  last_x = x;
  last_y = y;
  cairo_move_to(filter->_ctx, x - filter->_bounds->nw.x,
                filter->_bounds->nw.y - y);
  for(int j = 0; j < OGR_G_GetPointCount(geom); j++){
    OGR_G_GetPoint(geom, j, &x, &y, NULL);
    double dx = fabs(last_x - x);
    double dy = fabs(last_y - y);
    cairo_user_to_device_distance(filter->_ctx, &dx, &dy);

    if(dx >= 0.25 || dy >= 0.25){
      cairo_line_to(filter->_ctx, x - filter->_bounds->nw.x,
                    filter->_bounds->nw.y - y);
      last_x = x;
      last_y = y;

    }
  }
  // ensure something is always drawn
  OGR_G_GetPoint(geom, OGR_G_GetPointCount(geom) - 1, &x, &y, NULL);
  cairo_line_to(filter->_ctx, x - filter->_bounds->nw.x,
                              filter->_bounds->nw.y - y);
}

static void
plot_polygon(OGRGeometryH geom, simplet_filter_t *filter){
  cairo_save(filter->_ctx);
  cairo_new_path(filter->_ctx);
  for(int i = 0; i < OGR_G_GetGeometryCount(geom); i++){
    OGRGeometryH subgeom = OGR_G_GetGeometryRef(geom, i);
    if(subgeom == NULL)
      continue;

    if(OGR_G_GetGeometryCount(subgeom) > 0) {
      plot_polygon(subgeom, filter);
      continue;
    }

    plot_part(subgeom, filter);
    cairo_close_path(filter->_ctx);
  }
  cairo_close_path(filter->_ctx);
  simplet_apply_styles(filter->_ctx, filter->styles,
                       "line-join", "line-cap", "weight", "fill", "stroke", NULL);
  cairo_clip(filter->_ctx);
  cairo_restore(filter->_ctx);
}

static void
plot_point(OGRGeometryH geom, simplet_filter_t *filter){
  double x, y;

  simplet_style_t *style = simplet_lookup_style(filter->styles, "radius");
  if(style == NULL)
    return;

  double r = strtod(style->arg, NULL), dy = 0;

  cairo_device_to_user_distance(filter->_ctx, &r, &dy);
  cairo_save(filter->_ctx);
  for(int i = 0; i < OGR_G_GetPointCount(geom); i++){
    OGR_G_GetPoint(geom, i, &x, &y, NULL);
    cairo_arc(filter->_ctx, x - filter->_bounds->nw.x,
              filter->_bounds->nw.y - y, r, 0., 2 * M_PI);
  }
  cairo_close_path(filter->_ctx);
  simplet_apply_styles(filter->_ctx, filter->styles,
                       "line-join", "line-cap", "weight", "fill", "stroke", NULL);
  cairo_restore(filter->_ctx);
}

static void
plot_line(OGRGeometryH geom, simplet_filter_t *filter){
  cairo_save(filter->_ctx);
  cairo_new_path(filter->_ctx);
  plot_part(geom, filter);
  simplet_apply_styles(filter->_ctx, filter->styles,
                        "line-join", "line-cap", "weight", "stroke", NULL);
  cairo_restore(filter->_ctx);
}

static void
dispatch(OGRGeometryH geom, simplet_filter_t *filter){
  switch(wkbFlatten(OGR_G_GetGeometryType(geom))){
    case wkbPolygon:
      plot_polygon(geom, filter);
      break;
    case wkbLinearRing:
    case wkbLineString:
      plot_line(geom, filter);
      break;
    case wkbMultiPoint:
    case wkbPoint:
      plot_point(geom, filter);
      break;
    case wkbMultiPolygon:
    case wkbMultiLineString:
    case wkbGeometryCollection:
      for(int i = 0; i < OGR_G_GetGeometryCount(geom); i++){
        OGRGeometryH subgeom = OGR_G_GetGeometryRef(geom, i);
        if(subgeom == NULL)
          continue;
        dispatch(subgeom, filter);
      }
      break;
    default:
      ;
  }
}


/* FIXME: this function is way too hairy and needs error handling */
simplet_status_t
simplet_filter_process(simplet_filter_t *filter, simplet_layer_t *layer, simplet_map_t *map){

  pthread_mutex_lock(&map->lock);
  OGRDataSourceH source;
  OGRSFDriverH   driver = NULL;
  if(!(source = OGROpen(layer->source, 0, &driver))){
    pthread_mutex_unlock(&map->lock);
    return SIMPLET_OGR_ERR;
  }

  OGRLayerH olayer;
  if(!(olayer = OGR_DS_ExecuteSQL(source, filter->ogrsql, NULL, NULL))){
    int err = CPLGetLastErrorNo();
    pthread_mutex_unlock(&map->lock);
    OGR_DS_Destroy(source);
    if(!err)
      return SIMPLET_OK;
    else
      return SIMPLET_OGR_ERR;
  }
  pthread_mutex_unlock(&map->lock);

  OGRSpatialReferenceH srs;
  if(!(srs = OGR_L_GetSpatialRef(olayer))){
    OGR_DS_Destroy(source);
    return SIMPLET_OGR_ERR;
  }

  OGRGeometryH bounds = simplet_bounds_to_ogr(map->bounds, map->proj);
  OGR_G_TransformTo(bounds, srs);
  OGR_DS_ReleaseResultSet(source, olayer);

  olayer = OGR_DS_ExecuteSQL(source, filter->ogrsql, bounds, "");
  if(!olayer) {
    OGR_G_DestroyGeometry(bounds);
    OGR_DS_Destroy(source);
    return SIMPLET_OGR_ERR;
  }


  OGRCoordinateTransformationH transform;
  if(!(transform = OCTNewCoordinateTransformation(srs, map->proj))){
    OGR_G_DestroyGeometry(bounds);
    OGR_DS_Destroy(source);
    OGR_DS_ReleaseResultSet(source, olayer);
    return SIMPLET_OGR_ERR;
  }

  pthread_mutex_lock(&map->lock);
  filter->_surface = cairo_surface_create_similar(cairo_get_target(map->_ctx),
                                  CAIRO_CONTENT_COLOR_ALPHA, map->width, map->height);
  pthread_mutex_unlock(&map->lock);

  if(cairo_surface_status(filter->_surface) != CAIRO_STATUS_SUCCESS){
    OGR_G_DestroyGeometry(bounds);
    OGR_DS_Destroy(source);
    OGR_DS_ReleaseResultSet(source, olayer);
    return SIMPLET_CAIRO_ERR;
  }

  filter->_ctx = cairo_create(filter->_surface);

  simplet_style_t *seamless = simplet_lookup_style(filter->styles, "seamless");
  if(seamless)
    cairo_set_operator(filter->_ctx, CAIRO_OPERATOR_SATURATE);

  filter->_bounds = map->bounds;
  cairo_scale(filter->_ctx, map->width / filter->_bounds->width,
                            map->width / filter->_bounds->width);

  OGRFeatureH feature;
  while((feature = OGR_L_GetNextFeature(olayer))){
    OGRGeometryH geom = OGR_F_GetGeometryRef(feature);
    if(geom == NULL)
      continue;

    pthread_mutex_lock(&map->lock);
    OGR_G_Transform(geom, transform);
    pthread_mutex_unlock(&map->lock);

    dispatch(geom, filter);
    OGR_F_Destroy(feature);
  }
  cairo_scale(filter->_ctx, filter->_bounds->width / map->width,
                            filter->_bounds->width / map->width);

  OGR_G_DestroyGeometry(bounds);
  filter->_bounds = NULL;

  OGR_DS_ReleaseResultSet(source, olayer);
  OCTDestroyCoordinateTransformation(transform);
  OGR_DS_Destroy(source);
  return SIMPLET_OK;
}

simplet_style_t*
simplet_filter_add_style(simplet_filter_t *filter, const char *key, const char *arg){
  simplet_style_t *style;
  if(!(style = simplet_style_new(key, arg)))
    return NULL;

  if(!simplet_list_push(filter->styles, style)){
    simplet_style_free(style);
    return NULL;
  }

  return style;
}

