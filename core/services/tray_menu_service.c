#include "services/tray_menu_service.h"

#include <string.h>

#define BS_DBUSMENU_INTERFACE "com.canonical.dbusmenu"

typedef struct _BsTrayMenuRegistration {
  BsTrayMenuService *service;
  char *item_id;
  char *bus_name;
  char *menu_object_path;
  GDBusProxy *menu_proxy;
} BsTrayMenuRegistration;

struct _BsTrayMenuService {
  BsStateStore *store;
  GDBusConnection *session_bus;
  GHashTable *registrations;
  bool running;
};

static void bs_tray_menu_registration_free(gpointer data);
static BsTrayMenuRegistration *bs_tray_menu_service_lookup_registration(BsTrayMenuService *service,
                                                                        const char *item_id);
static gboolean bs_tray_menu_service_call_about_to_show(BsTrayMenuRegistration *registration,
                                                        gint32 menu_item_id);
static BsTrayMenuNode *bs_tray_menu_service_parse_layout_node(GVariant *value);
static bool bs_tray_menu_service_refresh_registration(BsTrayMenuRegistration *registration,
                                                      GError **error);
static void bs_tray_menu_service_on_proxy_signal(GDBusProxy *proxy,
                                                 const gchar *sender_name,
                                                 const gchar *signal_name,
                                                 GVariant *parameters,
                                                 gpointer user_data);

static void
bs_tray_menu_registration_free(gpointer data) {
  BsTrayMenuRegistration *registration = data;

  if (registration == NULL) {
    return;
  }

  if (registration->menu_proxy != NULL) {
    g_signal_handlers_disconnect_by_data(registration->menu_proxy, registration);
    g_object_unref(registration->menu_proxy);
  }
  g_free(registration->item_id);
  g_free(registration->bus_name);
  g_free(registration->menu_object_path);
  g_free(registration);
}

static BsTrayMenuRegistration *
bs_tray_menu_service_lookup_registration(BsTrayMenuService *service, const char *item_id) {
  g_return_val_if_fail(service != NULL, NULL);
  g_return_val_if_fail(item_id != NULL, NULL);

  return g_hash_table_lookup(service->registrations, item_id);
}

static gboolean
bs_tray_menu_service_call_about_to_show(BsTrayMenuRegistration *registration, gint32 menu_item_id) {
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;

  g_return_val_if_fail(registration != NULL, FALSE);
  g_return_val_if_fail(registration->menu_proxy != NULL, FALSE);

  reply = g_dbus_proxy_call_sync(registration->menu_proxy,
                                 "AboutToShow",
                                 g_variant_new("(i)", menu_item_id),
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 &error);
  if (reply == NULL) {
    return FALSE;
  }
  return TRUE;
}

static BsTrayMenuNode *
bs_tray_menu_service_parse_layout_node(GVariant *value) {
  g_autoptr(GVariant) id_value = NULL;
  g_autoptr(GVariant) properties = NULL;
  g_autoptr(GVariant) children = NULL;
  g_autofree char *item_type = NULL;
  g_autofree char *toggle_type = NULL;
  g_autofree char *children_display = NULL;
  GVariantIter properties_iter;
  GVariantIter children_iter;
  GVariant *child = NULL;
  BsTrayMenuNode *node = NULL;
  gint32 id = 0;

  g_return_val_if_fail(value != NULL, NULL);

  if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
    value = g_variant_get_variant(value);
  } else {
    g_variant_ref(value);
  }

  if (!g_variant_is_of_type(value, G_VARIANT_TYPE_TUPLE)) {
    g_variant_unref(value);
    return NULL;
  }

  node = g_new0(BsTrayMenuNode, 1);
  node->visible = true;
  node->enabled = true;
  node->children = g_ptr_array_new_with_free_func((GDestroyNotify) bs_tray_menu_node_free);

  id_value = g_variant_get_child_value(value, 0);
  properties = g_variant_get_child_value(value, 1);
  children = g_variant_get_child_value(value, 2);
  id = g_variant_get_int32(id_value);
  node->id = id;

  g_variant_iter_init(&properties_iter, properties);
  while ((child = g_variant_iter_next_value(&properties_iter)) != NULL) {
    const char *key = NULL;
    GVariant *child_value = NULL;

    g_variant_get(child, "{&sv}", &key, &child_value);
    if (g_strcmp0(key, "label") == 0) {
      g_free(node->label);
      node->label = g_variant_dup_string(child_value, NULL);
    } else if (g_strcmp0(key, "icon-name") == 0) {
      g_free(node->icon_name);
      node->icon_name = g_variant_dup_string(child_value, NULL);
    } else if (g_strcmp0(key, "visible") == 0) {
      node->visible = g_variant_get_boolean(child_value);
    } else if (g_strcmp0(key, "enabled") == 0) {
      node->enabled = g_variant_get_boolean(child_value);
    } else if (g_strcmp0(key, "type") == 0) {
      item_type = g_variant_dup_string(child_value, NULL);
    } else if (g_strcmp0(key, "children-display") == 0) {
      children_display = g_variant_dup_string(child_value, NULL);
    } else if (g_strcmp0(key, "toggle-type") == 0) {
      toggle_type = g_variant_dup_string(child_value, NULL);
    } else if (g_strcmp0(key, "toggle-state") == 0) {
      node->checked = g_variant_get_int32(child_value) != 0;
    }

    g_variant_unref(child_value);
    g_variant_unref(child);
  }

  g_variant_iter_init(&children_iter, children);
  while ((child = g_variant_iter_next_value(&children_iter)) != NULL) {
    BsTrayMenuNode *child_node = bs_tray_menu_service_parse_layout_node(child);

    g_variant_unref(child);
    if (child_node != NULL) {
      g_ptr_array_add(node->children, child_node);
    }
  }

  if (g_strcmp0(item_type, "separator") == 0) {
    node->kind = BS_TRAY_MENU_ITEM_SEPARATOR;
  } else if (g_strcmp0(toggle_type, "radio") == 0) {
    node->kind = BS_TRAY_MENU_ITEM_RADIO;
    node->is_radio = true;
  } else if (g_strcmp0(toggle_type, "checkmark") == 0 || g_strcmp0(toggle_type, "check") == 0) {
    node->kind = BS_TRAY_MENU_ITEM_CHECK;
  } else if ((node->children != NULL && node->children->len > 0)
             || g_strcmp0(children_display, "submenu") == 0) {
    node->kind = BS_TRAY_MENU_ITEM_SUBMENU;
  } else {
    node->kind = BS_TRAY_MENU_ITEM_NORMAL;
  }

  g_variant_unref(value);
  return node;
}

static bool
bs_tray_menu_service_refresh_registration(BsTrayMenuRegistration *registration, GError **error) {
  GVariantBuilder properties_builder;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) revision_value = NULL;
  g_autoptr(GVariant) layout = NULL;
  BsTrayMenuTree tree = {0};

  g_return_val_if_fail(registration != NULL, false);
  g_return_val_if_fail(registration->service != NULL, false);
  g_return_val_if_fail(registration->menu_proxy != NULL, false);

  if (g_dbus_proxy_get_name_owner(registration->menu_proxy) == NULL) {
    (void) bs_state_store_remove_tray_menu(registration->service->store, registration->item_id);
    return true;
  }

  (void) bs_tray_menu_service_call_about_to_show(registration, 0);

  g_variant_builder_init(&properties_builder, G_VARIANT_TYPE("as"));
  reply = g_dbus_proxy_call_sync(registration->menu_proxy,
                                 "GetLayout",
                                 g_variant_new("(ii@as)", 0, -1, g_variant_builder_end(&properties_builder)),
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 error);
  if (reply == NULL) {
    return false;
  }

  tree.item_id = registration->item_id;
  revision_value = g_variant_get_child_value(reply, 0);
  tree.revision = g_variant_get_uint32(revision_value);
  layout = g_variant_get_child_value(reply, 1);
  tree.root = bs_tray_menu_service_parse_layout_node(layout);
  if (tree.root == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to parse tray menu layout");
    return false;
  }

  g_debug("[bit_shelld] tray menu refresh item=%s path=%s revision=%u children=%u",
          registration->item_id,
          registration->menu_object_path != NULL ? registration->menu_object_path : "",
          tree.revision,
          tree.root != NULL && tree.root->children != NULL ? tree.root->children->len : 0);

  bs_state_store_begin_update(registration->service->store);
  (void) bs_state_store_replace_tray_menu(registration->service->store, &tree);
  bs_state_store_finish_update(registration->service->store);
  bs_tray_menu_node_free(tree.root);
  return true;
}

static void
bs_tray_menu_service_on_proxy_signal(GDBusProxy *proxy,
                                     const gchar *sender_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data) {
  BsTrayMenuRegistration *registration = user_data;

  (void) proxy;
  (void) sender_name;
  (void) parameters;

  if (registration == NULL || registration->service == NULL || !registration->service->running) {
    return;
  }

  if (g_strcmp0(signal_name, "LayoutUpdated") == 0
      || g_strcmp0(signal_name, "ItemsPropertiesUpdated") == 0
      || g_strcmp0(signal_name, "ItemActivationRequested") == 0) {
    g_autoptr(GError) error = NULL;

    if (!bs_tray_menu_service_refresh_registration(registration, &error)) {
      g_warning("[bit_shelld] failed to refresh tray menu for %s: %s",
                registration->item_id,
                error != NULL ? error->message : "unknown error");
    }
  }
}

BsTrayMenuService *
bs_tray_menu_service_new(BsStateStore *store) {
  BsTrayMenuService *service = g_new0(BsTrayMenuService, 1);

  service->store = store;
  service->registrations = g_hash_table_new_full(g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 bs_tray_menu_registration_free);
  return service;
}

void
bs_tray_menu_service_free(BsTrayMenuService *service) {
  if (service == NULL) {
    return;
  }

  bs_tray_menu_service_stop(service);
  g_clear_pointer(&service->registrations, g_hash_table_unref);
  g_free(service);
}

bool
bs_tray_menu_service_start(BsTrayMenuService *service, GError **error) {
  g_return_val_if_fail(service != NULL, false);

  if (service->running) {
    return true;
  }

  service->session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
  if (service->session_bus == NULL) {
    return false;
  }

  service->running = true;
  return true;
}

void
bs_tray_menu_service_stop(BsTrayMenuService *service) {
  if (service == NULL) {
    return;
  }

  if (service->registrations != NULL) {
    g_hash_table_remove_all(service->registrations);
  }
  if (service->running) {
    bs_state_store_clear_tray_menus(service->store);
  }
  g_clear_object(&service->session_bus);
  service->running = false;
}

bool
bs_tray_menu_service_sync_item(BsTrayMenuService *service,
                               const BsTrayItem *item,
                               GError **error) {
  BsTrayMenuRegistration *registration = NULL;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(item != NULL, false);
  g_return_val_if_fail(item->item_id != NULL, false);

  if (!service->running) {
    return true;
  }

  if (item->bus_name == NULL || *item->bus_name == '\0'
      || item->menu_object_path == NULL || *item->menu_object_path == '\0') {
    return bs_tray_menu_service_remove_item(service, item->item_id);
  }

  registration = bs_tray_menu_service_lookup_registration(service, item->item_id);
  if (registration != NULL
      && g_strcmp0(registration->bus_name, item->bus_name) == 0
      && g_strcmp0(registration->menu_object_path, item->menu_object_path) == 0) {
    return bs_tray_menu_service_refresh_registration(registration, error);
  }

  (void) bs_tray_menu_service_remove_item(service, item->item_id);
  registration = g_new0(BsTrayMenuRegistration, 1);
  registration->service = service;
  registration->item_id = g_strdup(item->item_id);
  registration->bus_name = g_strdup(item->bus_name);
  registration->menu_object_path = g_strdup(item->menu_object_path);
  registration->menu_proxy = g_dbus_proxy_new_sync(service->session_bus,
                                                   G_DBUS_PROXY_FLAGS_NONE,
                                                   NULL,
                                                   registration->bus_name,
                                                   registration->menu_object_path,
                                                   BS_DBUSMENU_INTERFACE,
                                                   NULL,
                                                   error);
  if (registration->menu_proxy == NULL) {
    bs_tray_menu_registration_free(registration);
    return false;
  }

  g_signal_connect(registration->menu_proxy,
                   "g-signal",
                   G_CALLBACK(bs_tray_menu_service_on_proxy_signal),
                   registration);
  g_hash_table_replace(service->registrations, g_strdup(registration->item_id), registration);
  return bs_tray_menu_service_refresh_registration(registration, error);
}

bool
bs_tray_menu_service_remove_item(BsTrayMenuService *service, const char *item_id) {
  bool removed = false;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);

  removed = g_hash_table_remove(service->registrations, item_id);
  removed = bs_state_store_remove_tray_menu(service->store, item_id) || removed;
  return removed;
}

void
bs_tray_menu_service_clear_items(BsTrayMenuService *service) {
  g_return_if_fail(service != NULL);

  if (service->registrations != NULL) {
    g_hash_table_remove_all(service->registrations);
  }
  bs_state_store_clear_tray_menus(service->store);
}

bool
bs_tray_menu_service_refresh_item(BsTrayMenuService *service,
                                  const char *item_id,
                                  GError **error) {
  BsTrayMenuRegistration *registration = NULL;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);

  registration = bs_tray_menu_service_lookup_registration(service, item_id);
  if (registration == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "tray menu registration not found for item_id: %s",
                item_id);
    return false;
  }

  return bs_tray_menu_service_refresh_registration(registration, error);
}

bool
bs_tray_menu_service_activate_menu_item(BsTrayMenuService *service,
                                        const char *item_id,
                                        gint32 menu_item_id,
                                        GError **error) {
  BsTrayMenuRegistration *registration = NULL;
  g_autoptr(GVariant) reply = NULL;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);

  registration = bs_tray_menu_service_lookup_registration(service, item_id);
  if (registration == NULL || registration->menu_proxy == NULL) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_NOT_FOUND,
                "tray menu registration not found for item_id: %s",
                item_id);
    return false;
  }

  reply = g_dbus_proxy_call_sync(registration->menu_proxy,
                                 "Event",
                                 g_variant_new("(isvu)",
                                               menu_item_id,
                                               "clicked",
                                               g_variant_new_int32(0),
                                               (guint32) 0),
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 error);
  if (reply == NULL) {
    return false;
  }

  return bs_tray_menu_service_refresh_registration(registration, NULL);
}
