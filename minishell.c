#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <nemotool.h>
#include <nemotimer.h>
#include <nemoshow.h>
#include <nemoplay.h>

#include "nemoutil.h"
#include "widgets.h"
#include "nemoui.h"
//#include "sound.h"

#define FPS_TIMEOUT 1000/60

typedef struct _ConfigApp ConfigApp;
struct _ConfigApp {
    Config *config;
    double sxy;
};

static ConfigApp *_config_load(const char *domain, const char *appname, const char *filename, int argc, char *argv[])
{
    ConfigApp *app = calloc(sizeof(ConfigApp), 1);
    app->config = config_load(domain, appname, filename, argc, argv);

    Xml *xml;
    if (app->config->path) {
        xml = xml_load_from_path(app->config->path);
        if (!xml) ERR("Load configuration failed: %s", app->config->path);
    } else {
        xml = xml_load_from_domain(domain, filename);
        if (!xml) ERR("Load configuration failed: %s:%s", domain, filename);
    }
    if (!xml) {
        config_unload(app->config);
        free(app);
        return NULL;
    }

    char buf[PATH_MAX];
    const char *temp;

    double sx = 1.0;
    double sy = 1.0;
    int width, height;
    snprintf(buf, PATH_MAX, "%s/size", appname);
    temp = xml_get_value(xml, buf, "width");
    if (!temp) {
        ERR("No size width in %s", appname);
    } else {
        width = atoi(temp);
    }
    temp = xml_get_value(xml, buf, "height");
    if (!temp) {
        ERR("No size height in %s", appname);
    } else {
        height = atoi(temp);
    }
    if (width > 0) sx = (double)app->config->width/width;
    if (height > 0) sy = (double)app->config->height/height;
    if (sx > sy) app->sxy = sy;
    else app->sxy = sx;

    xml_unload(xml);

    return app;
}

static void _config_unload(ConfigApp *app)
{
    config_unload(app->config);
    free(app);
}

typedef struct _AnimatorScene AnimatorScene;
struct _AnimatorScene {
    char *path;
    bool repeat;
    struct nemoplay *play;
    struct playbox *box;
};

void animator_scene_make_reverse(AnimatorScene *scene)
{
    RET_IF(!scene);
    struct playbox *box = scene->box;
    int i = 0;
    for (i = 0 ; i < box->nones ; i++) {
        if (i >= box->nones - i - 1) break;
        struct playone *temp;
        temp = box->ones[i];
        box->ones[i] = box->ones[box->nones - i - 1];
        box->ones[box->nones - i - 1] = temp;
    }
}

int animator_scene_get_frame_count(AnimatorScene *scene)
{
    RET_IF(!scene, -1);
    return  nemoplay_box_get_count(scene->box);
}

typedef struct _Animator Animator;
typedef void (*AnimatorDone)(Animator *anim, void *userdata);

struct _Animator {
    int width, height;
    struct nemoshow *show;
    struct nemotool *tool;

    struct playshader *shader;
    NemoWidget *widget;

    bool reverse;
    bool rewind;
    struct nemotimer *timer;
    int box_idx;
    List *scenes;
    AnimatorScene *scene;
    AnimatorScene *scene_next;
};

void animator_play(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 10);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    anim->reverse = false;
    anim->rewind = false;
    nemotimer_set_timeout(anim->timer, 10);
}

void animator_play_reverse(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 10);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    anim->reverse = true;
    anim->rewind = false;
    nemotimer_set_timeout(anim->timer, 10);
}

void animator_play_rewind(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 10);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    anim->reverse = false;
    anim->rewind = true;
    nemotimer_set_timeout(anim->timer, 10);
}

void animator_play_rewind_reverse(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 10);
    */
    anim->reverse = true;
    anim->rewind = true;

    nemotimer_set_timeout(anim->timer, 10);
}

void animator_stop(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    nemotimer_set_timeout(anim->timer, 0);
}

void animator_play_scene(Animator *anim, AnimatorScene *scene)
{
    RET_IF(!anim);
    RET_IF(!scene);

    if (!anim->scene) {
        anim->scene = scene;
        animator_play(anim);
    } else {
        if (anim->scene != scene) {
            anim->scene_next = scene;
            animator_play_rewind(anim);
        } else {
            animator_play(anim);
        }
    }
}

void animator_play_scene_reverse(Animator *anim, AnimatorScene *scene)
{
    RET_IF(!anim);
    RET_IF(!scene);

    if (!anim->scene) {
        anim->scene = scene;
        anim->box_idx = nemoplay_box_get_count(anim->scene->box) - 1;
        animator_play_reverse(anim);
    } else {
        if (anim->scene != scene) {
            anim->scene_next = scene;
            animator_play_rewind_reverse(anim);
        } else {
            animator_play_reverse(anim);
        }
    }
}

void animator_show(Animator *anim, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(anim->widget, easetype, duration, delay);
    nemowidget_set_alpha(anim->widget, easetype, duration, delay, 1.0);
    nemoshow_dispatch_frame(anim->show);
}

void animator_hide(Animator *anim, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(anim->widget, easetype, duration, delay);
    nemowidget_set_alpha(anim->widget, easetype, duration, delay, 0.0);
    nemoshow_dispatch_frame(anim->show);
}

void animator_translate(Animator *anim, uint32_t easetype, int duration, int delay, float tx, float ty)
{
    nemowidget_translate(anim->widget, easetype, duration, delay, tx, ty);
}

static void _animator_resize(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    NemoWidgetInfo_Resize *resize = info;
    Animator *anim = userdata;

    struct showone *canvas = nemowidget_get_canvas(widget);

	nemoplay_shader_set_viewport(anim->shader,
			nemoshow_canvas_get_texture(canvas),
            resize->width, resize->height);

    RET_IF(!anim->scene);

    struct playone *one;
    one = nemoplay_box_get_one(anim->scene->box, anim->box_idx);
    if (one) {
        nemoplay_shader_update(anim->shader, one);
        nemoplay_shader_dispatch(anim->shader);
    }
}

static void _animator_timeout(struct nemotimer *timer, void *userdata)
{
    Animator *anim = userdata;
    AnimatorScene *scene = anim->scene;
    RET_IF(!scene);

    int box_cnt = nemoplay_box_get_count(scene->box);

    struct playone *one;
	one = nemoplay_box_get_one(scene->box, anim->box_idx);
	if (one) {
		nemoplay_shader_update(anim->shader, one);
		nemoplay_shader_dispatch(anim->shader);
	}
    //ERR("[scene: %d][box: %d/%d][%p]", list_get_idx(anim->scenes, scene), anim->box_idx, box_cnt, one);

    nemowidget_dirty(anim->widget);
    nemoshow_dispatch_frame(anim->show);

    if (!anim->rewind) {
        if (!anim->reverse) {
            anim->box_idx++;
            if (anim->box_idx >= box_cnt) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = 0;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        anim->box_idx--;
                        nemotimer_set_timeout(timer, 0);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            anim->box_idx--;
            if (anim->box_idx < 0 ) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->reverse = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = box_cnt - 1;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        // XXX: repeat first scene
                        anim->box_idx = 0;
                        anim->scene = LIST_DATA(list_get_nth(anim->scenes, 0));
                        anim->reverse = false;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    } else {
        if (!anim->reverse) {
            int speed = 15;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            int speed = 5;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    }
}

void animator_append_callback(Animator *anim, const char *id, NemoWidget_Func func, void *userdata)
{
    nemowidget_append_callback(anim->widget, id, func, userdata);
}

#if 0
void _animator_frame(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    Animator *anim = userdata;
    AnimatorScene *scene = anim->scene;
    RET_IF(!scene);

    int box_cnt = nemoplay_box_get_count(scene->box);

    struct playone *one;
	one = nemoplay_box_get_one(scene->box, anim->box_idx);
	if (one) {
		nemoplay_shader_update(anim->shader, one);
		nemoplay_shader_dispatch(anim->shader);
	}
    ERR("[scene: %d][box: %d/%d][%p]", list_get_idx(anim->scenes, scene), anim->box_idx, box_cnt, one);

    nemowidget_dirty(anim->widget);
    nemoshow_dispatch_frame(anim->show);

    if (!anim->rewind) {
        if (!anim->reverse) {
            anim->box_idx++;
            if (anim->box_idx >= box_cnt) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = 0;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        anim->box_idx--;
                        nemotimer_set_timeout(timer, 0);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            anim->box_idx--;
            if (anim->box_idx < 0 ) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->reverse = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = box_cnt - 1;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        // XXX: repeat first scene
                        anim->box_idx = 0;
                        anim->scene = LIST_DATA(list_get_nth(anim->scenes, 0));
                        anim->reverse = false;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    } else {
        if (!anim->reverse) {
            int speed = 15;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            int speed = 5;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    }
}
#endif

Animator *animator_create(NemoWidget *parent, int width, int height)
{
    Animator *anim = calloc(sizeof(Animator), 1);
    anim->width = width;
    anim->height = height;
    anim->show = nemowidget_get_show(parent);
    anim->tool = nemowidget_get_tool(parent);
    anim->shader = nemoplay_shader_create();

    NemoWidget *widget;
    anim->widget = widget = nemowidget_create_opengl(parent, width, height);
    //nemowidget_append_callback(widget, "frame", _animator_frame, anim);
    nemowidget_append_callback(widget, "resize", _animator_resize, anim);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    anim->timer = TOOL_ADD_TIMER(anim->tool, 0, _animator_timeout, anim);

    return anim;
}

List *__animator_scene_caches;
AnimatorScene *animator_scene_cache_pop(const char *path, bool repeat)
{
    List *l;
    AnimatorScene *cache;
    LIST_FOR_EACH(__animator_scene_caches, l, cache) {
        if (!strcmp(path, cache->path)) {
            AnimatorScene *scene = calloc(sizeof(AnimatorScene), 1);
            scene->path = strdup(path);
            scene->repeat = repeat;
            scene->play = cache->play;
            scene->box = cache->box;
            return scene;
        }
    }

    ERR("path: %s", path);
    AnimatorScene *scene = calloc(sizeof(AnimatorScene), 1);
    scene->path = strdup(path);
    scene->repeat = repeat;

    struct nemoplay *play;
    scene->play = play = nemoplay_create();
    nemoplay_load_media(play, path);

    struct playbox *box;
    scene->box = box = nemoplay_box_create(nemoplay_get_video_framecount(play));
    nemoplay_extract_video(play, box, nemoplay_get_video_framecount(play));

    __animator_scene_caches = list_append(__animator_scene_caches, scene);
    return scene;
}

AnimatorScene *animator_append_scene(Animator *anim, const char *path, bool repeat)
{
    /*
    AnimatorScene *scene = calloc(sizeof(AnimatorScene), 1);
    scene->path = strdup(path);
    scene->repeat = repeat;

    struct nemoplay *play;
    scene->play = play = nemoplay_create();
    nemoplay_load_media(play, path);

    struct playbox *box;
    scene->box = box = nemoplay_box_create(nemoplay_get_video_framecount(play));
	nemoplay_extract_video(play, box);
    */

    AnimatorScene *scene = animator_scene_cache_pop(path, repeat);

    if (list_count(anim->scenes) == 0) {
        nemoplay_shader_set_format(anim->shader,
                nemoplay_get_pixel_format(scene->play));
        nemoplay_shader_resize(anim->shader,
                nemoplay_get_video_width(scene->play),
                nemoplay_get_video_height(scene->play));
    }

    anim->scenes = list_append(anim->scenes, scene);

    return scene;
}

typedef struct _MenuView MenuView;
struct _MenuView {
    int width, height;
    struct nemoshow *show;
    struct nemotool *tool;

    NemoWidget *widget;
    struct showone *group;
    struct showone *bg;

    int menu_w, menu_h;
    int menu_iw, menu_ih;
    List *menus;
};

typedef struct _MenuItem MenuItem;
typedef struct _Menu Menu;

struct _MenuItem {
    int width, height;
    Menu *menu;
    Animator *anim;
    AnimatorScene *scene_norm;
    AnimatorScene *scene_hide;
    AnimatorScene *scene_down;
};

struct _Menu {
    int x, y;
    int width, height;
    NemoWidget *parent;
    NemoWidget *widget;
    struct showone *group;

    Animator *bg;
    AnimatorScene *bg_scene;
    List *items;
};

static void _menu_item_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    MenuItem *it = userdata;

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        ERR("down");
        animator_play_scene(it->anim, it->scene_down);
    } else if (nemoshow_event_is_up(show, event)) {
        animator_play_scene_reverse(it->anim, it->scene_down);
            //;icon_set_state(it->icon, ICON_STATE_NORMAL);
        if (nemoshow_event_is_single_click(show, event)) {
            ERR("click");
            /*
            animator_set_box_idx(it->menu_item, 59);
            animator_set_scene(it->menu_item, 0);
            animator_play_reverse(it->menu_item);
            */
        } else {
            ERR("up");
            //animator_play_reverse(it->menu_item);
        }
    }
}

void menu_item_translate(MenuItem *it, uint32_t easetype, int duration, int delay, int tx, int ty)
{
    animator_translate(it->anim, easetype, duration, delay, tx , ty);
}

MenuItem *menu_append_item(Menu *menu, int w, int h)
{
    MenuItem *it = calloc(sizeof(MenuItem), 1);
    it->menu = menu;
    it->width = w;
    it->height = h;

    Animator *anim;
    it->anim = anim = animator_create(menu->parent, w, h);
    animator_append_callback(anim, "event", _menu_item_event, it);
    it->scene_norm = animator_append_scene(anim, MINISHELL_MOV_DIR"/menu_norm.mov", true);
    it->scene_hide = animator_append_scene(anim, MINISHELL_MOV_DIR"/menu_show.mov", false);
    // FIXME: revrese
    animator_scene_make_reverse(it->scene_hide);
    it->scene_down = animator_append_scene(anim, MINISHELL_MOV_DIR"/menu_down.mov", false);

    menu->items = list_append(menu->items, it);

    return it;
}

void menu_item_show(MenuItem *it, uint32_t easetype, int duration, int delay)
{
    animator_show(it->anim, easetype, duration, delay);
    animator_play_scene_reverse(it->anim, it->scene_hide);
}

Menu *menu_create(NemoWidget *parent, int w, int h)
{
    Menu *menu = calloc(sizeof(Menu), 1);
    menu->width = w;
    menu->height = h;
    menu->parent = parent;

    NemoWidget *widget;
    struct showone *group;
    struct showone *one;
    menu->widget = widget = nemowidget_create_vector(parent, w, h);
    menu->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    Animator *anim;
    menu->bg = anim = animator_create(parent, w, h);
    menu->bg_scene = animator_append_scene(anim, MINISHELL_MOV_DIR"/menu_back.mov", false);

    return menu;
}

void menu_translate(Menu *menu, uint32_t easetype, int duration, int delay, int tx, int ty)
{
    menu->x = tx;
    menu->y = ty;
    nemowidget_translate(menu->widget, easetype, duration, delay, tx, ty);
    animator_translate(menu->bg, easetype, duration, delay, tx, ty);
}

void menu_show(Menu *menu, uint32_t easetype, int duration, int delay)
{
    animator_show(menu->bg, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
    animator_play_scene(menu->bg, menu->bg_scene);


    List *l;
    MenuItem *it;

    it = LIST_DATA(LIST_FIRST(menu->items));
    RET_IF(!it);

    int ih = it->height;
    int gap = ih/4;
    int cnt = list_count(menu->items);

    int start = (menu->height - (cnt * ih + (cnt - 1) * gap))/2;
    LIST_FOR_EACH(menu->items, l, it) {
        menu_item_translate(it, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                menu->x, menu->y + start);
        menu_item_show(it, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
        start += ih + gap;
    }
}

#if 0
static void _menu_icon_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    MenuView *view = userdata;

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        animator_set_scene(view->menu_item0, 0);
        animator_set_reverse(view->menu_item0, false);
        animator_play(view->menu_item0);
        animator_show(view->menu_item0, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_hide(view->menu_item1, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_hide(view->menu_item, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);

    } else if (nemoshow_event_is_up(show, event)) {
        animator_set_scene(view->menu_item1, 0);
        animator_set_reverse(view->menu_item1, true);
        animator_play(view->menu_item1);
        animator_hide(view->menu_item0, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_show(view->menu_item1, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_hide(view->menu_item, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);

        if (nemoshow_event_is_single_click(show, event)) {
        }
    }
}
#endif

static void _menuview_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);
    MenuView *view = userdata;

    if (nemoshow_event_get_grab_time(event) + 1000 < nemoshow_event_get_time(event)) {
        if (!nemowidget_grab_get_data(grab, "longpress")) {
            nemowidget_grab_set_data(grab, "longpress", "1");
            ERR("LONG PRESS");

            Menu *menu;
            MenuItem *it;
            menu = menu_create(widget, view->menu_w, view->menu_h);
            view->menus = list_append(view->menus, menu);

            /*
            int i = 0;
            for (i = 0 ; i < 10 ; i++) {
                menu_append_item(menu, view->menu_iw, view->menu_ih);
            }
            menu_translate(menu, 0, 0, 0, ex - view->menu_w/2, 0);
            menu_show(menu, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
            */

        }
    }

    if (nemoshow_event_is_down(show, event)) {
        ERR("DOWN");
    } else if (nemoshow_event_is_up(show, event)) {
        ERR("UP");
    }
}

static void _menuview_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);
    MenuView *view = userdata;

    if (nemoshow_event_is_down(show, event)) {
        nemowidget_create_grab(widget, event, _menuview_grab_event, view);
    }
}

MenuView *menuview_create(NemoWidget *parent, int width, int height, ConfigApp *app)
{
    MenuView *view = calloc(sizeof(MenuView), 1);
    view->show = nemowidget_get_show(parent);
    view->tool = nemowidget_get_tool(parent);
    view->width = width;
    view->height = height;

    NemoWidget *widget;
    struct showone *one;
    struct showone *group;

    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _menuview_event, view);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));
    view->bg = one = RECT_CREATE(group, width, height);
    nemoshow_item_set_fill_color(one, RGBA(BLACK));

    int w, h, iw, ih;
    nemoplay_get_video_info(MINISHELL_MOV_DIR"/menu_back.mov", &w, &h);
    nemoplay_get_video_info(MINISHELL_MOV_DIR"/menu_norm.mov", &iw, &ih);
    view->menu_w = w * app->sxy;
    view->menu_h = h * app->sxy;
    view->menu_iw = iw * app->sxy;
    view->menu_ih = ih * app->sxy;

    return view;
}

void menuview_show(MenuView *view)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 1.0);
    nemoshow_dispatch_frame(view->show);
}

void menuview_hide(MenuView *view)
{
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 0.0);
    nemoshow_dispatch_frame(view->show);
}

int main(int argc, char *argv[])
{
    ConfigApp *app = _config_load(PROJECT_NAME, APPNAME, CONFXML, argc, argv);
    RET_IF(!app, -1);

    struct nemotool *tool = TOOL_CREATE();
    NemoWidget *win = nemowidget_create_win_base(tool, APPNAME, app->config);
    nemowidget_win_set_anchor(win, 0, 0);
    nemowidget_win_set_layer(win, "background");
    nemowidget_win_enable_move(win, 0);
    nemowidget_win_enable_rotate(win, 0);
    nemowidget_win_enable_scale(win, 0);

    MenuView *view = menuview_create(win, app->config->width, app->config->height, app);
    menuview_show(view);

    nemoshow_dispatch_frame(view->show);

    nemowidget_show(win, 0, 0, 0);
    nemotool_run(tool);

    nemowidget_destroy(win);
    TOOL_DESTROY(tool);
    _config_unload(app);

    return 0;
}
