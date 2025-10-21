<!-- @format -->

This folder contains a minimal ioLibrary-based HTTP server integration for the project.

Files:

- http_server.h/c: thin wrapper to initialize and run ioLibrary httpServer.
- http_cgi_handlers.c/h: simple CGI handlers (GPIO/get/set, network/system can be extended).

Build: CMakeLists.txt already updated to include http sources and ioLibrary httpServer sources.

Note: This is intentionally minimal to avoid conflicts with ioLibrary headers (no redefinitions).
