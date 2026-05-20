#pragma once

/* Initialize HTTP server resources (scratch buffer mutex). Call before wifi_init(). */
void http_server_init(void);

/* Start the HTTP server (idempotent — ignored if already running). */
void start_httpd(void);
