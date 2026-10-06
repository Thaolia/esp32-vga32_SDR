#include "input_ps2.h"

namespace {

bool map_key(const fabgl::VirtualKeyItem &k, app_cmd_t *cmd)
{
  switch (k.vk) {
    case fabgl::VK_RIGHT:    *cmd = k.SHIFT ? APP_CMD_FREQ_UP_10 : APP_CMD_FREQ_UP_1; return true;
    case fabgl::VK_LEFT:     *cmd = k.SHIFT ? APP_CMD_FREQ_DOWN_10 : APP_CMD_FREQ_DOWN_1; return true;
    case fabgl::VK_UP:       *cmd = APP_CMD_REF_UP; return true;
    case fabgl::VK_DOWN:     *cmd = APP_CMD_REF_DOWN; return true;
    case fabgl::VK_PAGEUP:   *cmd = APP_CMD_GAIN_UP; return true;
    case fabgl::VK_PAGEDOWN: *cmd = APP_CMD_GAIN_DOWN; return true;
    case fabgl::VK_r:
    case fabgl::VK_R:        *cmd = APP_CMD_RATE_CYCLE; return true;
    case fabgl::VK_g:
    case fabgl::VK_G:        *cmd = APP_CMD_AGC_TOGGLE; return true;
    case fabgl::VK_PLUS:
    case fabgl::VK_KP_PLUS:
    case fabgl::VK_EQUALS:   *cmd = APP_CMD_RANGE_DOWN; return true;
    case fabgl::VK_MINUS:
    case fabgl::VK_KP_MINUS: *cmd = APP_CMD_RANGE_UP; return true;
    default:                 return false;
  }
}

} // namespace

input_result_t input_poll(fabgl::Keyboard *kbd, app_settings_t *s, app_view_t *v, unsigned gain_max)
{
  input_result_t r = {false};
  if (!kbd)
    return r;
  fabgl::VirtualKeyItem item;
  while (kbd->getNextVirtualKey(&item, 0)) {
    app_cmd_t cmd;
    if (!item.down || !map_key(item, &cmd))
      continue;
    if (app_apply_cmd(cmd, s, v, gain_max) == APP_CHANGE_RADIO)
      r.radio_changed = true;
  }
  return r;
}
