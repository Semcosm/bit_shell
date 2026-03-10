#include "frontends/bit_dock/dock_app.h"

int
main(int argc, char **argv) {
  BsDockApp *app = bs_dock_app_new();
  int exit_code = 1;

  if (app == NULL) {
    return 1;
  }

  exit_code = bs_dock_app_run(app, argc, argv);
  bs_dock_app_free(app);
  return exit_code;
}
