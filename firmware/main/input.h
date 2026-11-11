#pragma once

/* Sets up MX keys and encoders and starts the polling task.
 * Results are posted to the UI as events via ui_post(). */
void input_init(void);
