"""
Test USD asset resolver

Copyright (c) 2018 Western Digital Corporation or its affiliates.
SPDX-License-Identifier: MIT
This code is based on code with license:
=====================================================================================
https://boto3.readthedocs.io/en/latest/guide/s3-example-creating-buckets.html
* Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
*  http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
"""
import os
import time
import sys
import boto3

from pxr import Usd, Ar


def upload(localpath, remotepath):
    """Upload an asset at localpath to S3"""
    session = boto3.session.Session()
    endpoint = os.environ.get('USD_S3_PROXY_HOST') + ':' + os.environ.get('USD_S3_PROXY_PORT')
    client = session.client(service_name='s3', endpoint_url=endpoint)
    bucket, asset = remotepath.partition('s3:')[2].lstrip('/').split('/', 1)
    print 'Uploading {0} to bucket {1} as object {2}'.format(localpath, bucket, asset)
    client.upload(localpath, bucket, asset)

def main(usd_file, local_file):
    """Test USD asset resolver"""

    # ensure the default context gets set
    resolver = Ar.GetResolver() #pylint: disable=no-member
    resolver.ConfigureResolverForAsset(usd_file)

    print "Open stage"
    start = time.time()
    stage = Usd.Stage.Open(usd_file) #pylint: disable=no-member
    print "Opened stage in", time.time() - start

    if local_file:
        upload(local_file, usd_file)

    print "Reload stage (but not the context cache)"
    start = time.time()
    stage.Reload()
    print "Reloaded stage in", time.time() - start

    print "Reload stage (including the context cache)"
    start = time.time()
    context = resolver.GetCurrentContext()
    print 'context:', context
    resolver.RefreshContext(context)
    stage.Reload()
    print "Reloaded stage in", time.time() - start


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage: test_s3.py [s3:]asset.usd [local_asset] \n" \
              "If local_asset is defined, it gets uploaded " \
              "after opening the stage"
        sys.exit(0)
    usd_file = sys.argv[1]
    local_file = sys.argv[2] if len(sys.argv) > 2 else None
    main(usd_file, local_file)
