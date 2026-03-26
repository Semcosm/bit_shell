#include "frontends/bit_bar/bar_app.h"

int
main(int argc, char **argv) {
  BsBarApp *app = bs_bar_app_new();
  int exit_code = 1;

  if (app == NULL) {
    return 1;
  }

  exit_code = bs_bar_app_run(app, argc, argv);
  bs_bar_app_free(app);
  return exit_code;
}
