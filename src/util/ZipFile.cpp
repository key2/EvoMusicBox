#include "util/ZipFile.h"
#include "util/Strings.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "miniz.h"

namespace fs = std::filesystem;

namespace evobox
{
namespace zipfile
{

namespace
{

std::string mzErr(mz_zip_archive& z)
{
    const char* s = mz_zip_get_error_string(mz_zip_get_last_error(&z));
    return s ? s : "unknown zip error";
}

std::string toArchiveName(const fs::path& rel)
{
    std::string s = rel.generic_string();
    while (str::startsWith(s, "./")) s = s.substr(2);
    return s;
}

bool safeRelative(const std::string& name)
{
    if (name.empty()) return false;
    fs::path p(name);
    if (p.is_absolute()) return false;
    for (const auto& part : p)
        if (part == "..") return false;
    // Windows drive letters / UNC inside a zip name are not acceptable either
    if (name.size() > 1 && name[1] == ':') return false;
    return true;
}

void collectFiles(const fs::path& base, const fs::path& dir, std::vector<fs::path>& out)
{
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        if (e.is_regular_file(ec)) out.push_back(fs::relative(e.path(), base, ec));
    std::sort(out.begin(), out.end());
}

} // namespace

bool writeDirectory(const std::string& zipPath, const std::string& dir, const std::vector<std::string>& rootFiles,
                    const std::vector<std::string>& subdirs, const std::vector<std::string>& storeExts, std::string* err)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    fs::path base(dir);
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return fail("not a directory: " + dir);
    std::vector<fs::path> files;
    for (const std::string& rf : rootFiles)
        if (fs::is_regular_file(base / rf, ec)) files.push_back(fs::path(rf));
    for (const std::string& sd : subdirs)
        if (fs::is_directory(base / sd, ec)) collectFiles(base, base / sd, files);

    fs::path target(zipPath);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    std::string tmp = zipPath + ".tmp";
    fs::remove(tmp, ec);

    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    if (!mz_zip_writer_init_file(&z, tmp.c_str(), 0)) return fail("cannot create " + tmp + ": " + mzErr(z));
    for (const fs::path& rel : files)
    {
        std::string name = toArchiveName(rel);
        std::string ext = str::fileExtLower(name);
        bool store = std::find(storeExts.begin(), storeExts.end(), ext) != storeExts.end();
        mz_uint level = store ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL;
        std::string src = (base / rel).string();
        if (!mz_zip_writer_add_file(&z, name.c_str(), src.c_str(), nullptr, 0, level))
        {
            std::string m = "cannot add " + name + ": " + mzErr(z);
            mz_zip_writer_end(&z);
            fs::remove(tmp, ec);
            return fail(m);
        }
    }
    if (!mz_zip_writer_finalize_archive(&z))
    {
        std::string m = "cannot finalize archive: " + mzErr(z);
        mz_zip_writer_end(&z);
        fs::remove(tmp, ec);
        return fail(m);
    }
    mz_zip_writer_end(&z);
    fs::rename(tmp, target, ec);
    if (ec)
    {
        // cross-device or Windows quirk: copy then remove
        ec.clear();
        fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) return fail("cannot write " + zipPath + ": " + ec.message());
    }
    return true;
}

bool list(const std::string& zipPath, std::vector<Entry>& out, std::string* err)
{
    out.clear();
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_file(&z, zipPath.c_str(), 0)) { if (err) *err = "cannot open " + zipPath + ": " + mzErr(z); return false; }
    mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; i++)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        Entry e;
        e.name = st.m_filename;
        e.size = st.m_uncomp_size;
        e.directory = mz_zip_reader_is_file_a_directory(&z, i) != 0;
        out.push_back(std::move(e));
    }
    mz_zip_reader_end(&z);
    return true;
}

bool readEntry(const std::string& zipPath, const std::string& name, std::string& out, std::string* err)
{
    out.clear();
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_file(&z, zipPath.c_str(), 0)) { if (err) *err = "cannot open " + zipPath + ": " + mzErr(z); return false; }
    size_t size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(&z, name.c_str(), &size, 0);
    if (!p)
    {
        if (err) *err = "'" + name + "' not found in " + zipPath + " (" + mzErr(z) + ")";
        mz_zip_reader_end(&z);
        return false;
    }
    out.assign((const char*)p, size);
    mz_free(p);
    mz_zip_reader_end(&z);
    return true;
}

bool extractTo(const std::string& zipPath, const std::string& dir, std::string* err)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    std::error_code ec;
    fs::path base(dir);
    fs::create_directories(base, ec);
    if (!fs::is_directory(base, ec)) return fail("cannot create " + dir);
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_file(&z, zipPath.c_str(), 0)) return fail("cannot open " + zipPath + ": " + mzErr(z));
    mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; i++)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        std::string name = st.m_filename;
        if (!safeRelative(name)) { mz_zip_reader_end(&z); return fail("unsafe entry name in archive: " + name); }
        fs::path dst = base / fs::path(name);
        if (mz_zip_reader_is_file_a_directory(&z, i)) { fs::create_directories(dst, ec); continue; }
        fs::create_directories(dst.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&z, i, dst.string().c_str(), 0))
        {
            std::string m = "cannot extract " + name + ": " + mzErr(z);
            mz_zip_reader_end(&z);
            return fail(m);
        }
    }
    mz_zip_reader_end(&z);
    return true;
}

bool looksLikeZip(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    unsigned char sig[4] = { 0, 0, 0, 0 };
    f.read((char*)sig, 4);
    // "PK\3\4" local header or "PK\5\6" empty archive
    return sig[0] == 'P' && sig[1] == 'K' && ((sig[2] == 3 && sig[3] == 4) || (sig[2] == 5 && sig[3] == 6));
}

} // namespace zipfile
} // namespace evobox
