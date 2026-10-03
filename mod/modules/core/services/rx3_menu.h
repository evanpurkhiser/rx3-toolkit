/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_MENU_H
#define RX3_MENU_H

/* Install the read-only Utility rows before rbp can open its menu. */
int rx3_menu_install(void);
void rx3_menu_remove(void);

#endif
