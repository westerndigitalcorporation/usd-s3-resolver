#include "s3.h"
#include "debugCodes.h"

#include <pxr/base/tf/diagnosticLite.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <fstream>

#include <iostream>
#include <fstream>
#include <time.h>

#include <aws/core/utils/logging/LogMacros.h>

PXR_NAMESPACE_USING_DIRECTIVE

// -------------------------------------------------------------------------------
// If you want to print out a stacktrace everywhere S3_WARN is called, set this
// to a value > 0 - it will print out this number of stacktrace entries
#define USD_S3_DEBUG_STACKTRACE_SIZE 0

#if USD_S3_DEBUG_STACKTRACE_SIZE > 0

#include <execinfo.h>

#define S3_WARN \
    { \
        void* backtrace_array[USD_S3_DEBUG_STACKTRACE_SIZE]; \
        size_t stack_size = backtrace(backtrace_array, USD_S3_DEBUG_STACKTRACE_SIZE); \
        TF_WARN("\n\n====================================\n"); \
        TF_WARN("Stacktrace:\n"); \
        backtrace_symbols_fd(backtrace_array, stack_size, STDERR_FILENO); \
    } \
    TF_WARN

#else // STACKTRACE_SIZE

#define S3_WARN TF_WARN

#endif // STACKTRACE_SIZE

// -------------------------------------------------------------------------------

// If you want to control the number of seconds an idle connection is kept alive
// for, set this to something other than zero

#define SESSION_WAIT_TIMEOUT 0

#if SESSION_WAIT_TIMEOUT > 0

#define _USD_S3_SIMPLE_QUOTE(ARG) #ARG
#define _USD_S3_EXPAND_AND_QUOTE(ARG) _SIMPLE_QUOTE(ARG)
#define SET_SESSION_WAIT_TIMEOUT_QUERY ( "SET SESSION wait_timeout=" _USD_S3_EXPAND_AND_QUOTE( SESSION_WAIT_TIMEOUT ) )
#define SET_SESSION_WAIT_TIMEOUT_QUERY_STRLEN ( sizeof(SET_SESSION_WAIT_TIMEOUT_QUERY) - 1 )


#endif // SESSION_WAIT_TIMEOUT

// -------------------------------------------------------------------------------

namespace {
    constexpr double INVALID_TIME = std::numeric_limits<double>::lowest();

    //using mutex_scoped_lock = std::lock_guard<std::mutex>;

    // Otherwise clang static analyser will throw errors.
    template <size_t len> constexpr size_t
    cexpr_strlen(const char (&)[len]) {
        return len - 1;
    }

    // Parse an S3 url and strip off the prefix ('s3:', 's3:/' or 's3://')
    // e.g. s3://bucket/object.usd returns bucket/object.usd
    std::string parse_path(const std::string& path) {
        constexpr auto schema_length_short = cexpr_strlen(usd_s3::S3_PREFIX_SHORT);
        return path.substr(path.find_first_not_of("/", schema_length_short));
    }

    // Get the bucket from a parsed path
    // e.g. 'bucket/object.usd' returns 'bucket'
    //      'bucket/somedir/object.usd' returns 'bucket'
    const std::string get_bucket_name(const std::string& path) {
        return path.substr(0, path.find_first_of('/'));
    }

    // Get the object from a parsed path
    // e.g. 'bucket/object.usd' returns 'object.usd'
    //      'bucket/somedir/object.usd' returns 'somedir/object.usd'
    //      'bucket/object.usd?versionId=abc123' returns object.usd
    const std::string get_object_name(const std::string& path) {
        const int i = path.find_first_of('/');
        return path.substr(i, path.find_first_of('?') - i);
    }

    // Check if a parsed path uses S3 versioning
    // e.g. 'bucket/object.usd' returns False
    //      'bucket/object.usd?versionId=abc123' returns True
    const bool uses_versioning(const std::string& path) {
        return path.find("versionId=") != std::string::npos;
    }

    // Get the version ID of a parsed path uses S3 versioning
    // e.g. 'bucket/object.usd' returns an empty string
    //      'bucket/object.usd?versionId=abc123' returns abc123
    const std::string get_object_versionid(const std::string& path) {
        const uint i = path.find_first_of("versionId=");
        return (i != std::string::npos) ? path.substr(i + 10) : std::string();
    }

    // get an environment variable
    std::string get_env_var(const std::string& env_var, const std::string& default_value) {
        const auto env_var_value = getenv(env_var.c_str());
        return (env_var_value != nullptr) ? env_var_value : default_value;
    }

    // TODO: random names may be required to support multiple versions of the same asset
    // std::string generate_name(const std::string& base, const std::string& extension, char* buffer) {
    //     std::tmpnam(buffer);
    //     std::string ret(buffer);
    //     const auto last_slash = ret.find_last_of('/');
    //     if (last_slash == std::string::npos) {
    //         return base + ret + extension;
    //     } else {
    //         return base + ret.substr(last_slash + 1) + extension;
    //     }
    // }

}

namespace usd_s3 {
    Aws::SDKOptions options;
    Aws::S3::S3Client* s3_client;

    enum CacheState {
        CACHE_MISSING,
        CACHE_NEEDS_FETCHING,
        CACHE_FETCHED
    };

    struct Cache {
        CacheState state;
        std::string local_path;
        double timestamp;       // date last modified
        bool is_pinned;         // pinned (versioned) objects don't need to be checked for changes
        std::string ETag;       // md5 hash
    };

    std::map<std::string, Cache> cached_requests;

    // Determine a local path for an asset
    std::string generate_path(const std::string& path) {
        const std::string local_dir = get_env_var(CACHE_PATH_ENV_VAR, "/tmp");
        return TfNormPath(local_dir + "/" + get_bucket_name(path) + "/" + get_object_name(path));
    }

    // Check / resolve an asset with an S3 HEAD request and store the result in the cache
    // Set CACHE_NEEDS_FETCHING if the asset was updated
    // Requires the asset to be fetched before --
    std::string check_object(const std::string& path, Cache& cache) {
        if (s3_client == nullptr) {
            TF_DEBUG(S3_DBG).Msg("S3: check_object - abort due to s3_client nullptr\n");
            return std::string();
        }

        // versioned objects can't change... no need to check them
        // TODO: move this check?
        if (cache.is_pinned) {
            return cache.local_path;
        }

        Aws::S3::Model::HeadObjectRequest head_request;
        Aws::String bucket_name = get_bucket_name(path).c_str();
        Aws::String object_name = get_object_name(path).c_str();
        head_request.WithBucket(bucket_name).WithKey(object_name);

        if (uses_versioning(path)) {
            Aws::String object_versionid = get_object_versionid(path).c_str();
            head_request.WithVersionId(object_versionid);
            cache.is_pinned = true;
            TF_DEBUG(S3_DBG).Msg("S3: check_object bucket: %s and object: %s and version: %s\n",
                bucket_name.c_str(), object_name.c_str(), object_versionid.c_str());
        } else {
            TF_DEBUG(S3_DBG).Msg("S3: check_object bucket: %s and object: %s\n", bucket_name.c_str(), object_name.c_str());
        }

        auto head_object_outcome = s3_client->HeadObject(head_request);

        if (head_object_outcome.IsSuccess())
        {
            // TODO set local_dir in S3 constructor
            double date_modified = head_object_outcome.GetResult().GetLastModified().SecondsWithMSPrecision();
            TF_DEBUG(S3_DBG).Msg("S3: check_object OK %.0f\n", date_modified);
            // check
            std::string local_path = generate_path(path);
            if (date_modified > cache.timestamp) {
                cache.state = CACHE_NEEDS_FETCHING;
            }
            cache.timestamp = date_modified;
            cache.local_path = local_path;

            return local_path;
        }
        else
        {
            TF_DEBUG(S3_DBG).Msg("S3: check_object NOK\n");
            cache.timestamp = INVALID_TIME;
            std::cout << "HeadObjects error: " <<
                head_object_outcome.GetError().GetExceptionName() << " " <<
                head_object_outcome.GetError().GetMessage() << std::endl;
            return std::string();
        };
    }

    // Fetch an asset from S3 to the local_path set in the cache object.
    // Check for the presence of a local cache and only fetch the asset
    // when it was modified after the cached timestamp.
    bool fetch_object(const std::string& path, Cache& cache) {
        if (s3_client == nullptr) {
            TF_DEBUG(S3_DBG).Msg("S3: fetch_object - abort due to s3_client nullptr\n");
            return false;
        }

        Aws::S3::Model::GetObjectRequest object_request;
        Aws::String bucket_name = get_bucket_name(path).c_str();
        Aws::String object_name = get_object_name(path).c_str();
        object_request.WithBucket(bucket_name).WithKey(object_name);

        if (uses_versioning(path)) {
            Aws::String object_versionid = get_object_versionid(path).c_str();
            object_request.WithVersionId(object_versionid);
            cache.is_pinned = true;

            TF_DEBUG(S3_DBG).Msg("S3: fetch_object bucket: %s and object: %s and version: %s\n",
                bucket_name.c_str(), object_name.c_str(), object_versionid.c_str());
        } else {
            TF_DEBUG(S3_DBG).Msg("S3: fetch_object bucket: %s and object: %s\n", bucket_name.c_str(), object_name.c_str());
        }

        // Only download the asset if there's no local copy or if the local copy is outdated
        // The GET request returns a 304 (not modified).
        const std::string local_path = cache.local_path;
        if (TfPathExists(local_path)) {
            double local_date_modified;
            if (ArchGetModificationTime(local_path.c_str(), &local_date_modified)) {
                TF_DEBUG(S3_DBG).Msg("S3: fetch_object - found local asset\n");
                cache.timestamp = local_date_modified;
                object_request.WithIfModifiedSince(local_date_modified);
            }
            // TODO compare cache ETag with MD5 of local copy to know if fetching is required
        }

        auto get_object_outcome = s3_client->GetObject(object_request);

        if (get_object_outcome.IsSuccess())
        {
            TF_DEBUG(S3_DBG).Msg("S3: fetch_object %s success\n", path.c_str());
            // TODO: support directories in object_name
            // prepare cache directory
            const std::string& bucket_path = cache.local_path.substr(0, cache.local_path.find_last_of('/'));
            if (!TfIsDir(bucket_path)) {
                bool isSuccess = TfMakeDirs(bucket_path);
                if (! isSuccess) {
                    TF_DEBUG(S3_DBG).Msg("S3: fetch_object failed to create bucket directory\n");
                    return false;
                }
            }
            // TODO: restore the original datemodified on the asset
            Aws::OFStream local_file;
            local_file.open(cache.local_path, std::ios::out | std::ios::binary);
            local_file << get_object_outcome.GetResult().GetBody().rdbuf();
            cache.timestamp = get_object_outcome.GetResult().GetLastModified().SecondsWithMSPrecision();
            TF_DEBUG(S3_DBG).Msg("S3: fetch_object OK %.0f\n", cache.timestamp);
            //TF_DEBUG(S3_DBG).Msg("S3: fetch_object version: %s\n", get_object_outcome.GetResult().GetVersionId().c_str());
            cache.state = CACHE_FETCHED;
            cache.ETag = get_object_outcome.GetResult().GetETag().c_str();
            return true;
        }
        else
        {
            if (get_object_outcome.GetError().GetResponseCode() == Aws::Http::HttpResponseCode::NOT_MODIFIED) {
                //cache.timestamp = get_object_outcome.GetResult().GetLastModified().SecondsWithMSPrecision();
                TF_DEBUG(S3_DBG).Msg("S3: fetch_object OK (not modified)\n");
                cache.state = CACHE_FETCHED;
                return true;
            }
            std::cout << "GetObject error: " <<
                get_object_outcome.GetError().GetExceptionName() << " " <<
                get_object_outcome.GetError().GetMessage() << std::endl;
            return false;
        }
    }

    S3::S3() {
        TF_DEBUG(S3_DBG).Msg("S3: client setup \n");
        Aws::InitAPI(options);

        Aws::Client::ClientConfiguration config;
        // TODO: set executor to a PooledThreadExecutor to limit the number of threads
        config.scheme = Aws::Http::Scheme::HTTP;

        // set a custom endpoint e.g. an ActiveScale system node or minio server
        if (!get_env_var(ENDPOINT_ENV_VAR, "").empty()) {
            config.endpointOverride = (get_env_var(ENDPOINT_ENV_VAR, "")).c_str();
        }
        if (!get_env_var(PROXY_HOST_ENV_VAR, "").empty()) {
            config.proxyHost = get_env_var(PROXY_HOST_ENV_VAR, "").c_str();
            config.proxyPort = atoi(get_env_var(PROXY_PORT_ENV_VAR, "80").c_str());
        }

        config.connectTimeoutMs = 3000;
        config.requestTimeoutMs = 3000;

        // create a client with useVirtualAddressing=false to use path style addressing
        // see https://github.com/aws/aws-sdk-cpp/issues/587
        s3_client = Aws::New<Aws::S3::S3Client>("s3resolver", config, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);

    }

    S3::~S3() {
        TF_DEBUG(S3_DBG).Msg("S3: client teardown \n");
        Aws::Delete(s3_client);
        Aws::ShutdownAPI(options);
    }

    // Resolve an asset path such as 's3://hello/world.usd'
    // Checks if the asset exists and returns a local path for the asset
    std::string S3::resolve_name(const std::string& asset_path) {
        const auto path = parse_path(asset_path);
        TF_DEBUG(S3_DBG).Msg("S3: resolve_name %s\n", path.c_str());
        const auto cached_result = cached_requests.find(path);
        if (cached_result != cached_requests.end()) {
            if (cached_result->second.state == CACHE_FETCHED) {
                TF_DEBUG_TIMED_SCOPE(USD_S3_RESOLVER, "RESOLVE %s", path.c_str());
                TF_DEBUG(S3_DBG).Msg("S3: resolve_name - got cache, need check %s\n", path.c_str());
                return check_object(path, cached_result->second);
            }
            if (cached_result->second.state != CACHE_MISSING) {
                TF_DEBUG(S3_DBG).Msg("S3: resolve_name - use cached result for %s\n", path.c_str());
                return cached_result->second.local_path;
            }
            TF_DEBUG(S3_DBG).Msg("S3: resolve_name - refresh cached result for %s\n", path.c_str());
            // TODO: this should just generate a local path
            // return check_object(path, cached_result->second);
            return cached_result->second.local_path;
        } else {
            Cache cache{
                CACHE_NEEDS_FETCHING,
                generate_path(path)
            };
            TF_DEBUG(S3_DBG).Msg("S3: resolve_name - no cache for %s\n", path.c_str());
            // std::string result = check_object(path, cache);
            // TODO: this should just generate a local path
            cached_requests.insert(std::make_pair(path, cache));
            return cache.local_path;
        }
    }

    // Update asset info for resolved assets
    // If the asset needs fetching, nothing is done as the cache is updated during the fetch phase
    // If the asset doesn't need fetching, also do nothing (lol)
    void S3::update_asset_info(const std::string& asset_path) {
        // const auto path = parse_path(asset_path);
        // const auto cached_result = cached_requests.find(path);
        // if (cached_result != cached_requests.end()) {
        //     //
        //     if (cached_result->second.state == CACHE_NEEDS_FETCHING) {
        //         return;
        //     }

        //     TF_DEBUG(S3_DBG).Msg("S3: update_asset_info %s cache state %d\n", path.c_str(), cached_result->second.state);
        //     //cached_result->second.state = CACHE_NEEDS_FETCHING;
        //     //cached_result->second.timestamp = INVALID_TIME;
        //     //check_object(path, cached_result->second);
        // }
    }

    // Fetch an asset to a local path
    // The asset should be resolved first and exist in the cache
    bool S3::fetch_asset(const std::string& asset_path, const std::string& local_path) {
        const auto path = parse_path(asset_path);
        TF_DEBUG(S3_DBG).Msg("S3: fetch_asset %s\n", path.c_str());
        if (s3_client == nullptr) {
            TF_DEBUG(S3_DBG).Msg("S3: fetch_asset - abort due to s3_client nullptr\n");
            return false;
        }

        const auto cached_result = cached_requests.find(path);
        if (cached_result == cached_requests.end()) {
            S3_WARN("[S3Resolver] %s was not resolved before fetching!", path.c_str());
            return false;
        }

        if (cached_result->second.state == CACHE_NEEDS_FETCHING) {
            TF_DEBUG(S3_DBG).Msg("S3: fetch_asset - cache needed fetching\n");
            cached_result->second.state = CACHE_MISSING; // we'll set this up if fetching is successful
            bool success = fetch_object(path, cached_result->second);
            return success;
        } else {
            TF_DEBUG(S3_DBG).Msg("S3: fetch_asset - cache does not need fetch\n");
        }
        return true;
    }

    // returns true if the path matches the S3 schema
    bool S3::matches_schema(const std::string& path) {
        constexpr auto schema_length_short = cexpr_strlen(usd_s3::S3_PREFIX_SHORT);
        return path.compare(0, schema_length_short, usd_s3::S3_PREFIX_SHORT) == 0;
    }

    // returns the timestamp of the local cached asset
    double S3::get_timestamp(const std::string& asset_path) {
        const auto path = parse_path(asset_path);
        if (s3_client == nullptr) {
            return 1.0;
        }

        const auto cached_result = cached_requests.find(path);
        if (cached_result == cached_requests.end() ||
                cached_result->second.state == CACHE_MISSING) {
            S3_WARN("[S3Resolver] %s is missing when querying timestamps!",
                    path.c_str());
            return 1.0;
        } else {
            return cached_result->second.timestamp;
        }
    }

    // refresh all assets with this prefix
    void S3::refresh(const std::string& prefix) {
        if (prefix.empty()) {
            // refresh all assets
            cached_requests.clear();
        } else {
            // TODO: remove only matching assets
            // and reload based on S3 list operation with prefix
            cached_requests.clear();
        }
    }

}
