// web.cpp — static file serving + SSE broadcaster.
#include "web.h"

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

namespace web {

static std::string g_www_root = ".";
static std::atomic<int> g_next_id{1};

struct SseClient {
  int id;
  // In a full implementation we'd hold a socket/response reference. Since
  // Crow manages the connection per-request, we keep a simple registry of
  // active client ids and a message ring buffer instead.
};

static std::mutex g_sse_mu;
static std::vector<std::string> g_sse_ring;  // recent events (ring buffer)

void set_www_root(const std::string& dir) { g_www_root = dir; }

std::string read_file(const std::string& rel_path) {
  // Prevent path traversal.
  if (rel_path.find("..") != std::string::npos) return "";
  std::string full = g_www_root + "/" + rel_path;
  std::ifstream f(full, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string mime_type(const std::string& path) {
  std::string p = path;
  size_t dot = p.find_last_of('.');
  std::string ext = (dot == std::string::npos) ? "" : p.substr(dot + 1);
  if (ext == "html") return "text/html";
  if (ext == "css") return "text/css";
  if (ext == "js") return "application/javascript";
  if (ext == "json") return "application/json";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "png") return "image/png";
  if (ext == "ico") return "image/x-icon";
  return "text/plain";
}

int sse_register() { return g_next_id.fetch_add(1); }

void sse_broadcast(const std::string& event, const std::string& data) {
  std::lock_guard<std::mutex> lk(g_sse_mu);
  g_sse_ring.push_back("event: " + event + "\ndata: " + data + "\n\n");
  // Keep only last 256 events.
  if (g_sse_ring.size() > 256) g_sse_ring.erase(g_sse_ring.begin());
}

void sse_unregister(int id) { (void)id; }

}  // namespace web
