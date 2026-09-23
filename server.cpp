// server.cpp
// A minimal HTTP server (raw POSIX sockets, no library) that exposes the
// scheduling engine over HTTP so the frontend can call the REAL C++ engine
// instead of its in-browser JavaScript port. This is deliberately a
// SEPARATE executable from main.cpp (main.cpp is still the plain CLI demo)
// -- build this one with server.cpp + scheduler.cpp, NOT main.cpp, since
// both files define main().
//
// Routes:
//   GET  /                -> serves public/index.html (the frontend)
//   GET  /api/health      -> {"status":"ok"}
//   POST /api/schedule    -> body: {theatre, screens, movies} -> full EngineResult as JSON
//   OPTIONS *             -> CORS preflight response
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>
#include "models.h"
#include "scheduler.h"
#include "json.h"

// ---------- tiny HTTP helpers ----------------------------------------

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string httpResponse(int code, const std::string& status,
                                 const std::string& contentType, const std::string& body) {
    std::ostringstream o;
    o << "HTTP/1.1 " << code << " " << status << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Connection: close\r\n\r\n"
      << body;
    return o.str();
}

struct HttpRequest { std::string method, path, body; };

// Reads one HTTP request off a socket: headers first, then the body
// (using Content-Length to know how many more bytes to read for POSTs).
static HttpRequest readRequest(int client) {
    std::string raw;
    char buf[8192];
    ssize_t n;
    size_t headerEnd = std::string::npos;
    while ((n = recv(client, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, n);
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
    }
    HttpRequest req;
    if (headerEnd == std::string::npos) return req;

    std::istringstream headerStream(raw.substr(0, headerEnd));
    std::string requestLine;
    std::getline(headerStream, requestLine);
    std::istringstream lineStream(requestLine);
    std::string httpVersion;
    lineStream >> req.method >> req.path >> httpVersion;

    size_t contentLength = 0;
    std::string headerLine;
    while (std::getline(headerStream, headerLine)) {
        if (headerLine.size() > 15 &&
            (headerLine.compare(0, 15, "Content-Length:") == 0 ||
             headerLine.compare(0, 15, "content-length:") == 0)) {
            contentLength = std::stoul(headerLine.substr(15));
        }
    }
    std::string body = raw.substr(headerEnd + 4);
    while (body.size() < contentLength && (n = recv(client, buf, sizeof(buf), 0)) > 0)
        body.append(buf, n);
    req.body = body;
    return req;
}

// ---------- JSON (request) -> engine structs --------------------------

static Theatre parseTheatre(const JsonValue& j, int& gridInterval) {
    Theatre t;
    t.name = j.getString("name", "Cinema");
    t.openingMinutes = static_cast<int>(j.getNumber("openingMinutes", 540));
    t.closingMinutes = static_cast<int>(j.getNumber("closingMinutes", 1440));
    t.turnaroundMinutes = static_cast<int>(j.getNumber("turnaroundMinutes", 15));
    gridInterval = static_cast<int>(j.getNumber("gridInterval", 30));
    return t;
}

static std::vector<Screen> parseScreens(const JsonValue& arr) {
    std::vector<Screen> out;
    for (const auto& sv : arr.arr) {
        Screen s;
        s.id = static_cast<int>(sv.getNumber("id"));
        s.name = sv.getString("name", "Screen");
        s.capacity = static_cast<int>(sv.getNumber("capacity", 100));
        s.type = sv.getString("type", "Standard");
        s.priceMultiplier = sv.getNumber("priceMultiplier", 1.0);
        s.available = sv.getBool("available", true);
        out.push_back(s);
    }
    return out;
}

static std::vector<Movie> parseMovies(const JsonValue& arr) {
    std::vector<Movie> out;
    for (const auto& mv : arr.arr) {
        Movie m;
        m.id = static_cast<int>(mv.getNumber("id"));
        m.name = mv.getString("name", "Movie");
        m.runtimeMinutes = static_cast<int>(mv.getNumber("runtimeMinutes", 100));
        m.basePrice = mv.getNumber("basePrice", 150);
        m.demandScore = static_cast<int>(mv.getNumber("demandScore", 5));
        m.genre = mv.getString("genre", "");
        m.minShows = static_cast<int>(mv.getNumber("minShows", 0));
        m.maxShows = static_cast<int>(mv.getNumber("maxShows", 0));
        out.push_back(m);
    }
    return out;
}

// ---------- request handling -------------------------------------------

static std::string handleSchedule(const std::string& body) {
    JsonParser parser(body);
    JsonValue root = parser.parse();

    const JsonValue* theatreJson = root.get("theatre");
    const JsonValue* screensJson = root.get("screens");
    const JsonValue* moviesJson = root.get("movies");
    if (!theatreJson || !screensJson || !moviesJson)
        return "{\"valid\":false,\"errors\":[\"Request must include theatre, screens and movies.\"]}";

    int grid = 30;
    Theatre theatre = parseTheatre(*theatreJson, grid);
    std::vector<Screen> screens = parseScreens(*screensJson);
    std::vector<Movie> movies = parseMovies(*moviesJson);

    EngineResult result = runEngine(theatre, screens, movies, grid);
    return toJson(theatre, screens, movies, result);
}

static void handleClient(int client) {
    HttpRequest req = readRequest(client);
    std::string response;
    try {
        if (req.method == "OPTIONS") {
            response = httpResponse(204, "No Content", "text/plain", "");
        } else if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
            std::string html = readFile("public/index.html");
            response = html.empty()
                ? httpResponse(404, "Not Found", "text/plain", "public/index.html not found")
                : httpResponse(200, "OK", "text/html; charset=utf-8", html);
        } else if (req.method == "GET" && req.path == "/api/health") {
            response = httpResponse(200, "OK", "application/json", "{\"status\":\"ok\"}");
        } else if (req.method == "POST" && req.path == "/api/schedule") {
            response = httpResponse(200, "OK", "application/json", handleSchedule(req.body));
        } else {
            response = httpResponse(404, "Not Found", "text/plain", "Not found: " + req.path);
        }
    } catch (const std::exception& e) {
        std::string err = std::string("{\"valid\":false,\"errors\":[\"Server error: ") + e.what() + "\"]}";
        response = httpResponse(500, "Internal Server Error", "application/json", err);
    }
    send(client, response.c_str(), response.size(), 0);
    close(client);
}

int main() {
    int port = 3000;
    if (const char* envPort = std::getenv("PORT")) port = std::atoi(envPort);

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(serverFd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << port << "\n";
        return 1;
    }
    listen(serverFd, 32);
    std::cout << "Cinema Scheduling Engine API listening on port " << port << "\n";
    std::cout << "  GET  /              -> frontend (public/index.html)\n";
    std::cout << "  GET  /api/health    -> health check\n";
    std::cout << "  POST /api/schedule  -> run the engine, returns full EngineResult JSON\n";

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int client = accept(serverFd, (sockaddr*)&clientAddr, &clientLen);
        if (client < 0) continue;
        std::thread(handleClient, client).detach(); // one thread per request -- fine at this scale
    }
}
