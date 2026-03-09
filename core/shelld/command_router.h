#ifndef BIT_SHELL_CORE_SHELLD_COMMAND_ROUTER_H
#define BIT_SHELL_CORE_SHELLD_COMMAND_ROUTER_H

#include <gio/gio.h>
#include <stdbool.h>

#include "model/ipc.h"

struct _BsShelldApp;
typedef struct _BsShelldApp BsShelldApp;

typedef struct _BsCommandRouter BsCommandRouter;

BsCommandRouter *bs_command_router_new(BsShelldApp *app);
void bs_command_router_free(BsCommandRouter *router);

bool bs_command_router_parse_request(BsCommandRouter *router,
                                     const char *payload,
                                     BsCommandRequest *request,
                                     GError **error);
bool bs_command_router_handle_request(BsCommandRouter *router,
                                      const BsCommandRequest *request,
                                      char **response_json,
                                      GError **error);
bool bs_command_router_handle_json(BsCommandRouter *router,
                                   const char *payload,
                                   char **response_json,
                                   GError **error);
char *bs_command_router_build_event_json(BsCommandRouter *router, BsTopic topic);
char *bs_command_router_build_error_json(BsCommand command,
                                         const char *code,
                                         const char *message);

#endif
