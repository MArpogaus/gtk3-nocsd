/*
    gtk3-nocsd, a module used to disable GTK+3 client side decoration.

    Copyright (C) 2014  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    http://lxqt.org/

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>

typedef void (*gtk_window_set_titlebar_t) (GtkWindow *window, GtkWidget *titlebar);
typedef gboolean (*gdk_screen_is_composited_t) (GdkScreen *screen);
typedef void (*gdk_window_set_decorations_t) (GdkWindow *window, GdkWMDecoration decorations);
typedef GType (*g_type_register_static_t) (GType parent_type, const gchar *type_name, const GTypeInfo *info, GTypeFlags flags);
typedef GType (*g_type_register_static_simple_t) (GType parent_type, const gchar *type_name, guint class_size, GClassInitFunc class_init, guint instance_size, GInstanceInitFunc instance_init, GTypeFlags flags);
typedef void (*g_type_add_interface_static_t) (GType instance_type, GType interface_type, const GInterfaceInfo *info);
typedef void (*gtk_window_buildable_add_child_t) (GtkBuildable *buildable, GtkBuilder *builder, GObject *child, const gchar *type);

// When set to true, this override gdk_screen_is_composited() and let it
// return FALSE temporarily. Then, client-side decoration (CSD) cannot be initialized.
static gboolean disable_composite = FALSE;

static void set_has_custom_title(GtkWindow* window, gboolean set) {
    g_object_set_data(G_OBJECT(window), "custom_title", set ? GINT_TO_POINTER(1) : NULL);
}

static gboolean has_custom_title(GtkWindow* window) {
    return (gboolean)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "custom_title"));
}

// This API exists since gtk+ 3.10
extern void gtk_window_set_titlebar (GtkWindow *window, GtkWidget *titlebar) {
    static gtk_window_set_titlebar_t orig_func = NULL;
    if(!orig_func)
        orig_func = (gtk_window_set_titlebar_t)dlsym(RTLD_NEXT, "gtk_window_set_titlebar");
    // printf("gtk_window_set_titlebar\n");
    disable_composite = TRUE;
    orig_func(window, titlebar);
    if(window && titlebar)
        set_has_custom_title(window, TRUE);
    disable_composite = FALSE;
}

extern gboolean gdk_screen_is_composited (GdkScreen *screen) {
    static gdk_screen_is_composited_t orig_func = NULL;
    if(!orig_func)
        orig_func = (gdk_screen_is_composited_t)dlsym(RTLD_NEXT, "gdk_screen_is_composited");
    // printf("gdk_screen_is_composited\n");
    if(disable_composite)
        return FALSE;
    return orig_func(screen);
}

extern void gdk_window_set_decorations (GdkWindow *window, GdkWMDecoration decorations) {
    static gdk_window_set_decorations_t orig_func = NULL;
    if(!orig_func)
        orig_func = (gdk_window_set_decorations_t)dlsym(RTLD_NEXT, "gdk_window_set_decorations");
    if(decorations == GDK_DECOR_BORDER) {
        GtkWidget* widget = NULL;
        gdk_window_get_user_data(window, (void**)&widget);
        if(widget && GTK_IS_WINDOW(widget)) { // if this GdkWindow is associated with a GtkWindow
            // if this window has custom title (not using CSD), turn on all decorations
            if(has_custom_title(GTK_WINDOW(widget)))
                decorations = GDK_DECOR_ALL;
        }
    }
    orig_func(window, decorations);
}

typedef void (*gtk_window_realize_t)(GtkWidget* widget);
static gtk_window_realize_t orig_gtk_window_realize = NULL;

static void fake_gtk_window_realize(GtkWidget* widget) {
    // printf("intercept gtk_window_realize()!!! %p, %s\n", widget, G_OBJECT_TYPE_NAME(widget));
    disable_composite = TRUE;
    orig_gtk_window_realize(widget);
    disable_composite = FALSE;
}

static GClassInitFunc orig_gtk_window_class_init = NULL;
static GType gtk_window_type = 0;

static void fake_gtk_window_class_init (GtkWindowClass *klass, gpointer data) {
    orig_gtk_window_class_init(klass, data);
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
    if(widget_class) {
        orig_gtk_window_realize = widget_class->realize;
        widget_class->realize = fake_gtk_window_realize;
    }
}

extern GType g_type_register_static (GType parent_type, const gchar *type_name, const GTypeInfo *info, GTypeFlags flags) {
    static g_type_register_static_t orig_func = NULL;
    if(!orig_func)
        orig_func = (g_type_register_static_t)dlsym(RTLD_NEXT, "g_type_register_static");

    // printf("register %s\n", type_name);
    if(!orig_gtk_window_class_init) { // GtkWindow is not overriden
        if(type_name && G_UNLIKELY(strcmp(type_name, "GtkWindow") == 0)) {
            // override GtkWindowClass
            GTypeInfo fake_info = *info;
            orig_gtk_window_class_init = info->class_init;
            fake_info.class_init = (GClassInitFunc)fake_gtk_window_class_init;
            gtk_window_type = orig_func(parent_type, type_name, &fake_info, flags);
            return gtk_window_type;
        }
    }
    return orig_func(parent_type, type_name, info, flags);
}

GType g_type_register_static_simple (GType parent_type, const gchar *type_name, guint class_size, GClassInitFunc class_init, guint instance_size, GInstanceInitFunc instance_init, GTypeFlags flags) {
    GType type;
    static g_type_register_static_simple_t orig_func = NULL;
    if(!orig_func)
        orig_func = (g_type_register_static_simple_t)dlsym(RTLD_NEXT, "g_type_register_static_simple");

    // printf("register simple %s\n", type_name);
    if(!orig_gtk_window_class_init) { // GtkWindow is not overriden
        if(type_name && G_UNLIKELY(strcmp(type_name, "GtkWindow") == 0)) {
            // override GtkWindowClass
            orig_gtk_window_class_init = class_init;
            class_init = (GClassInitFunc)fake_gtk_window_class_init;
            gtk_window_type = orig_func(parent_type, type_name, class_size, class_init, instance_size, instance_init, flags);;
            return gtk_window_type;
        }
    }
    type = orig_func(parent_type, type_name, class_size, class_init, instance_size, instance_init, flags);
    return type;
}

static gtk_window_buildable_add_child_t orig_gtk_window_buildable_add_child = NULL;

static void fake_gtk_window_buildable_add_child (GtkBuildable *buildable, GtkBuilder *builder, GObject *child, const gchar *type) {
    // setting a titelbar via GtkBuilder => disable compositing temporarily
    if(type && strcmp(type, "titlebar") == 0) {
        disable_composite = TRUE;
        if(child)
            set_has_custom_title(GTK_WINDOW(buildable), TRUE);
    }
    orig_gtk_window_buildable_add_child(buildable, builder, child, type);
    disable_composite = FALSE;
}

static GInterfaceInitFunc orig_gtk_window_buildable_interface_init = NULL;

static void fake_gtk_window_buildable_interface_init (GtkBuildableIface *iface, gpointer data)
{
    orig_gtk_window_buildable_interface_init(iface, data);
    orig_gtk_window_buildable_add_child = iface->add_child;
    iface->add_child = fake_gtk_window_buildable_add_child;
    // printf("intercept gtk_window_buildable_interface_init!!\n");
    // iface->set_buildable_property = gtk_window_buildable_set_buildable_property;
}

void g_type_add_interface_static (GType instance_type, GType interface_type, const GInterfaceInfo *info) {
    static g_type_add_interface_static_t orig_func = NULL;
    if(!orig_func)
        orig_func = (g_type_add_interface_static_t)dlsym(RTLD_NEXT, "g_type_add_interface_static");
    if(instance_type == gtk_window_type) {
        if(interface_type == GTK_TYPE_BUILDABLE) {
            // register GtkBuildable interface for GtkWindow class
            GInterfaceInfo fake_info = *info;
            orig_gtk_window_buildable_interface_init = info->interface_init;
            fake_info.interface_init = (GInterfaceInitFunc)fake_gtk_window_buildable_interface_init;
            orig_func(instance_type, interface_type, &fake_info);
            return;
        }
    }
    orig_func(instance_type, interface_type, info);
}
