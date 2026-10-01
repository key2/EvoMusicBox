#include "live/IconFetcher.h"
#include "util/Hash.h"
#include "util/Paths.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <curl/curl.h>

namespace fs = std::filesystem;

namespace evobox
{

namespace
{
// TLS trust for the icon downloads. The curl-impersonate shipped on Windows is a self-contained
// BoringSSL build without a certificate store, so the Mozilla bundle installed next to evobox.exe
// (cacert.pem, tools/windows/build.sh) is used — the same lookup ttlive's own HTTP client does
// (CURL_CA_BUNDLE / SSL_CERT_FILE, then the executable's directory). "" = libcurl's defaults.
const std::string& caBundlePath()
{
    static const std::string path = []
    {
        std::error_code ec;
        for (const char* env : { "CURL_CA_BUNDLE", "SSL_CERT_FILE" })
            if (const char* v = std::getenv(env); v && *v && fs::exists(v, ec)) return std::string(v);
        for (const char* name : { "curl-ca-bundle.crt", "cacert.pem" })
        {
            fs::path p = paths::exeDir() / name;
            if (fs::exists(p, ec)) return p.string();
        }
        return std::string();
    }();
    return path;
}
} // namespace

IconFetcher::IconFetcher() = default;
IconFetcher::~IconFetcher() { stop(); }

std::string IconFetcher::cachePathFor(const std::string& cacheDir, const std::string& url)
{
    std::string ext;
    // keep a recognisable extension when the URL has one (helps probing)
    size_t q = url.find('?');
    std::string base = q == std::string::npos ? url : url.substr(0, q);
    size_t dot = base.find_last_of('.');
    size_t slash = base.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash) && base.size() - dot <= 6)
        ext = base.substr(dot);
    return (fs::path(cacheDir) / (sha1Hex(url) + ext)).string();
}

void IconFetcher::start(const std::string& cacheDir)
{
    if (running()) return;
    cacheDir_ = cacheDir;
    std::error_code ec;
    fs::create_directories(cacheDir_, ec);
    requests_.reset();
    thread_ = std::thread([this] { run(); });
}

void IconFetcher::stop()
{
    if (!running()) return;
    requests_.stop();
    thread_.join();
    requests_.clear();
}

void IconFetcher::fetch(const std::string& url, int64_t giftId)
{
    if (url.empty()) return;
    requests_.push(IconRequest{ url, giftId });
}

size_t IconFetcher::drain(const std::function<void(const IconResult&)>& fn)
{
    return results_.drain([&](IconResult& r) { fn(r); });
}

static size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = (std::vector<char>*)userdata;
    out->insert(out->end(), ptr, ptr + size * nmemb);
    return size * nmemb;
}

void IconFetcher::run()
{
    CURL* curl = curl_easy_init();
    while (true)
    {
        auto req = requests_.waitPop();
        if (!req) break;
        IconResult res;
        res.giftId = req->giftId;
        res.url = req->url;
        res.path = cachePathFor(cacheDir_, req->url);
        std::error_code ec;
        if (fs::exists(res.path, ec) && fs::file_size(res.path, ec) > 0)
        {
            res.ok = true;
            res.fromCache = true;
            results_.push(std::move(res));
            continue;
        }
        if (!curl) { res.error = "curl unavailable"; results_.push(std::move(res)); continue; }

        std::vector<char> body;
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, req->url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (EvoMusicBox)");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        if (!caBundlePath().empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath().c_str());
#ifdef _WIN32
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA); // + the Windows certificate store
#endif
        CURLcode rc = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        if (rc != CURLE_OK) res.error = curl_easy_strerror(rc);
        else if (http >= 400) res.error = "HTTP " + std::to_string(http);
        else if (body.empty()) res.error = "empty response";
        else
        {
            std::string tmp = res.path + ".part";
            FILE* f = fopen(tmp.c_str(), "wb");
            if (!f) res.error = "cannot write cache file";
            else
            {
                fwrite(body.data(), 1, body.size(), f);
                fclose(f);
                fs::rename(tmp, res.path, ec);
                if (ec) { fs::remove(tmp, ec); res.error = "cannot move cache file"; }
                else res.ok = true;
            }
        }
        results_.push(std::move(res));
    }
    if (curl) curl_easy_cleanup(curl);
}

} // namespace evobox
