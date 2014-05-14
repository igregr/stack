/*
 * Do not modify this file. Changes will be overwritten.
 *
 * Generated automatically from ../../tools/make-dissector-reg.py.
 */

#include "config.h"

#include <gmodule.h>

#include "moduleinfo.h"

/* plugins are DLLs */
#define WS_BUILD_DLL
#include "ws_symbol_export.h"

#ifndef ENABLE_STATIC
WS_DLL_PUBLIC_NOEXTERN const gchar version[] = VERSION;

/* Start the functions we need for the plugin stuff */

WS_DLL_PUBLIC_NOEXTERN void
plugin_register (void)
{
  {extern void proto_register_ircomm (void); proto_register_ircomm ();}
  {extern void proto_register_irda (void); proto_register_irda ();}
  {extern void proto_register_irsir (void); proto_register_irsir ();}
}

WS_DLL_PUBLIC_NOEXTERN void
plugin_reg_handoff(void)
{
  {extern void proto_reg_handoff_ircomm (void); proto_reg_handoff_ircomm ();}
  {extern void proto_reg_handoff_irda (void); proto_reg_handoff_irda ();}
  {extern void proto_reg_handoff_irsir (void); proto_reg_handoff_irsir ();}
}
#endif