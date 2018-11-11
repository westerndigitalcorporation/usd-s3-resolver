#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/type.h>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/definePackageResolver.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/packageResolver.h>
#include <pxr/usd/ar/assetInfo.h>
#include <pxr/usd/ar/resolverContext.h>

#include <pxr/usd/ar/packageResolver.h>
#include <pxr/usd/ar/threadLocalScopedCache.h>
#include <pxr/usd/usd/zipFile.h>

#include <tbb/concurrent_hash_map.h>
#include <memory>

#include "resolver.h"
#include "s3.h"
#include "debugCodes.h"

/*
 * Depending on the asset count and access frequency, it could be better to store the
 * resolver paths in a sorted vector, rather than a map. That's way faster when we are
 * doing significantly more queries than inserts.
 */

PXR_NAMESPACE_OPEN_SCOPE

#define S3_WARN TF_WARN

namespace {
    usd_s3::S3 g_s3;
}

AR_DEFINE_RESOLVER(S3Resolver, ArResolver)

struct S3Resolver::_Cache
{
    using _PathToResolvedPathMap =
        tbb::concurrent_hash_map<std::string, std::string>;
    _PathToResolvedPathMap _pathToResolvedPathMap;
};

S3Resolver::S3Resolver() : ArDefaultResolver()
{
    TF_DEBUG(USD_S3_RESOLVER).Msg("Loading the S3Resolver\n");
}

S3Resolver::~S3Resolver()
{}

bool S3Resolver::IsRelativePath(const std::string& path)
{
    return !g_s3.matches_schema(path) && ArDefaultResolver::IsRelativePath(path);
}

std::string S3Resolver::Resolve(const std::string& path)
{
    return S3Resolver::ResolveWithAssetInfo(path, nullptr);
}

std::string S3Resolver::ResolveWithAssetInfo(
    const std::string& path,
    ArAssetInfo* assetInfo)
{
    if (path.empty()) {
        return path;
    }

    // S3 assets have their own cache
    if (g_s3.matches_schema(path)) {
        TF_DEBUG(USD_S3_RESOLVER).Msg("S3Resolver RESOLVE %s \n", path.c_str());
        return g_s3.resolve_name(path);
    }
    // handle other assets with the default cache
    if (_CachePtr currentCache = _GetCurrentCache()) {
        _Cache::_PathToResolvedPathMap::accessor accessor;
        if (currentCache->_pathToResolvedPathMap.insert(
                accessor, std::make_pair(path, std::string()))) {
            accessor->second = ArDefaultResolver::ResolveWithAssetInfo(path, nullptr);
        }
        return accessor->second;
    }
    return ArDefaultResolver::ResolveWithAssetInfo(path, nullptr);
}

VtValue S3Resolver::GetModificationTimestamp(
    const std::string& path,
    const std::string& resolvedPath)
{
    if (g_s3.matches_schema(path)) {
        TF_DEBUG(USD_S3_RESOLVER).Msg("S3Resolver TIMESTAMP %s \n", path.c_str());
    }

    return g_s3.matches_schema(path) ?
           VtValue(g_s3.get_timestamp(path)) :
           ArDefaultResolver::GetModificationTimestamp(path, resolvedPath);
}

bool S3Resolver::FetchToLocalResolvedPath(const std::string& path, const std::string& resolvedPath)
{
    if (g_s3.matches_schema(path)) {
        TF_DEBUG(USD_S3_RESOLVER).Msg("S3Resolver FETCH %s to %s\n", path.c_str(), resolvedPath.c_str());
        return g_s3.fetch_asset(path, resolvedPath);
    } else {
        return ArDefaultResolver::FetchToLocalResolvedPath(path, resolvedPath);
    }
}

void
S3Resolver::BeginCacheScope(
    VtValue* cacheScopeData)
{
    _cache.BeginCacheScope(cacheScopeData);
}

void
S3Resolver::EndCacheScope(
    VtValue* cacheScopeData)
{
    _cache.EndCacheScope(cacheScopeData);
}

S3Resolver::_CachePtr
S3Resolver::_GetCurrentCache()
{
    return _cache.GetCurrentCache();
}

// ------------------------------------------------------------

PXR_NAMESPACE_CLOSE_SCOPE
