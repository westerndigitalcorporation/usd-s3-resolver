#ifndef S3_H
#define S3_H

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <fstream>

#include <mutex>
#include <string>
#include <vector>
#include <map>

namespace usd_s3 {
    constexpr const char S3_PREFIX[] = "s3://";
    constexpr const char S3_PREFIX_SHORT[] = "s3:";
    constexpr const char S3_SUFFIX[] = ".s3";
    constexpr const char CACHE_PATH_ENV_VAR[] = "USD_S3_CACHE_PATH";
    constexpr const char PROXY_HOST_ENV_VAR[] = "USD_S3_PROXY_HOST";
    constexpr const char PROXY_PORT_ENV_VAR[] = "USD_S3_PROXY_PORT";

    class S3 {
    public:
        S3();
        ~S3();

        bool matches_schema(const std::string& path);

        std::string resolve_name(const std::string& path);
        void update_asset_info(const std::string& asset_path);
        bool fetch_asset(const std::string& asset_path, const std::string& local_path);
        double get_timestamp(const std::string& asset_path);

        private:
    };
}

#endif // S3_H