#include <megaapi.h>

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace
{
namespace fs = std::filesystem;
using namespace mega;

constexpr int EXIT_USAGE = 2;
constexpr int EXIT_LINK = 3;
constexpr int EXIT_FILESYSTEM = 4;
constexpr int EXIT_TRANSFER = 5;

std::mutex outputMutex;

std::string jsonEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char c : value)
    {
        switch (c)
        {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[c >> 4];
                    escaped += hex[c & 0x0f];
                }
                else
                {
                    escaped += static_cast<char>(c);
                }
        }
    }
    return escaped;
}

void emit(const std::string& json)
{
    std::lock_guard<std::mutex> lock(outputMutex);
    std::cout << json << std::endl;
}

void emitError(const std::string& stage, int code, const std::string& message)
{
    emit("{\"event\":\"error\",\"stage\":\"" + jsonEscape(stage) +
         "\",\"code\":" + std::to_string(code) +
         ",\"message\":\"" + jsonEscape(message) + "\"}");
}

std::string wideToUtf8(const wchar_t* value)
{
    if (!value || !*value)
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool looksLikePublicFileLink(const std::string& url)
{
    const bool megaHost = url.rfind("https://mega.nz/", 0) == 0 ||
                          url.rfind("https://www.mega.nz/", 0) == 0;
    const bool fileLink = url.find("/file/") != std::string::npos ||
                          url.find("/#!") != std::string::npos;
    return megaHost && fileLink && url.find('#') != std::string::npos;
}

class PublicNodeRequest final : public MegaRequestListener
{
public:
    void onRequestFinish(MegaApi*, MegaRequest* request, MegaError* error) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        errorCode_ = error->getErrorCode();
        errorText_ = error->getErrorString() ? error->getErrorString() : "Unknown MEGA error";
        if (errorCode_ == MegaError::API_OK)
        {
            node_.reset(request->getPublicMegaNode());
        }
        finished_ = true;
        condition_.notify_one();
    }

    std::unique_ptr<MegaNode> wait(bool& timedOut)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        timedOut = !condition_.wait_for(lock, std::chrono::seconds(60), [this] { return finished_; });
        return std::move(node_);
    }

    int errorCode() const { return errorCode_; }
    const std::string& errorText() const { return errorText_; }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool finished_ = false;
    int errorCode_ = MegaError::API_EINTERNAL;
    std::string errorText_ = "Request did not finish";
    std::unique_ptr<MegaNode> node_;
};

class DownloadTransfer final : public MegaTransferListener
{
public:
    void onTransferStart(MegaApi*, MegaTransfer* transfer) override
    {
        emit("{\"event\":\"start\",\"total\":" +
             std::to_string(transfer->getTotalBytes()) + "}");
    }

    void onTransferUpdate(MegaApi*, MegaTransfer* transfer) override
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (now - lastProgress_ < std::chrono::milliseconds(250) &&
            transfer->getTransferredBytes() != transfer->getTotalBytes())
        {
            return;
        }
        lastProgress_ = now;
        emit("{\"event\":\"progress\",\"transferred\":" +
             std::to_string(transfer->getTransferredBytes()) +
             ",\"total\":" + std::to_string(transfer->getTotalBytes()) +
             ",\"bytes_per_second\":" + std::to_string(transfer->getSpeed()) + "}");
    }

    void onTransferTemporaryError(MegaApi*, MegaTransfer*, MegaError* error) override
    {
        const char* errorText = error->getErrorString();
        emit("{\"event\":\"retry\",\"code\":" + std::to_string(error->getErrorCode()) +
             ",\"message\":\"" + jsonEscape(errorText ? errorText : "Unknown MEGA error") + "\"}");
    }

    void onTransferFinish(MegaApi*, MegaTransfer*, MegaError* error) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        errorCode_ = error->getErrorCode();
        errorText_ = error->getErrorString() ? error->getErrorString() : "Unknown MEGA error";
        finished_ = true;
        condition_.notify_one();
    }

    int wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return finished_; });
        return errorCode_;
    }

    const std::string& errorText() const { return errorText_; }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool finished_ = false;
    int errorCode_ = MegaError::API_EINTERNAL;
    std::string errorText_ = "Transfer did not finish";
    std::chrono::steady_clock::time_point lastProgress_{};
};

class TemporarySdkDirectory
{
public:
    TemporarySdkDirectory()
    {
        std::error_code error;
        path_ = fs::temp_directory_path(error) /
                ("SimpleMegaGrabber-" + std::to_string(GetCurrentProcessId()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!error)
        {
            fs::create_directories(path_, error);
        }
        valid_ = !error;
        if (valid_)
        {
            pathUtf8_ = path_.u8string();
        }
    }

    ~TemporarySdkDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    bool valid() const { return valid_; }
    const std::string& utf8Path() const { return pathUtf8_; }

private:
    fs::path path_;
    std::string pathUtf8_;
    bool valid_ = false;
};

class DiagnosticLogger final : public MegaLogger
{
public:
    explicit DiagnosticLogger(bool enabled): enabled_(enabled) {}

    void log(const char*, int, const char*, const char* message
#ifdef ENABLE_LOG_PERFORMANCE
             , const char**, std::size_t*, int
#endif
             ) override
    {
        if (!enabled_)
        {
            return;
        }
        if (message)
        {
            std::lock_guard<std::mutex> lock(outputMutex);
            std::cerr << message << std::endl;
        }
    }

private:
    bool enabled_;
};
}

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc != 3)
    {
        emitError("usage", EXIT_USAGE,
                  "Usage: SimpleMegaGrabber.exe <public-file-url> <output-directory>");
        return EXIT_USAGE;
    }

    const std::string url = wideToUtf8(argv[1]);
    const std::string outputArgument = wideToUtf8(argv[2]);
    if (!looksLikePublicFileLink(url))
    {
        emitError("link", EXIT_LINK, "Expected a public mega.nz file link with its decryption key");
        return EXIT_LINK;
    }

    std::error_code filesystemError;
    const fs::path outputDirectory = fs::u8path(outputArgument);
    fs::create_directories(outputDirectory, filesystemError);
    if (filesystemError || !fs::is_directory(outputDirectory))
    {
        emitError("filesystem", EXIT_FILESYSTEM, "Unable to create or access output directory");
        return EXIT_FILESYSTEM;
    }

    wchar_t debugEnvironment[2]{};
    const bool debugLogging = GetEnvironmentVariableW(
                                  L"SIMPLE_MEGA_GRABBER_DEBUG",
                                  debugEnvironment,
                                  2) > 0 &&
                              debugEnvironment[0] == L'1';
    DiagnosticLogger logger(debugLogging);
    MegaApi::addLoggerObject(&logger, true);
    MegaApi::setLogLevel(debugLogging ? MegaApi::LOG_LEVEL_DEBUG : MegaApi::LOG_LEVEL_FATAL);

    TemporarySdkDirectory sdkData;
    if (!sdkData.valid())
    {
        emitError("filesystem", EXIT_FILESYSTEM, "Unable to create temporary SDK data directory");
        MegaApi::removeLoggerObject(&logger);
        return EXIT_FILESYSTEM;
    }

    PublicNodeRequest request;
    MegaApi api("SimpleMegaGrabber",
                sdkData.utf8Path().c_str(),
                "SimpleMegaGrabber/1.0");
    api.getPublicNode(url.c_str(), &request);
    bool requestTimedOut = false;
    std::unique_ptr<MegaNode> node = request.wait(requestTimedOut);
    if (requestTimedOut)
    {
        emitError("metadata", MegaError::API_EAGAIN, "Timed out while retrieving MEGA file metadata");
        MegaApi::removeLoggerObject(&logger);
        return EXIT_LINK;
    }
    if (!node)
    {
        emitError("metadata", request.errorCode(), request.errorText());
        MegaApi::removeLoggerObject(&logger);
        return EXIT_LINK;
    }

    if (!node->isFile())
    {
        emitError("metadata", EXIT_LINK, "Folder links are not supported");
        MegaApi::removeLoggerObject(&logger);
        return EXIT_LINK;
    }

    const std::string name = node->getName() ? node->getName() : "download";
    const std::string leafName = fs::u8path(name).filename().u8string();
    if (leafName.empty() || leafName == "." || leafName == ".." || leafName != name)
    {
        emitError("filesystem", EXIT_FILESYSTEM, "MEGA file name is not a safe local file name");
        MegaApi::removeLoggerObject(&logger);
        return EXIT_FILESYSTEM;
    }
    const fs::path outputPath = outputDirectory / fs::u8path(name);
    const bool outputExists = fs::exists(outputPath, filesystemError);
    if (filesystemError)
    {
        emitError("filesystem", EXIT_FILESYSTEM, "Unable to inspect destination path");
        MegaApi::removeLoggerObject(&logger);
        return EXIT_FILESYSTEM;
    }
    if (outputExists)
    {
        emitError("filesystem", EXIT_FILESYSTEM, "Destination file already exists: " + outputPath.u8string());
        MegaApi::removeLoggerObject(&logger);
        return EXIT_FILESYSTEM;
    }

    emit("{\"event\":\"metadata\",\"name\":\"" + jsonEscape(name) +
         "\",\"size\":" + std::to_string(node->getSize()) + "}");

    DownloadTransfer transfer;
    std::unique_ptr<MegaCancelToken> cancelToken(MegaCancelToken::createInstance());
    const std::string outputPathUtf8 = outputPath.u8string();
    api.startDownload(node.get(),
                      outputPathUtf8.c_str(),
                      nullptr,
                      nullptr,
                      false,
                      cancelToken.get(),
                      MegaTransfer::COLLISION_CHECK_ALWAYSERROR,
                      MegaTransfer::COLLISION_RESOLUTION_OVERWRITE,
                      false,
                      &transfer);

    const int transferResult = transfer.wait();
    if (transferResult != MegaError::API_OK)
    {
        emitError("transfer", transferResult, transfer.errorText());
        MegaApi::removeLoggerObject(&logger);
        return EXIT_TRANSFER;
    }

    emit("{\"event\":\"complete\",\"path\":\"" + jsonEscape(outputPathUtf8) +
         "\",\"size\":" + std::to_string(node->getSize()) + "}");
    MegaApi::removeLoggerObject(&logger);
    return 0;
}
