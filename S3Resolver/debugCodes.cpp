#include "debugCodes.h"

#include <pxr/pxr.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/registryManager.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(S3_DBG, "S3 Debug");
    TF_DEBUG_ENVIRONMENT_SYMBOL(USD_S3_RESOLVER, "Usd S3 handling asset resolver");
    TF_DEBUG_ENVIRONMENT_SYMBOL(USD_S3_FILEFORMAT, "Usd S3 fileformat");
}

PXR_NAMESPACE_CLOSE_SCOPE
