// web.h — serve the dashboard (static files) + SSE event stream.
#pragma once

#include <string>
#include <vector>

namespace web {
void set_www_root(const std::string& dir);
std::string read_file(const std::string& rel_path);
std::string mime_type(const std::string& path);

int sse_register();
void sse_broadcast(const std::string& event, const std::string& data);
void sse_unregister(int id);

}  // namespace web
