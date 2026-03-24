#include "services/tray_service.h"

#include <glib.h>
#include <string.h>

#define BS_TRAY_WATCHER_OBJECT_PATH "/StatusNotifierWatcher"
#define BS_TRAY_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"
#define BS_TRAY_ITEM_INTERFACE "org.kde.StatusNotifierItem"

typedef struct _BsTrayRegistration BsTrayRegistration;
typedef struct _BsTrayPendingRegistration BsTrayPendingRegistration;

struct _BsTrayService {
  BsStateStore *store;
  char *watcher_name;
  bool running;
  GDBusConnection *session_bus;
  guint watcher_owner_id;
  guint watcher_object_id;
  GDBusNodeInfo *watcher_node_info;
  GHashTable *registrations;
};

struct _BsTrayRegistration {
  BsTrayService *service;
  char *item_id;
  char *bus_name;
  char *object_path;
  guint name_watch_id;
  GDBusProxy *item_proxy;
};

struct _BsTrayPendingRegistration {
  BsTrayService *service;
  char *sender;
  char *service_or_path;
};

static const char *bs_tray_watcher_introspection_xml =
  "<node>"
  "  <interface name='org.kde.StatusNotifierWatcher'>"
  "    <method name='RegisterStatusNotifierItem'>"
  "      <arg type='s' name='service' direction='in'/>"
  "    </method>"
  "    <method name='RegisterStatusNotifierHost'>"
  "      <arg type='s' name='service' direction='in'/>"
  "    </method>"
  "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
  "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
  "    <property name='ProtocolVersion' type='i' access='read'/>"
  "    <signal name='StatusNotifierItemRegistered'>"
  "      <arg type='s' name='service'/>"
  "    </signal>"
  "    <signal name='StatusNotifierItemUnregistered'>"
  "      <arg type='s' name='service'/>"
  "    </signal>"
  "    <signal name='StatusNotifierHostRegistered'/>"
  "  </interface>"
  "</node>";

static void bs_tray_registration_free(gpointer data);
static BsTrayRegistration *bs_tray_service_lookup_registration(BsTrayService *service,
                                                               const char *item_id);
static bool bs_tray_service_register_item(BsTrayService *service,
                                          const char *sender,
                                          const char *service_or_path,
                                          GError **error);
static void bs_tray_service_pending_registration_free(gpointer data);
static gboolean bs_tray_service_register_item_idle(gpointer user_data);
static void bs_tray_service_unregister_item(BsTrayService *service,
                                            const char *item_id);
static void bs_tray_service_unregister_all_items(BsTrayService *service);
static bool bs_tray_service_refresh_registration(BsTrayRegistration *registration, GError **error);
static char *bs_tray_service_build_item_id(const char *bus_name, const char *object_path);
static char *bs_tray_service_dup_proxy_string(GDBusProxy *proxy, const char *property_name);
static bool bs_tray_service_get_proxy_bool(GDBusProxy *proxy,
                                           const char *property_name,
                                           bool fallback_value);
static BsTrayItemStatus bs_tray_service_get_proxy_status(GDBusProxy *proxy);
static void bs_tray_service_on_name_vanished(GDBusConnection *connection,
                                             const char *name,
                                             gpointer user_data);
static void bs_tray_service_on_proxy_properties_changed(GDBusProxy *proxy,
                                                        GVariant *changed_properties,
                                                        const gchar * const *invalidated_properties,
                                                        gpointer user_data);
static void bs_tray_service_on_proxy_signal(GDBusProxy *proxy,
                                            const gchar *sender_name,
                                            const gchar *signal_name,
                                            GVariant *parameters,
                                            gpointer user_data);
static void bs_tray_service_handle_method_call(GDBusConnection *connection,
                                               const char *sender,
                                               const char *object_path,
                                               const char *interface_name,
                                               const char *method_name,
                                               GVariant *parameters,
                                               GDBusMethodInvocation *invocation,
                                               gpointer user_data);
static GVariant *bs_tray_service_handle_get_property(GDBusConnection *connection,
                                                     const char *sender,
                                                     const char *object_path,
                                                     const char *interface_name,
                                                     const char *property_name,
                                                     GError **error,
                                                     gpointer user_data);
static bool bs_tray_service_call_item_method(BsTrayService *service,
                                             const char *item_id,
                                             const char *method_name,
                                             int32_t x,
                                             int32_t y,
                                             GError **error);

static const GDBusInterfaceVTable bs_tray_service_watcher_vtable = {
  bs_tray_service_handle_method_call,
  bs_tray_service_handle_get_property,
  NULL,
  {NULL},
};

static void
bs_tray_registration_free(gpointer data) {
  BsTrayRegistration *registration = data;

  if (registration == NULL) {
    return;
  }

  if (registration->name_watch_id != 0 && registration->service != NULL && registration->service->session_bus != NULL) {
    g_bus_unwatch_name(registration->name_watch_id);
  }
  if (registration->item_proxy != NULL) {
    g_signal_handlers_disconnect_by_data(registration->item_proxy, registration);
    g_object_unref(registration->item_proxy);
  }
  g_free(registration->item_id);
  g_free(registration->bus_name);
  g_free(registration->object_path);
  g_free(registration);
}

static void
bs_tray_service_pending_registration_free(gpointer data) {
  BsTrayPendingRegistration *pending = data;

  if (pending == NULL) {
    return;
  }

  g_free(pending->sender);
  g_free(pending->service_or_path);
  g_free(pending);
}

static gboolean
bs_tray_service_register_item_idle(gpointer user_data) {
  BsTrayPendingRegistration *pending = user_data;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(pending != NULL, G_SOURCE_REMOVE);

  if (pending->service != NULL && pending->service->running) {
    if (!bs_tray_service_register_item(pending->service,
                                       pending->sender,
                                       pending->service_or_path,
                                       &error)) {
      g_warning("[bit_shelld] failed to register tray item: %s",
                error != NULL ? error->message : "unknown error");
    }
  }

  return G_SOURCE_REMOVE;
}

static BsTrayRegistration *
bs_tray_service_lookup_registration(BsTrayService *service, const char *item_id) {
  g_return_val_if_fail(service != NULL, NULL);
  g_return_val_if_fail(item_id != NULL, NULL);

  return g_hash_table_lookup(service->registrations, item_id);
}

static char *
bs_tray_service_build_item_id(const char *bus_name, const char *object_path) {
  g_return_val_if_fail(bus_name != NULL, NULL);
  g_return_val_if_fail(object_path != NULL, NULL);

  return g_strdup_printf("%s%s", bus_name, object_path);
}

static char *
bs_tray_service_dup_proxy_string(GDBusProxy *proxy, const char *property_name) {
  g_autoptr(GVariant) value = NULL;

  g_return_val_if_fail(proxy != NULL, NULL);
  g_return_val_if_fail(property_name != NULL, NULL);

  value = g_dbus_proxy_get_cached_property(proxy, property_name);
  if (value == NULL) {
    return NULL;
  }
  if (!g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)
      && !g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)) {
    return NULL;
  }

  return g_variant_dup_string(value, NULL);
}

static bool
bs_tray_service_get_proxy_bool(GDBusProxy *proxy,
                               const char *property_name,
                               bool fallback_value) {
  g_autoptr(GVariant) value = NULL;

  g_return_val_if_fail(proxy != NULL, fallback_value);
  g_return_val_if_fail(property_name != NULL, fallback_value);

  value = g_dbus_proxy_get_cached_property(proxy, property_name);
  if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
    return fallback_value;
  }

  return g_variant_get_boolean(value);
}

static BsTrayItemStatus
bs_tray_service_get_proxy_status(GDBusProxy *proxy) {
  g_autofree char *status = NULL;

  g_return_val_if_fail(proxy != NULL, BS_TRAY_ITEM_STATUS_PASSIVE);

  status = bs_tray_service_dup_proxy_string(proxy, "Status");
  if (g_strcmp0(status, "Active") == 0) {
    return BS_TRAY_ITEM_STATUS_ACTIVE;
  }
  if (g_strcmp0(status, "Attention") == 0) {
    return BS_TRAY_ITEM_STATUS_ATTENTION;
  }
  return BS_TRAY_ITEM_STATUS_PASSIVE;
}

BsTrayService *
bs_tray_service_new(BsStateStore *store, const BsTrayServiceConfig *config) {
  BsTrayService *service = g_new0(BsTrayService, 1);
  service->store = store;
  if (config != NULL && config->watcher_name != NULL) {
    service->watcher_name = g_strdup(config->watcher_name);
  }
  service->registrations = g_hash_table_new_full(g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 bs_tray_registration_free);
  return service;
}

void
bs_tray_service_free(BsTrayService *service) {
  if (service == NULL) {
    return;
  }

  bs_tray_service_stop(service);
  g_clear_pointer(&service->watcher_node_info, g_dbus_node_info_unref);
  g_clear_pointer(&service->registrations, g_hash_table_unref);
  g_free(service->watcher_name);
  g_free(service);
}

bool
bs_tray_service_start(BsTrayService *service, GError **error) {
  GDBusInterfaceInfo *watcher_interface = NULL;

  g_return_val_if_fail(service != NULL, false);
  if (service->running) {
    return true;
  }

  if (service->watcher_name == NULL || *service->watcher_name == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "missing tray watcher_name");
    return false;
  }

  service->session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
  if (service->session_bus == NULL) {
    return false;
  }

  service->watcher_node_info = g_dbus_node_info_new_for_xml(bs_tray_watcher_introspection_xml, error);
  if (service->watcher_node_info == NULL) {
    g_clear_object(&service->session_bus);
    return false;
  }

  watcher_interface = g_dbus_node_info_lookup_interface(service->watcher_node_info,
                                                        BS_TRAY_WATCHER_INTERFACE);
  if (watcher_interface == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "missing tray watcher interface info");
    g_clear_pointer(&service->watcher_node_info, g_dbus_node_info_unref);
    g_clear_object(&service->session_bus);
    return false;
  }

  service->watcher_owner_id = g_bus_own_name_on_connection(service->session_bus,
                                                           service->watcher_name,
                                                           G_BUS_NAME_OWNER_FLAGS_NONE,
                                                           NULL,
                                                           NULL,
                                                           NULL,
                                                           NULL);
  service->watcher_object_id = g_dbus_connection_register_object(service->session_bus,
                                                                 BS_TRAY_WATCHER_OBJECT_PATH,
                                                                 watcher_interface,
                                                                 &bs_tray_service_watcher_vtable,
                                                                 service,
                                                                 NULL,
                                                                 error);
  if (service->watcher_object_id == 0) {
    g_bus_unown_name(service->watcher_owner_id);
    service->watcher_owner_id = 0;
    g_clear_pointer(&service->watcher_node_info, g_dbus_node_info_unref);
    g_clear_object(&service->session_bus);
    return false;
  }

  service->running = true;
  bs_state_store_clear_tray_items(service->store);
  bs_state_store_mark_topic_changed(service->store, BS_TOPIC_TRAY);
  return true;
}

void
bs_tray_service_stop(BsTrayService *service) {
  if (service == NULL) {
    return;
  }

  bs_tray_service_unregister_all_items(service);
  if (service->watcher_object_id != 0 && service->session_bus != NULL) {
    g_dbus_connection_unregister_object(service->session_bus, service->watcher_object_id);
    service->watcher_object_id = 0;
  }
  if (service->watcher_owner_id != 0) {
    g_bus_unown_name(service->watcher_owner_id);
    service->watcher_owner_id = 0;
  }
  g_clear_pointer(&service->watcher_node_info, g_dbus_node_info_unref);
  g_clear_object(&service->session_bus);
  bs_state_store_clear_tray_items(service->store);
  service->running = false;
}

static void
bs_tray_service_unregister_all_items(BsTrayService *service) {
  g_return_if_fail(service != NULL);

  if (service->registrations != NULL) {
    g_hash_table_remove_all(service->registrations);
  }
}

static void
bs_tray_service_unregister_item(BsTrayService *service, const char *item_id) {
  bool removed = false;

  g_return_if_fail(service != NULL);
  g_return_if_fail(item_id != NULL);

  removed = g_hash_table_remove(service->registrations, item_id);
  if (!removed) {
    return;
  }

  bs_state_store_begin_update(service->store);
  (void) bs_state_store_remove_tray_item(service->store, item_id);
  bs_state_store_finish_update(service->store);

  if (service->session_bus != NULL) {
    g_dbus_connection_emit_signal(service->session_bus,
                                  NULL,
                                  BS_TRAY_WATCHER_OBJECT_PATH,
                                  BS_TRAY_WATCHER_INTERFACE,
                                  "StatusNotifierItemUnregistered",
                                  g_variant_new("(s)", item_id),
                                  NULL);
  }
}

static bool
bs_tray_service_refresh_registration(BsTrayRegistration *registration, GError **error) {
  BsTrayItem item = {0};
  const char *name_owner = NULL;

  g_return_val_if_fail(registration != NULL, false);
  g_return_val_if_fail(registration->service != NULL, false);
  g_return_val_if_fail(registration->item_proxy != NULL, false);

  name_owner = g_dbus_proxy_get_name_owner(registration->item_proxy);
  if (name_owner == NULL || *name_owner == '\0') {
    bs_state_store_begin_update(registration->service->store);
    (void) bs_state_store_remove_tray_item(registration->service->store,
                                           registration->item_id);
    bs_state_store_finish_update(registration->service->store);
    return true;
  }

  item.item_id = g_strdup(registration->item_id);
  item.bus_name = g_strdup(registration->bus_name);
  item.object_path = g_strdup(registration->object_path);
  item.menu_object_path = bs_tray_service_dup_proxy_string(registration->item_proxy, "Menu");
  item.id = bs_tray_service_dup_proxy_string(registration->item_proxy, "Id");
  item.title = bs_tray_service_dup_proxy_string(registration->item_proxy, "Title");
  item.icon_name = bs_tray_service_dup_proxy_string(registration->item_proxy, "IconName");
  item.attention_icon_name = bs_tray_service_dup_proxy_string(registration->item_proxy,
                                                              "AttentionIconName");
  item.status = bs_tray_service_get_proxy_status(registration->item_proxy);
  item.item_is_menu = bs_tray_service_get_proxy_bool(registration->item_proxy,
                                                     "ItemIsMenu",
                                                     false);
  item.has_activate = true;
  item.has_context_menu = true;

  (void) error;
  bs_state_store_begin_update(registration->service->store);
  (void) bs_state_store_replace_tray_item(registration->service->store, &item);
  bs_state_store_finish_update(registration->service->store);
  bs_tray_item_clear(&item);
  return true;
}

static bool
bs_tray_service_register_item(BsTrayService *service,
                              const char *sender,
                              const char *service_or_path,
                              GError **error) {
  g_autofree char *item_id = NULL;
  g_autofree char *bus_name = NULL;
  g_autofree char *object_path = NULL;
  BsTrayRegistration *registration = NULL;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(sender != NULL, false);
  g_return_val_if_fail(service_or_path != NULL, false);

  if (service_or_path[0] == '/') {
    bus_name = g_strdup(sender);
    object_path = g_strdup(service_or_path);
  } else {
    bus_name = g_strdup(service_or_path);
    object_path = g_strdup("/StatusNotifierItem");
  }
  item_id = bs_tray_service_build_item_id(bus_name, object_path);
  if (item_id == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "invalid tray item registration");
    return false;
  }

  bs_tray_service_unregister_item(service, item_id);

  registration = g_new0(BsTrayRegistration, 1);
  registration->service = service;
  registration->item_id = g_strdup(item_id);
  registration->bus_name = g_strdup(bus_name);
  registration->object_path = g_strdup(object_path);
  registration->item_proxy = g_dbus_proxy_new_sync(service->session_bus,
                                                   G_DBUS_PROXY_FLAGS_NONE,
                                                   NULL,
                                                   registration->bus_name,
                                                   registration->object_path,
                                                   BS_TRAY_ITEM_INTERFACE,
                                                   NULL,
                                                   error);
  if (registration->item_proxy == NULL) {
    bs_tray_registration_free(registration);
    return false;
  }

  g_signal_connect(registration->item_proxy,
                   "g-properties-changed",
                   G_CALLBACK(bs_tray_service_on_proxy_properties_changed),
                   registration);
  g_signal_connect(registration->item_proxy,
                   "g-signal",
                   G_CALLBACK(bs_tray_service_on_proxy_signal),
                   registration);
  registration->name_watch_id = g_bus_watch_name_on_connection(service->session_bus,
                                                               registration->bus_name,
                                                               G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                               NULL,
                                                               bs_tray_service_on_name_vanished,
                                                               registration,
                                                               NULL);

  if (!bs_tray_service_refresh_registration(registration, error)) {
    bs_tray_registration_free(registration);
    return false;
  }

  g_hash_table_replace(service->registrations, g_strdup(registration->item_id), registration);
  g_dbus_connection_emit_signal(service->session_bus,
                                NULL,
                                BS_TRAY_WATCHER_OBJECT_PATH,
                                BS_TRAY_WATCHER_INTERFACE,
                                "StatusNotifierItemRegistered",
                                g_variant_new("(s)", registration->item_id),
                                NULL);
  return true;
}

static void
bs_tray_service_on_name_vanished(GDBusConnection *connection,
                                 const char *name,
                                 gpointer user_data) {
  BsTrayRegistration *registration = user_data;

  (void) connection;
  g_return_if_fail(registration != NULL);
  g_return_if_fail(registration->service != NULL);

  if (name != NULL && g_strcmp0(name, registration->bus_name) != 0) {
    return;
  }

  bs_tray_service_unregister_item(registration->service, registration->item_id);
}

static void
bs_tray_service_on_proxy_properties_changed(GDBusProxy *proxy,
                                            GVariant *changed_properties,
                                            const gchar * const *invalidated_properties,
                                            gpointer user_data) {
  BsTrayRegistration *registration = user_data;

  (void) proxy;
  (void) changed_properties;
  (void) invalidated_properties;
  if (registration == NULL || registration->service == NULL || !registration->service->running) {
    return;
  }

  (void) bs_tray_service_refresh_registration(registration, NULL);
}

static void
bs_tray_service_on_proxy_signal(GDBusProxy *proxy,
                                const gchar *sender_name,
                                const gchar *signal_name,
                                GVariant *parameters,
                                gpointer user_data) {
  BsTrayRegistration *registration = user_data;

  (void) proxy;
  (void) sender_name;
  (void) parameters;
  if (registration == NULL || registration->service == NULL || !registration->service->running) {
    return;
  }

  if (g_strcmp0(signal_name, "NewTitle") == 0
      || g_strcmp0(signal_name, "NewIcon") == 0
      || g_strcmp0(signal_name, "NewAttentionIcon") == 0
      || g_strcmp0(signal_name, "NewStatus") == 0
      || g_strcmp0(signal_name, "NewMenu") == 0) {
    (void) bs_tray_service_refresh_registration(registration, NULL);
  }
}

static void
bs_tray_service_handle_method_call(GDBusConnection *connection,
                                   const char *sender,
                                   const char *object_path,
                                   const char *interface_name,
                                   const char *method_name,
                                   GVariant *parameters,
                                   GDBusMethodInvocation *invocation,
                                   gpointer user_data) {
  BsTrayService *service = user_data;
  g_autoptr(GError) error = NULL;
  const char *service_or_path = NULL;

  (void) connection;
  (void) object_path;
  (void) interface_name;
  g_return_if_fail(service != NULL);

  if (g_strcmp0(method_name, "RegisterStatusNotifierItem") == 0) {
    BsTrayPendingRegistration *pending = g_new0(BsTrayPendingRegistration, 1);

    g_variant_get(parameters, "(&s)", &service_or_path);
    pending->service = service;
    pending->sender = g_strdup(sender);
    pending->service_or_path = g_strdup(service_or_path);

    g_dbus_method_invocation_return_value(invocation, NULL);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    bs_tray_service_register_item_idle,
                    pending,
                    bs_tray_service_pending_registration_free);
    return;
  }

  if (g_strcmp0(method_name, "RegisterStatusNotifierHost") == 0) {
    g_dbus_connection_emit_signal(service->session_bus,
                                  NULL,
                                  BS_TRAY_WATCHER_OBJECT_PATH,
                                  BS_TRAY_WATCHER_INTERFACE,
                                  "StatusNotifierHostRegistered",
                                  NULL,
                                  NULL);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  g_dbus_method_invocation_return_error(invocation,
                                        G_IO_ERROR,
                                        G_IO_ERROR_NOT_SUPPORTED,
                                        "unsupported tray watcher method: %s",
                                        method_name);
}

static GVariant *
bs_tray_service_handle_get_property(GDBusConnection *connection,
                                    const char *sender,
                                    const char *object_path,
                                    const char *interface_name,
                                    const char *property_name,
                                    GError **error,
                                    gpointer user_data) {
  BsTrayService *service = user_data;
  g_autoptr(GPtrArray) items = NULL;
  GHashTableIter iter;
  gpointer key = NULL;

  (void) connection;
  (void) sender;
  (void) object_path;
  (void) interface_name;
  (void) error;
  g_return_val_if_fail(service != NULL, NULL);

  if (g_strcmp0(property_name, "IsStatusNotifierHostRegistered") == 0) {
    return g_variant_new_boolean(service->running);
  }
  if (g_strcmp0(property_name, "ProtocolVersion") == 0) {
    return g_variant_new_int32(0);
  }
  if (g_strcmp0(property_name, "RegisteredStatusNotifierItems") == 0) {
    items = g_ptr_array_new_with_free_func(g_free);
    g_hash_table_iter_init(&iter, service->registrations);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
      g_ptr_array_add(items, g_strdup(key));
    }
    g_ptr_array_sort(items, (GCompareFunc) g_strcmp0);
    g_ptr_array_add(items, NULL);
    return g_variant_new_strv((const char * const *) items->pdata, items->len - 1);
  }

  return NULL;
}

static bool
bs_tray_service_call_item_method(BsTrayService *service,
                                 const char *item_id,
                                 const char *method_name,
                                 int32_t x,
                                 int32_t y,
                                 GError **error) {
  BsTrayRegistration *registration = NULL;

  g_return_val_if_fail(service != NULL, false);
  g_return_val_if_fail(item_id != NULL, false);
  g_return_val_if_fail(method_name != NULL, false);

  registration = bs_tray_service_lookup_registration(service, item_id);
  if (registration == NULL || registration->item_proxy == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "unknown tray item: %s", item_id);
    return false;
  }

  return g_dbus_proxy_call_sync(registration->item_proxy,
                                method_name,
                                g_variant_new("(ii)", x, y),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                error) != NULL;
}

bool
bs_tray_service_activate_item(BsTrayService *service,
                              const char *item_id,
                              int32_t x,
                              int32_t y,
                              GError **error) {
  return bs_tray_service_call_item_method(service, item_id, "Activate", x, y, error);
}

bool
bs_tray_service_context_menu_item(BsTrayService *service,
                                  const char *item_id,
                                  int32_t x,
                                  int32_t y,
                                  GError **error) {
  return bs_tray_service_call_item_method(service, item_id, "ContextMenu", x, y, error);
}

const char *
bs_tray_service_watcher_name(const BsTrayService *service) {
  g_return_val_if_fail(service != NULL, NULL);
  return service->watcher_name;
}

bool
bs_tray_service_running(const BsTrayService *service) {
  g_return_val_if_fail(service != NULL, false);
  return service->running;
}
