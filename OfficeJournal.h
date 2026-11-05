#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QDateTime>
#include <QUuid>
#include <json.hpp>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>

#ifndef OFFICE_SOURCE_REVISION
#define OFFICE_SOURCE_REVISION "unversioned"
#endif

namespace office {
// One native owner; callback threads serialize complete records. No images,
// titles or request bodies enter this bounded, independent diagnostic channel.
class Journal {
    std::filesystem::path path;
    HANDLE file=INVALID_HANDLE_VALUE;
    mutable std::mutex mutex;
    std::string failure;
    const std::string session=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const std::chrono::steady_clock::time_point began=std::chrono::steady_clock::now();
    uint64_t sequence=0,bytes=0,limit;
    unsigned segments;
    void fail(const char* operation) {failure=std::string(operation)+" Win32="+std::to_string(GetLastError());}
    void close() {if(file!=INVALID_HANDLE_VALUE) {CloseHandle(file);file=INVALID_HANDLE_VALUE;}}
    bool open() {
        std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
        if(ec) {failure="journal directory: "+ec.message();return false;}
        file=CreateFileW(path.c_str(),FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file==INVALID_HANDLE_VALUE) {fail("journal open");return false;}
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(file,&size)) {fail("journal size");close();return false;}
        bytes=static_cast<uint64_t>(size.QuadPart);return true;
    }
    bool rotate() {
        close();
        for(unsigned index=segments-1;index>0;--index) {
            auto from=index==1?path:std::filesystem::path(path.wstring()+L"."+std::to_wstring(index-1));
            auto to=std::filesystem::path(path.wstring()+L"."+std::to_wstring(index));
            if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) && GetLastError()!=ERROR_FILE_NOT_FOUND) {fail("journal rotate");return false;}
        }
        if(segments==1 && !DeleteFileW(path.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND) {fail("journal rotate");return false;}
        return open();
    }
public:
    explicit Journal(std::filesystem::path target,uint64_t maxBytes=4*1024*1024,unsigned retained=8):path(std::move(target)),limit(maxBytes),segments(std::max(1u,retained)) {}
    ~Journal() {close();}
    std::string error() const {std::lock_guard lock(mutex);return failure;}
    void write(const char* event,nlohmann::json data=nlohmann::json::object()) noexcept {
        std::lock_guard lock(mutex);
        try {
            nlohmann::json record={{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()},
                {"elapsed_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()},
                {"pid",GetCurrentProcessId()},{"tid",GetCurrentThreadId()},{"session",session},{"sequence",++sequence},
                {"build",OFFICE_SOURCE_REVISION},{"event",event},{"data",std::move(data)}};
            auto line=record.dump(-1,' ',false,nlohmann::json::error_handler_t::replace)+"\n";
            if(line.size()>limit) {failure="journal record exceeds segment limit";return;}
            if(file==INVALID_HANDLE_VALUE && !open())return;
            if(bytes && bytes+line.size()>limit && !rotate())return;
            DWORD written{};
            if(!WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) || written!=line.size()) {fail("journal write");close();return;}
            bytes+=written;failure.clear();
        } catch(std::exception const& e) {failure=e.what();close();}
        catch(...) {failure="journal serialization failed";close();}
    }
};
}
