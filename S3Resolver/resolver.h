#ifndef S3_RESOLVER_H
#define S3_RESOLVER_H

//#include <pxr/usd/usd/zipFile.h>

#include <pxr/usd/ar/assetInfo.h>
#include <pxr/usd/ar/defaultResolver.h>
#include "pxr/usd/ar/threadLocalScopedCache.h"

#include <string>

#include "object.h"

PXR_NAMESPACE_OPEN_SCOPE

// \class S3Resolver
///
/// Resolves assets on an S3 object store
///
class S3Resolver
    : public ArDefaultResolver
{
public:
    S3Resolver();
    ~S3Resolver() override;

    virtual std::string Resolve(const std::string& path) override;

    virtual std::string ResolveWithAssetInfo(
        const std::string& path,
        ArAssetInfo* assetInfo) override;

    virtual bool IsRelativePath(const std::string& path);

    VtValue GetModificationTimestamp(
        const std::string& path,
        const std::string& resolvedPath) override;

    virtual bool FetchToLocalResolvedPath(
        const std::string& path,
        const std::string& resolvedPath) override;

    virtual void BeginCacheScope(
        VtValue* cacheScopeData) override;

    virtual void EndCacheScope(
        VtValue* cacheScopeData) override;

private:
    struct _Cache;
    using ResolveCache = ArThreadLocalScopedCache<_Cache>;
    using _CachePtr = ResolveCache::CachePtr;
    ResolveCache _cache;
    _CachePtr _GetCurrentCache();
};



PXR_NAMESPACE_CLOSE_SCOPE

#endif // S3_RESOLVER_H