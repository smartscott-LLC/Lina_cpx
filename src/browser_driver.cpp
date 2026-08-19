/**
 * browser_driver.cpp — her browser hands (Implementation, D-042)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Zero Python, zero wrappers, zero new dependencies: a minimal RFC 6455
 * WebSocket client + the Chrome DevTools Protocol, driving Chrome/Brave/
 * Playwright's Chromium binaries. Every hand is approval-gated like the rest
 * (D-040) — the approval engine is the only gate; there are no URL or command
 * restrictions.
 */

#include "browser_driver.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

#include "tool_engine.hpp"

namespace lina::tools {

namespace {

namespace fs = std::filesystem;

// ===========================================================================
// BASE64 (RFC 4648) — WebSocket key encoding + screenshot decoding
// ===========================================================================

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < len ? data[i + 1] : 0;
        const uint32_t c = i + 2 < len ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        out += kBase64Table[(triple >> 18) & 0x3f];
        out += kBase64Table[(triple >> 12) & 0x3f];
        out += i + 1 < len ? kBase64Table[(triple >> 6) & 0x3f] : '=';
        out += i + 2 < len ? kBase64Table[triple & 0x3f] : '=';
    }
    return out;
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string base64_decode(const std::string& input) {
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const int v = base64_value(c);
        if (v < 0) continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xff);
        }
    }
    return out;
}

// ===========================================================================
// SHA-1 (RFC 3174) — WebSocket handshake accept validation
// ===========================================================================

struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
                     0xc3d2e1f0};
    uint64_t length = 0;
    unsigned char block[64];
    size_t block_len = 0;

    static uint32_t rol(uint32_t v, int bits) {
        return (v << bits) | (v >> (32 - bits));
    }

    void process_block() {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
                   | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                   | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
                   | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            const uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        block_len = 0;
    }

    void update(const unsigned char* data, size_t len) {
        length += len;
        while (len > 0) {
            const size_t take = std::min(len, 64 - block_len);
            std::memcpy(block + block_len, data, take);
            block_len += take;
            data += take;
            len -= take;
            if (block_len == 64) process_block();
        }
    }

    std::string digest() {
        const uint64_t bit_length = length * 8;
        const unsigned char pad = 0x80;
        update(&pad, 1);
        const unsigned char zero = 0x00;
        while (block_len != 56) update(&zero, 1);
        unsigned char len_bytes[8];
        for (int i = 0; i < 8; ++i) {
            len_bytes[i] = static_cast<unsigned char>(
                (bit_length >> (56 - i * 8)) & 0xff);
        }
        update(len_bytes, 8);
        std::string out;
        for (uint32_t v : h) {
            out += static_cast<char>((v >> 24) & 0xff);
            out += static_cast<char>((v >> 16) & 0xff);
            out += static_cast<char>((v >> 8) & 0xff);
            out += static_cast<char>(v & 0xff);
        }
        return out;
    }
};

// ===========================================================================
// MINIMAL RFC 6455 WEBSOCKET CLIENT — one connection, text frames
// ===========================================================================

class MiniWebSocket {
public:
    ~MiniWebSocket() { close(); }

    bool connect(const std::string& url, std::string* error) {
        // ws://host:port/path
        if (url.rfind("ws://", 0) != 0) {
            *error = "unsupported ws url";
            return false;
        }
        std::string rest = url.substr(5);
        std::string host_port = rest;
        std::string path = "/";
        const auto slash = rest.find('/');
        if (slash != std::string::npos) {
            host_port = rest.substr(0, slash);
            path = rest.substr(slash);
        }
        std::string host = host_port;
        std::string port = "80";
        const auto colon = host_port.rfind(':');
        if (colon != std::string::npos) {
            host = host_port.substr(0, colon);
            port = host_port.substr(colon + 1);
        }

        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* results = nullptr;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &results) != 0) {
            *error = "dns failed for " + host;
            return false;
        }
        int fd = -1;
        for (auto* ai = results; ai; ai = ai->ai_next) {
            fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
            ::close(fd);
            fd = -1;
        }
        freeaddrinfo(results);
        if (fd < 0) {
            *error = "connect failed to " + host_port;
            return false;
        }
        fd_ = fd;

        // HTTP/1.1 upgrade with a random Sec-WebSocket-Key.
        unsigned char key_bytes[16];
        std::random_device rd;
        for (unsigned char& kb : key_bytes) kb = static_cast<unsigned char>(rd());
        const std::string key = base64_encode(key_bytes, sizeof(key_bytes));

        std::ostringstream req;
        req << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host_port << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n"
            << "\r\n";
        const std::string request = req.str();
        ssize_t sent = ::send(fd_, request.data(), request.size(), 0);
        if (sent != static_cast<ssize_t>(request.size())) {
            *error = "handshake write failed";
            return false;
        }

        // Read headers until \r\n\r\n.
        std::string headers;
        char ch;
        while (headers.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(fd_, &ch, 1, 0);
            if (n <= 0) {
                *error = "handshake read failed";
                return false;
            }
            headers += ch;
            if (headers.size() > 16384) {
                *error = "handshake too large";
                return false;
            }
        }
        if (headers.find(" 101 ") == std::string::npos
            && headers.find("101 ") == std::string::npos) {
            *error = "upgrade refused: " + headers.substr(0, 120);
            return false;
        }
        // Validate the accept key (SHA-1(key + GUID), base64). digest() is
        // stateful — finalize ONCE into a local before encoding.
        Sha1 sha;
        sha.update(reinterpret_cast<const unsigned char*>(key.c_str()),
                   key.size());
        const char* guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        sha.update(reinterpret_cast<const unsigned char*>(guid),
                   std::strlen(guid));
        const std::string digest = sha.digest();
        const std::string expected = base64_encode(
            reinterpret_cast<const unsigned char*>(digest.data()),
            digest.size());
        if (headers.find("Sec-WebSocket-Accept: " + expected)
            == std::string::npos) {
            const auto accept_pos = headers.find("Sec-WebSocket-Accept: ");
            const std::string sent = accept_pos == std::string::npos
                ? "<none>"
                : headers.substr(
                      accept_pos + std::strlen("Sec-WebSocket-Accept: "),
                      32);
            *error = "accept key mismatch; key=" + key + " expected="
                     + expected + " server=" + sent;
            return false;
        }
        return true;
    }

    bool send_text(const std::string& text) {
        if (fd_ < 0) return false;
        std::string frame;
        frame += static_cast<char>(0x81); // FIN + text
        const size_t len = text.size();
        unsigned char mask[4];
        std::random_device rd;
        for (unsigned char& m : mask) m = static_cast<unsigned char>(rd());

        if (len < 126) {
            frame += static_cast<char>(0x80 | len);
        } else if (len < 65536) {
            frame += static_cast<char>(0x80 | 126);
            frame += static_cast<char>((len >> 8) & 0xff);
            frame += static_cast<char>(len & 0xff);
        } else {
            frame += static_cast<char>(0x80 | 127);
            for (int i = 7; i >= 0; --i) {
                frame += static_cast<char>((len >> (i * 8)) & 0xff);
            }
        }
        frame.append(reinterpret_cast<const char*>(mask), 4);
        for (size_t i = 0; i < len; ++i) {
            frame += static_cast<char>(text[i] ^ mask[i % 4]);
        }
        const ssize_t sent = ::send(fd_, frame.data(), frame.size(), 0);
        return sent == static_cast<ssize_t>(frame.size());
    }

    // Receives one complete message payload. Returns false on close/error.
    bool receive(std::string& payload, int timeout_ms) {
        if (fd_ < 0) return false;
        payload.clear();
        for (;;) {
            struct pollfd pfd;
            pfd.fd = fd_;
            pfd.events = POLLIN;
            const int pr = poll(&pfd, 1, timeout_ms);
            if (pr <= 0) return false; // timeout or error

            unsigned char header[2];
            if (recv_exact(header, 2) != 2) return false;
            const bool fin = (header[0] & 0x80) != 0;
            const int opcode = header[0] & 0x0f;
            const bool masked = (header[1] & 0x80) != 0;
            uint64_t len = header[1] & 0x7f;
            if (len == 126) {
                unsigned char ext[2];
                if (recv_exact(ext, 2) != 2) return false;
                len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
            } else if (len == 127) {
                unsigned char ext[8];
                if (recv_exact(ext, 8) != 8) return false;
                len = 0;
                for (int i = 0; i < 8; ++i) {
                    len = (len << 8) | ext[i];
                }
            }
            unsigned char mask[4] = {0, 0, 0, 0};
            if (masked && recv_exact(mask, 4) != 4) return false;

            std::string data(static_cast<size_t>(len), '\0');
            if (len > 0 && recv_exact(reinterpret_cast<unsigned char*>(&data[0]),
                                      static_cast<size_t>(len))
                               != static_cast<ssize_t>(len)) {
                return false;
            }
            if (masked) {
                for (size_t i = 0; i < data.size(); ++i) {
                    data[i] = static_cast<char>(data[i] ^ mask[i % 4]);
                }
            }
            if (opcode == 0x9) { // ping → pong
                send_pong(data);
                continue;
            }
            if (opcode == 0x8) { // close
                close();
                return false;
            }
            if (opcode == 0x1 || opcode == 0x2) { // text/binary
                payload += data;
                if (fin) return true;
                continue; // continuation frames append below
            }
            if (opcode == 0x0) { // continuation
                payload += data;
                if (fin) return true;
            }
        }
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool connected() const { return fd_ >= 0; }

private:
    int fd_{-1};

    ssize_t recv_exact(unsigned char* buf, size_t len) {
        size_t got = 0;
        while (got < len) {
            const ssize_t n = ::recv(fd_, buf + got, len - got, 0);
            if (n <= 0) return static_cast<ssize_t>(got);
            got += static_cast<size_t>(n);
        }
        return static_cast<ssize_t>(got);
    }

    void send_pong(const std::string& data) {
        std::string frame;
        frame += static_cast<char>(0x8a);
        if (data.size() < 126) {
            frame += static_cast<char>(data.size());
        } else {
            frame += static_cast<char>(126);
            frame += static_cast<char>((data.size() >> 8) & 0xff);
            frame += static_cast<char>(data.size() & 0xff);
        }
        frame += data;
        ::send(fd_, frame.data(), frame.size(), 0);
    }
};

// ===========================================================================
// BROWSER DRIVER — launch Chrome/Brave, speak CDP
// ===========================================================================

class BrowserDriver {
public:
    // Returns a human-readable error, or empty on success.
    std::string ensure_connected() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connected()) return "";
        return launch_locked();
    }

    std::string navigate(const std::string& url) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string err = ensure_locked();
        if (!err.empty()) return err;
        return call_ok("Page.navigate", "{\"url\":\"" + escape_json(url) + "\"}");
    }

    std::string eval(const std::string& expression) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string err = ensure_locked();
        if (!err.empty()) return err;
        const std::string params =
            "{\"expression\":\"" + escape_json(expression)
            + "\",\"returnByValue\":true,\"awaitPromise\":true}";
        const std::string response = call("Runtime.evaluate", params);
        if (response.empty()) return "cdp error";
        // Extract result.result.value (string, number, or object).
        const std::string value = json_string(response, "value");
        if (!value.empty()) return value;
        const std::string object = json_object(response, "value");
        if (!object.empty()) return object;
        // Could be a bare number or null — pull the raw value field.
        const auto pos = response.find("\"value\"");
        if (pos != std::string::npos) {
            auto start = pos + 7;
            while (start < response.size() && (response[start] == ' '
                   || response[start] == ':')) ++start;
            if (start < response.size()) {
                auto end = start;
                while (end < response.size() && response[end] != ','
                       && response[end] != '}') ++end;
                return response.substr(start, end - start);
            }
        }
        return "null";
    }

    std::string click(const std::string& selector) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string err = ensure_locked();
        if (!err.empty()) return err;
        const std::string js =
            "(()=>{const e=document.querySelector(" + quote_js(selector)
            + ");if(!e)return null;const r=e.getBoundingClientRect();"
            "return JSON.stringify({x:r.x+r.width/2,y:r.y+r.height/2});})()";
        const std::string point = eval_locked(js);
        if (point.find("\"x\"") == std::string::npos) {
            return "element not found: " + selector;
        }
        const std::string x = json_string(point, "x");
        const std::string y = json_string(point, "y");
        std::string press = call("Input.dispatchMouseEvent",
            "{\"type\":\"mousePressed\",\"x\":" + x + ",\"y\":" + y
            + ",\"button\":\"left\",\"clickCount\":1}");
        std::string release = call("Input.dispatchMouseEvent",
            "{\"type\":\"mouseReleased\",\"x\":" + x + ",\"y\":" + y
            + ",\"button\":\"left\",\"clickCount\":1}");
        if (press.empty() || release.empty()) return "cdp error on click";
        return "clicked " + selector;
    }

    std::string type_text(const std::string& selector, const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string err = ensure_locked();
        if (!err.empty()) return err;
        const std::string focus = eval_locked(
            "(()=>{const e=document.querySelector(" + quote_js(selector)
            + ");if(!e)return false;e.focus();return true;})()");
        if (focus != "true") return "element not found: " + selector;
        const std::string params = "{\"text\":\"" + escape_json(text) + "\"}";
        if (call("Input.insertText", params).empty()) return "cdp error on type";
        return "typed into " + selector;
    }

    std::string screenshot(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string err = ensure_locked();
        if (!err.empty()) return err;
        const std::string response = call(
            "Page.captureScreenshot",
            "{\"format\":\"png\",\"captureBeyondViewport\":true}");
        if (response.empty()) return "cdp error on screenshot";
        const std::string encoded = json_string(response, "data");
        if (encoded.empty()) return "screenshot produced no data";
        const std::string png = base64_decode(encoded);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write: " + path;
        out.write(png.data(), static_cast<std::streamsize>(png.size()));
        out.close();
        return "saved " + std::to_string(png.size()) + " bytes to " + path;
    }

    std::string close_browser() {
        std::lock_guard<std::mutex> lock(mutex_);
        return close_locked();
    }

private:
    std::mutex mutex_;
    std::unique_ptr<MiniWebSocket> ws_;
    pid_t pid_{-1};
    int64_t next_id_{1};
    std::string session_id_;
    std::string target_id_;

    bool connected() const {
        return ws_ && ws_->connected() && !session_id_.empty();
    }

    std::string ensure_locked() {
        if (connected()) return "";
        return launch_locked();
    }

    // Assumes mutex_ is held.
    std::string close_locked() {
        if (ws_) ws_->close();
        ws_.reset();
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
        session_id_.clear();
        target_id_.clear();
        return "browser closed";
    }

    static std::string escape_json(const std::string& text) {
        std::string out;
        for (char c : text) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c;
            }
        }
        return out;
    }

    static std::string quote_js(const std::string& text) {
        std::string out = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
        return out;
    }

    std::string launch_locked() {
        const std::string binary = find_browser_binary();
        if (binary.empty()) return "no browser binary found (LINA_BROWSER_PATH)";

        // Isolated profile in /tmp so her browsing never touches a real one.
        const std::string profile =
            (fs::temp_directory_path()
             / ("lina-browser-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count())))
                .string();

        int errpipe[2];
        if (pipe(errpipe) != 0) return "pipe failed";
        const pid_t pid = fork();
        if (pid < 0) {
            close(errpipe[0]);
            close(errpipe[1]);
            return "fork failed";
        }
        if (pid == 0) {
            close(errpipe[0]);
            dup2(errpipe[1], STDERR_FILENO);
            close(errpipe[1]);
            execl(binary.c_str(), binary.c_str(),
                  "--headless",
                  "--no-sandbox",
                  "--disable-gpu",
                  "--disable-dev-shm-usage",
                  "--no-first-run",
                  "--no-default-browser-check",
                  "--remote-debugging-port=0",
                  ("--user-data-dir=" + profile).c_str(),
                  "about:blank",
                  static_cast<char*>(nullptr));
            _exit(127);
        }
        close(errpipe[1]);
        pid_ = pid;

        // Read stderr until Chrome announces the DevTools endpoint, then keep
        // reading until the full ws:// URL has arrived.
        std::string stderr_buf;
        std::string ws_url;
        bool collecting = false;
        bool url_done = false;
        char ch;
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline && !url_done) {
            struct pollfd pfd;
            pfd.fd = errpipe[0];
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 200) > 0) {
                const ssize_t n = read(errpipe[0], &ch, 1);
                if (n == 1) {
                    if (collecting) {
                        if (ch == ' ' || ch == '\r' || ch == '\n') {
                            url_done = true;
                        } else {
                            ws_url += ch;
                        }
                    } else {
                        stderr_buf += ch;
                        if (stderr_buf.find("DevTools listening on ")
                            != std::string::npos) {
                            collecting = true;
                        }
                    }
                } else if (n == 0) {
                    break;
                }
            } else {
                int status = 0;
                if (waitpid(pid, &status, WNOHANG) == pid) {
                    close(errpipe[0]);
                    pid_ = -1;
                    return "browser exited during launch";
                }
            }
        }
        close(errpipe[0]);
        if (ws_url.empty()) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
            pid_ = -1;
            return "no DevTools endpoint (stderr: " + stderr_buf.substr(0, 160)
                   + ")";
        }

        ws_ = std::make_unique<MiniWebSocket>();
        std::string error;
        if (!ws_->connect(ws_url, &error)) {
            close_locked();
            return "websocket connect failed [" + ws_url + "]: " + error;
        }

        // A tab to drive.
        const std::string created =
            call("Target.createTarget", "{\"url\":\"about:blank\"}");
        const std::string target = json_string(created, "targetId");
        if (target.empty()) {
            close_locked();
            return "createTarget failed";
        }
        target_id_ = target;
        const std::string attached = call(
            "Target.attachToTarget",
            "{\"targetId\":\"" + target + "\",\"flatten\":true}");
        session_id_ = json_string(attached, "sessionId");
        if (session_id_.empty()) {
            close_locked();
            return "attachToTarget failed";
        }
        return "";
    }

    // One CDP JSON-RPC call; returns the full response JSON (or "").
    std::string call(const std::string& method, const std::string& params) {
        if (!ws_ || !ws_->connected()) return "";
        const int64_t id = next_id_++;
        std::ostringstream msg;
        msg << "{\"id\":" << id << ",\"method\":\"" << method
            << "\",\"params\":" << params;
        if (!session_id_.empty()) {
            msg << ",\"sessionId\":\"" << session_id_ << "\"";
        }
        msg << "}";
        if (!ws_->send_text(msg.str())) return "";
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::seconds(15);
        for (;;) {
            const auto remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(deadline
                                           - std::chrono::steady_clock::now())
                                       .count();
            if (remaining <= 0) return "";
            std::string frame;
            if (!ws_->receive(frame, static_cast<int>(remaining))) return "";
            if (json_int(frame, "id", -1) == id) {
                return frame; // our response — caller checks errors
            }
            // Otherwise: an event (e.g. Page.loadEventFired) — skip it.
        }
    }

    // Like call, but surfaces CDP error messages as a readable string.
    std::string call_ok(const std::string& method, const std::string& params) {
        const std::string response = call(method, params);
        if (response.empty()) return "cdp timeout/error";
        const std::string error = json_string(response, "message");
        if (response.find("\"error\"") != std::string::npos) {
            return error.empty() ? "cdp error" : "cdp error: " + error;
        }
        return "ok";
    }

    std::string eval_locked(const std::string& expression) {
        const std::string params =
            "{\"expression\":\"" + escape_json(expression)
            + "\",\"returnByValue\":true,\"awaitPromise\":true}";
        const std::string response = call("Runtime.evaluate", params);
        return json_string(response, "value");
    }
};

BrowserDriver& browser_driver() {
    static BrowserDriver driver;
    return driver;
}

// ===========================================================================
// The browser hands (tools)
// ===========================================================================

class BrowserOpenTool : public Tool {
public:
    std::string name() const override { return "browser.open"; }
    std::string description() const override {
        return "Open a URL in her browser (launches Chrome/Brave headless on "
               "first use). args: {\"url\": \"...\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string url = json_string(args, "url");
        if (url.empty()) {
            result.error = "url is required";
            result.exit_code = 1;
            return result;
        }
        const std::string error = browser_driver().navigate(url);
        result.ok = error == "ok";
        result.exit_code = result.ok ? 0 : 1;
        result.error = result.ok ? "" : error;
        result.output = result.ok ? "opened " + url : error;
        result.summary = result.output;
        return result;
    }
};

class BrowserNavigateTool : public Tool {
public:
    std::string name() const override { return "browser.navigate"; }
    std::string description() const override {
        return "Navigate her browser to a URL. args: {\"url\": \"...\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string url = json_string(args, "url");
        if (url.empty()) {
            result.error = "url is required";
            result.exit_code = 1;
            return result;
        }
        const std::string error = browser_driver().navigate(url);
        result.ok = error == "ok";
        result.exit_code = result.ok ? 0 : 1;
        result.error = result.ok ? "" : error;
        result.output = result.ok ? "navigated to " + url : error;
        result.summary = result.output;
        return result;
    }
};

class BrowserEvalTool : public Tool {
public:
    std::string name() const override { return "browser.eval"; }
    std::string description() const override {
        return "Evaluate a JavaScript expression in the page and return the "
               "value. args: {\"expression\": \"...\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string expression = json_string(args, "expression");
        if (expression.empty()) {
            result.error = "expression is required";
            result.exit_code = 1;
            return result;
        }
        const std::string error = browser_driver().ensure_connected();
        if (!error.empty()) {
            result.error = error;
            result.exit_code = 1;
            return result;
        }
        result.output = browser_driver().eval(expression);
        result.ok = true;
        result.summary = result.output.size() > 400
            ? result.output.substr(0, 400) + "…" : result.output;
        return result;
    }
};

class BrowserTextTool : public Tool {
public:
    std::string name() const override { return "browser.text"; }
    std::string description() const override {
        return "Read the visible text of the current page. No args.";
    }
    ToolResult run(const std::string&) override {
        ToolResult result;
        const std::string error = browser_driver().ensure_connected();
        if (!error.empty()) {
            result.error = error;
            result.exit_code = 1;
            return result;
        }
        result.output = browser_driver().eval(
            "document.body?document.body.innerText:''");
        result.ok = true;
        result.summary = result.output.size() > 400
            ? result.output.substr(0, 400) + "…" : result.output;
        return result;
    }
};

class BrowserContentTool : public Tool {
public:
    std::string name() const override { return "browser.content"; }
    std::string description() const override {
        return "Read the full HTML of the current page. No args.";
    }
    ToolResult run(const std::string&) override {
        ToolResult result;
        const std::string error = browser_driver().ensure_connected();
        if (!error.empty()) {
            result.error = error;
            result.exit_code = 1;
            return result;
        }
        result.output = browser_driver().eval(
            "document.documentElement.outerHTML");
        result.ok = true;
        result.summary = result.output.size() > 400
            ? result.output.substr(0, 400) + "…" : result.output;
        return result;
    }
};

class BrowserClickTool : public Tool {
public:
    std::string name() const override { return "browser.click"; }
    std::string description() const override {
        return "Click the element matching a CSS selector. "
               "args: {\"selector\": \"...\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string selector = json_string(args, "selector");
        if (selector.empty()) {
            result.error = "selector is required";
            result.exit_code = 1;
            return result;
        }
        result.output = browser_driver().click(selector);
        result.ok = result.output.rfind("clicked", 0) == 0;
        result.exit_code = result.ok ? 0 : 1;
        result.error = result.ok ? "" : result.output;
        result.summary = result.output;
        return result;
    }
};

class BrowserTypeTool : public Tool {
public:
    std::string name() const override { return "browser.type"; }
    std::string description() const override {
        return "Type text into the element matching a CSS selector. "
               "args: {\"selector\": \"...\", \"text\": \"...\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string selector = json_string(args, "selector");
        const std::string text = json_string(args, "text");
        if (selector.empty()) {
            result.error = "selector is required";
            result.exit_code = 1;
            return result;
        }
        result.output = browser_driver().type_text(selector, text);
        result.ok = result.output.rfind("typed", 0) == 0;
        result.exit_code = result.ok ? 0 : 1;
        result.error = result.ok ? "" : result.output;
        result.summary = result.output;
        return result;
    }
};

class BrowserScreenshotTool : public Tool {
public:
    explicit BrowserScreenshotTool(std::string workspace)
        : workspace_(std::move(workspace)) {}

    std::string name() const override { return "browser.screenshot"; }
    std::string description() const override {
        return "Capture the page as a PNG saved to the workspace. "
               "args: {\"path\": \"shot.png\"}.";
    }
    ToolResult run(const std::string& args) override {
        ToolResult result;
        const std::string raw = json_string(args, "path");
        if (raw.empty()) {
            result.error = "path is required";
            result.exit_code = 1;
            return result;
        }
        fs::path p(raw);
        const std::string path = p.is_absolute()
            ? p.string()
            : (fs::path(workspace_) / p).string();
        result.output = browser_driver().screenshot(path);
        result.ok = result.output.rfind("saved", 0) == 0;
        result.exit_code = result.ok ? 0 : 1;
        result.error = result.ok ? "" : result.output;
        result.summary = result.output;
        return result;
    }

private:
    std::string workspace_;
};

class BrowserCloseTool : public Tool {
public:
    std::string name() const override { return "browser.close"; }
    std::string description() const override {
        return "Close her browser session. No args.";
    }
    ToolResult run(const std::string&) override {
        ToolResult result;
        result.output = browser_driver().close_browser();
        result.ok = true;
        result.exit_code = 0;
        result.summary = result.output;
        return result;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Public seam
// ---------------------------------------------------------------------------

std::string find_browser_binary() {
    const char* env = std::getenv("LINA_BROWSER_PATH");
    if (env && *env && fs::exists(env)) return env;

    const char* candidates[] = {
        "google-chrome-stable", "google-chrome", "brave-browser",
        "chromium-browser", "chromium",
    };
    for (const char* candidate : candidates) {
        std::string cmd = "command -v ";
        cmd += candidate;
        std::FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) continue;
        char buf[512];
        if (std::fgets(buf, sizeof(buf), pipe)) {
            pclose(pipe);
            std::string path(buf);
            if (!path.empty() && path.back() == '\n') path.pop_back();
            if (fs::exists(path)) return path;
            continue;
        }
        pclose(pipe);
    }
    // Playwright's downloaded Chromium builds.
    const char* home = std::getenv("HOME");
    if (home) {
        const fs::path cache = fs::path(home) / ".cache" / "ms-playwright";
        if (fs::is_directory(cache)) {
            for (const auto& entry : fs::directory_iterator(cache)) {
                const fs::path chrome = entry.path()
                    / "chrome-linux" / "chrome";
                if (fs::exists(chrome)) return chrome.string();
            }
        }
    }
    return "";
}

bool browser_available() {
    return !find_browser_binary().empty();
}

std::shared_ptr<Tool> make_browser_open_tool() {
    return std::make_shared<BrowserOpenTool>();
}

std::shared_ptr<Tool> make_browser_navigate_tool() {
    return std::make_shared<BrowserNavigateTool>();
}

std::shared_ptr<Tool> make_browser_eval_tool() {
    return std::make_shared<BrowserEvalTool>();
}

std::shared_ptr<Tool> make_browser_text_tool() {
    return std::make_shared<BrowserTextTool>();
}

std::shared_ptr<Tool> make_browser_content_tool() {
    return std::make_shared<BrowserContentTool>();
}

std::shared_ptr<Tool> make_browser_click_tool() {
    return std::make_shared<BrowserClickTool>();
}

std::shared_ptr<Tool> make_browser_type_tool() {
    return std::make_shared<BrowserTypeTool>();
}

std::shared_ptr<Tool> make_browser_screenshot_tool(const std::string& workspace) {
    return std::make_shared<BrowserScreenshotTool>(workspace);
}

std::shared_ptr<Tool> make_browser_close_tool() {
    return std::make_shared<BrowserCloseTool>();
}

} // namespace lina::tools
