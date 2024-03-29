/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2020-2021 Paradigm4 Inc.
* All Rights Reserved.
*
* bridge is a plugin for SciDB, an Open Source Array DBMS maintained
* by Paradigm4. See http://www.paradigm4.com/
*
* bridge is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* bridge is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with bridge.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#include "S3Driver.h"

#include <log4cxx/logger.h>

// AWS
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/PutObjectRequest.h>


#define RETRY_COUNT 5
#define RETRY_SLEEP 1000        // milliseconds

#define S3_EXCEPTION_NOT_SUCCESS(operation)                             \
    {                                                                   \
        if (!outcome.IsSuccess()) {                                     \
            std::ostringstream exceptionOutput;                         \
            exceptionOutput                                             \
            << (operation) << " operation on s3://"                     \
            << _bucket << "/" << key << " failed. ";                    \
            auto error = outcome.GetError();                            \
            exceptionOutput << error.GetMessage();                      \
            if (error.GetResponseCode() ==                              \
                Aws::Http::HttpResponseCode::FORBIDDEN)                 \
                exceptionOutput                                         \
                    << "See https://aws.amazon.com/premiumsupport/"     \
                    << "knowledge-center/s3-troubleshoot-403/";         \
            throw SYSTEM_EXCEPTION(                                     \
                SCIDB_SE_NETWORK,                                       \
                SCIDB_LE_UNKNOWN_ERROR) << exceptionOutput.str();       \
        }                                                               \
    }

#define FAIL(reason, bucket, key)                                               \
    {                                                                           \
        std::ostringstream out;                                                 \
        out << (reason) << " s3://" << (bucket) << "/" << (key);                \
        throw SYSTEM_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_UNKNOWN_ERROR)      \
            << out.str();                                                       \
    }


namespace scidb {
    static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.s3driver"));

    //
    // ScopedMutex
    //
    class ScopedMutex {
    public:
        ScopedMutex(std::mutex &lock):_lock(lock) { _lock.lock(); }
        ~ScopedMutex() { _lock.unlock(); }
    private:
        std::mutex &_lock;
    };

    //
    // S3Init
    //
    S3Init::S3Init()
    {
        {
             ScopedMutex lock(_lock); // LOCK

             if (s_count == 0)
                 Aws::InitAPI(_awsOptions);
             s_count++;
        }
    }

    size_t S3Init::s_count = 0;

    //
    // S3Driver
    //
    S3Driver::S3Driver(const std::string &url, const Driver::Mode mode):
        Driver(url, mode)
    {
        const size_t prefix_len = 5; // "s3://"
        size_t pos = _url.find("/", prefix_len);
        if (_url.rfind("s3://", 0) != 0 || pos == std::string::npos)
            throw USER_EXCEPTION(SCIDB_SE_METADATA,
                                 SCIDB_LE_ILLEGAL_OPERATION)
                << "Invalid S3 URL " << _url;
        _bucket = _url.substr(prefix_len, pos - prefix_len).c_str();
        _prefix = _url.substr(pos + 1);

        _client = std::make_unique<Aws::S3::S3Client>();
    }

    void S3Driver::init(const Query &query)
    {
        Aws::String key((_prefix + "/metadata").c_str());

        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(_bucket);
        request.SetKey(key);

        auto outcome = _retryLoop<Aws::S3::Model::GetObjectOutcome>(
            "Get", key, request, &Aws::S3::S3Client::GetObject, false);

        if (_mode == Driver::Mode::READ
            || _mode == Driver::Mode::UPDATE) {
            // metadata *needs to* exist
            if (!outcome.IsSuccess()) {
                FAIL("Array not found, missing metadata", _bucket, key);
            }
        }
        else if (_mode == Driver::Mode::WRITE) {
            // metadata *cannot* exist
            if (outcome.IsSuccess()) {
                FAIL("Array found, metadata exists", _bucket, key);
            }
        }
    }

    size_t S3Driver::_readArrow(const std::string &suffix,
                                std::shared_ptr<arrow::Buffer> &buffer,
                                bool reuse) const
    {
        Aws::String key((_prefix + "/" + suffix).c_str());

        auto&& result = _getRequest(key);

        auto length = result.GetContentLength();
        _setBuffer(suffix, buffer, reuse, length);

        auto& body = result.GetBody();
        body.read(reinterpret_cast<char*>(buffer->mutable_data()), length);

        return length;
    }

    void S3Driver::writeArrow(const std::string &suffix,
                              std::shared_ptr<const arrow::Buffer> buffer) const
    {
        Aws::String key((_prefix + "/" + suffix).c_str());

        std::shared_ptr<Aws::IOStream> data =
            Aws::MakeShared<Aws::StringStream>("");
        data->write(reinterpret_cast<const char*>(buffer->data()),
                    buffer->size());

        _putRequest(key, data);
    }

    void S3Driver::_readMetadataFile(std::shared_ptr<Metadata> metadata) const
    {
        Aws::String key((_prefix + "/metadata").c_str());

        auto&& result = _getRequest(key);
        auto& body = result.GetBody();
        std::string line;
        while (std::getline(body, line)) {
            std::istringstream stream(line);
            std::string key, value;
            if (!std::getline(stream, key, '\t')
                || !std::getline(stream, value)) {
                std::ostringstream out;
                out << "Invalid metadata line '" << line << "'";
                throw SYSTEM_EXCEPTION(SCIDB_SE_METADATA, SCIDB_LE_UNKNOWN_ERROR)
                    << out.str();
            }
            (*metadata)[key] = value;
        }
    }

    void S3Driver::writeMetadata(std::shared_ptr<const Metadata> metadata) const
    {
        Aws::String key((_prefix + "/metadata").c_str());

        std::shared_ptr<Aws::IOStream> data =
            Aws::MakeShared<Aws::StringStream>("");
        for (auto i = metadata->begin(); i != metadata->end(); ++i)
            *data << i->first << "\t" << i->second << "\n";

        _putRequest(key, data);
    }

    size_t S3Driver::count(const std::string& suffix) const
    {
        Aws::String key((_prefix + "/" + suffix).c_str());
        Aws::S3::Model::ListObjectsRequest request;
        request.WithBucket(_bucket);
        request.WithPrefix(key);

        auto outcome = _retryLoop<Aws::S3::Model::ListObjectsOutcome>(
            "List", key, request, &Aws::S3::S3Client::ListObjects);

        return outcome.GetResult().GetContents().size();
    }

    const std::string& S3Driver::getURL() const
    {
        return _url;
    }

    Aws::S3::Model::GetObjectResult S3Driver::_getRequest(const Aws::String &key) const
    {
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(_bucket);
        request.SetKey(key);

        auto outcome = _retryLoop<Aws::S3::Model::GetObjectOutcome>(
            "Get", key, request, &Aws::S3::S3Client::GetObject);

        return outcome.GetResultWithOwnership();
    }

    void S3Driver::_putRequest(const Aws::String &key,
                              std::shared_ptr<Aws::IOStream> data) const
    {
        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(_bucket);
        request.SetKey(key);
        request.SetBody(data);

        _retryLoop<Aws::S3::Model::PutObjectOutcome>(
            "Put", key, request, &Aws::S3::S3Client::PutObject);
    }

    // Re-try Loop Template
    template <typename Outcome, typename Request, typename RequestFunc>
    Outcome S3Driver::_retryLoop(const std::string &name,
                                 const Aws::String &key,
                                 const Request &request,
                                 RequestFunc requestFunc,
                                 bool throwIfFails) const
    {
        LOG4CXX_DEBUG(logger, "S3DRIVER|" << name << ":" << key);
        auto outcome = ((*_client).*requestFunc)(request);

        // -- - Retry - --
        int retry = 1;
        while (!outcome.IsSuccess() && retry < RETRY_COUNT) {
            LOG4CXX_WARN(logger,
                         "S3DRIVER|" << name << " s3://" << _bucket << "/"
                         << key << " attempt #" << retry << " failed");
            retry++;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(RETRY_SLEEP));

            outcome = ((*_client).*requestFunc)(request);
        }

        if (throwIfFails) {
            S3_EXCEPTION_NOT_SUCCESS(name);
        }
        return outcome;
    }
} // namespace scidb
