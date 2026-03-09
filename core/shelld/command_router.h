#ifndef BIT_SHELL_CORE_SHELLD_COMMAND_ROUTER_H
#define BIT_SHELL_CORE_SHELLD_COMMAND_ROUTER_H

#include <gio/gio.h>
#include <stdbool.h>

struct _BsShelldApp;
typedef struct _BsShelldApp BsShelldApp;

typedef struct _BsCommandRouter BsCommandRouter;

BsCommandRouter *bs_command_router_new(BsShelldApp *app);
void bs_command_router_free(BsCommandRouter *router);

bool bs_command_router_handle_json(BsCommandRouter *router,
                                   const char *payload,
                                   char **response_json,
                                   GError **error);

#endif
