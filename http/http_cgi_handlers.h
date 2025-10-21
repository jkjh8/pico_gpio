#ifndef HTTP_CGI_HANDLERS_H
#define HTTP_CGI_HANDLERS_H

#include <stdint.h>
#include <stdbool.h>

/* Include the ioLibrary HTTP parser header to get the canonical
	definition of st_http_request (typedef struct _st_http_request ...).
	This avoids struct tag mismatches caused by an incorrect forward
	declaration. */
#include "ioLibrary_Driver/Internet/httpServer/httpParser.h"

// CGI handler entrypoint called by httpServer when a CGI request is dispatched.
// The function should use ioLibrary's send(sock, buf, len) to reply on HTTP_SOCKET_NUM.
void http_cgi_handler(st_http_request *request);

#endif // HTTP_CGI_HANDLERS_H
