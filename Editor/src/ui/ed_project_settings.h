#pragma once

/*
 * ed_project_settings.h — Project Settings window.
 *
 * Contains: Render Graph node graph tab.
 * Opens via Settings > Project Settings... in the menu bar.
 */

void ed_project_settings_init    (void *editor);
void ed_project_settings_open    (void);
void ed_project_settings_shutdown(void);
