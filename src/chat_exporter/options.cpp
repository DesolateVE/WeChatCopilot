#include "options.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace wechat::chat_exporter
{
namespace
{

bool isDatabaseStorage(const std::filesystem::path& directory)
{
    return std::filesystem::is_regular_file(directory / "contact" / "contact.db")
           && std::filesystem::is_regular_file(directory / "message" / "message_0.db");
}

std::filesystem::path findDatabaseStorage(const std::filesystem::path& suppliedPath)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::absolute(suppliedPath, error);
    if (error || !std::filesystem::is_directory(root, error))
    {
        throw std::runtime_error("account data directory does not exist");
    }

    if (root.filename() == "db_storage" && isDatabaseStorage(root))
    {
        return root;
    }

    const std::filesystem::path direct = root / "db_storage";
    if (isDatabaseStorage(direct))
    {
        return direct;
    }

    std::vector<std::filesystem::path> matches;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator iterator(root, options, error), end;
         iterator != end; iterator.increment(error))
    {
        if (error)
        {
            error.clear();
            continue;
        }
        if (iterator.depth() >= 4)
        {
            iterator.disable_recursion_pending();
        }
        if (iterator->is_directory(error) && iterator->path().filename() == "db_storage"
            && isDatabaseStorage(iterator->path()))
        {
            matches.push_back(iterator->path());
            iterator.disable_recursion_pending();
        }
    }

    if (matches.empty())
    {
        throw std::runtime_error(
                "cannot find db_storage containing contact/contact.db and message/message_0.db");
    }
    if (matches.size() != 1)
    {
        throw std::runtime_error("multiple valid db_storage directories found; pass the exact db_storage path");
    }
    return matches.front();
}

} // namespace

Options parseOptions(const int argc, wchar_t* argv[])
{
    if (argc != 2)
    {
        throw std::runtime_error("usage: chat_exporter <account-data-directory-or-db_storage>");
    }
    const std::filesystem::path databaseDirectory = findDatabaseStorage(argv[1]);
    const std::filesystem::path outputRoot = std::filesystem::current_path() / "db-storage";
    return {databaseDirectory, outputRoot};
}

} // namespace wechat::chat_exporter
